#include "TeachWidget.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSpinBox>
#include <QCheckBox>
#include <cmath>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QFileDialog>
#include <QFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QTimer>
#include <QInputDialog>
#include <QMessageBox>

namespace {
const char *gripperStateText(TeachWidget::GripperState s) {
    switch (s) {
        case TeachWidget::GripperUnchanged: return "夹爪: 不变";
        case TeachWidget::GripperOpen:      return "夹爪: 张开 (30%)";
        case TeachWidget::GripperCloseSoft: return "夹爪: 闭合 (30%)";
        case TeachWidget::GripperCloseFirm: return "夹爪: 闭合 (90%)";
    }
    return "?";
}
const char *gripperStateColor(TeachWidget::GripperState s) {
    switch (s) {
        case TeachWidget::GripperUnchanged: return "#8a90a8";
        case TeachWidget::GripperOpen:      return "#6ed8ff";
        case TeachWidget::GripperCloseSoft: return "#ffc86e";
        case TeachWidget::GripperCloseFirm: return "#ff6e6e";
    }
    return "#aab";
}
}

TeachWidget::TeachWidget(RpcClient *rpc, QWidget *parent)
    : QGroupBox(QStringLiteral("轨迹跟踪 (标记 / 回放)"), parent)
    , rpc_(rpc)
{
    setStyleSheet(
        "QGroupBox {"
        " border: 1px solid #ffc86e; border-radius: 6px;"
        " margin-top: 12px; color: #d8e0f0; background: rgba(28,34,50,200);"
        "}"
        "QGroupBox::title {"
        " subcontrol-origin: margin; left: 10px; padding: 0 8px;"
        " color: #ffc86e; font-weight: bold;"
        "}");
    buildUi();

    replay_timer_ = new QTimer(this);
    // 20 ms, not 50. A continuously captured trajectory has steps as short
    // as the capture interval (50 ms at the default 20 Hz), and a tick that
    // coarse can only advance one waypoint per tick — the playback rate
    // would be set by the timer instead of by the recorded timing, and any
    // scheduling jitter would land whole waypoints late. Ticking faster
    // than the shortest step keeps the recorded timestamps in charge.
    replay_timer_->setInterval(20);
    connect(replay_timer_, &QTimer::timeout, this, &TeachWidget::onReplayTick);

    auto_timer_ = new QTimer(this);
    auto_timer_->setInterval(50);        // 20Hz default; reset on enable
    connect(auto_timer_, &QTimer::timeout, this, &TeachWidget::onAutoRecordTick);
}

void TeachWidget::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 16, 8, 8);
    root->setSpacing(6);

    // ── Row 1: record / delete / clear ───────────────────────────────
    auto *row1 = new QHBoxLayout;
    row1->setSpacing(4);
    btn_record_ = new QPushButton(QStringLiteral("📍 标记"), this);
    btn_delete_ = new QPushButton(QStringLiteral("✂ 删所选"), this);
    btn_clear_  = new QPushButton(QStringLiteral("🗑 清空"), this);
    btn_record_->setFixedHeight(28);
    btn_delete_->setFixedHeight(28);
    btn_clear_->setFixedHeight(28);
    btn_record_->setStyleSheet(
        "QPushButton{ background:#3a8; color:white; border-radius:4px; "
        "font-weight:bold; padding:2px 8px;}"
        "QPushButton:hover{ background:#4ab;}"
        "QPushButton:disabled{ background:#446; color:#aab;}");
    btn_delete_->setStyleSheet(
        "QPushButton{ background:#664422; color:white; border-radius:4px; padding:2px 8px;}"
        "QPushButton:hover{ background:#774;}"
        "QPushButton:disabled{ background:#444; color:#aab;}");
    btn_clear_->setStyleSheet(
        "QPushButton{ background:#553; color:#ddd; border-radius:4px; padding:2px 8px;}"
        "QPushButton:disabled{ background:#444; color:#aab;}");
    row1->addWidget(btn_record_);
    row1->addWidget(btn_delete_);
    row1->addWidget(btn_clear_);
    root->addLayout(row1);

    // ── Row 2: save / load + step duration ───────────────────────────
    auto *row2 = new QHBoxLayout;
    row2->setSpacing(4);
    btn_save_ = new QPushButton(QStringLiteral("💾 保存"), this);
    btn_load_ = new QPushButton(QStringLiteral("📂 加载"), this);
    btn_save_->setFixedHeight(24);
    btn_load_->setFixedHeight(24);
    btn_save_->setStyleSheet("QPushButton{ background:#445; color:#ddd; border-radius:3px; padding:2px 8px;}");
    btn_load_->setStyleSheet("QPushButton{ background:#445; color:#ddd; border-radius:3px; padding:2px 8px;}");

    auto *stepLbl = new QLabel(QStringLiteral("步时长:"), this);
    stepLbl->setStyleSheet("color:#aab6cc;");
    step_ms_spin_ = new QSpinBox(this);
    step_ms_spin_->setRange(200, 10000);
    step_ms_spin_->setSingleStep(100);
    step_ms_spin_->setValue(1500);
    step_ms_spin_->setSuffix(" ms");
    step_ms_spin_->setFixedWidth(110);

    row2->addWidget(btn_save_);
    row2->addWidget(btn_load_);
    row2->addStretch();
    row2->addWidget(stepLbl);
    row2->addWidget(step_ms_spin_);
    root->addLayout(row2);

    // ── Row 2b: 连续轨迹录制 ──────────────────────────────────────────
    auto *rowAuto = new QHBoxLayout;
    rowAuto->setSpacing(4);
    auto_record_chk_ = new QCheckBox(QStringLiteral("连续轨迹录制"), this);
    auto_record_chk_->setToolTip(QStringLiteral(
        "勾选后监测机械臂固件的 teach_status:\n"
        "  用手拖动机械臂 → 自动开始采集\n"
        "  松手            → 自动结束本段\n"
        "无需按开始/停止, 抓住机械臂就是开始。\n"
        "采集到的点带时间戳, 回放时按当时的速度还原。"));
    auto_record_chk_->setStyleSheet("color:#ddd;");

    auto *rateLbl = new QLabel(QStringLiteral("频率:"), this);
    rateLbl->setStyleSheet("color:#aab6cc;");
    auto_rate_spin_ = new QSpinBox(this);
    auto_rate_spin_->setRange(5, 50);
    auto_rate_spin_->setValue(20);
    auto_rate_spin_->setSuffix(" Hz");
    auto_rate_spin_->setFixedWidth(80);
    auto_rate_spin_->setToolTip(QStringLiteral(
        "proc_piper 的状态缓存约 20ms 刷新一次 (50Hz), 采样再快也只是\n"
        "重复读到同一份快照。手动拖动的真实频率远低于 5Hz, 20Hz 已经\n"
        "远超奈奎斯特, 且 30 秒示教约 600 点而不是 1500 点。"));

    auto *mindegLbl = new QLabel(QStringLiteral("静止阈值:"), this);
    mindegLbl->setStyleSheet("color:#aab6cc;");
    auto_mindeg_spin_ = new QSpinBox(this);
    auto_mindeg_spin_->setRange(0, 50);
    auto_mindeg_spin_->setValue(3);            // 0.3°
    auto_mindeg_spin_->setSuffix(QStringLiteral(" ×0.1°"));
    auto_mindeg_spin_->setFixedWidth(90);
    auto_mindeg_spin_->setToolTip(QStringLiteral(
        "相邻采样中最大关节变化小于该值就丢弃, 避免示教开头/结尾\n"
        "握住不动的那几秒变成几百个重复点 (回放时是纯粹的死时间)。\n"
        "设为 0 = 全部保留。"));

    rowAuto->addWidget(auto_record_chk_);
    rowAuto->addStretch();
    rowAuto->addWidget(rateLbl);
    rowAuto->addWidget(auto_rate_spin_);
    rowAuto->addWidget(mindegLbl);
    rowAuto->addWidget(auto_mindeg_spin_);
    root->addLayout(rowAuto);

    auto_state_label_ = new QLabel(QStringLiteral("未启用"), this);
    auto_state_label_->setStyleSheet("color:#888aaa; font-family: Consolas;");
    root->addWidget(auto_state_label_);

    // ── Row 3: replay / stop ──────────────────────────────────────────
    auto *row3 = new QHBoxLayout;
    row3->setSpacing(4);
    btn_replay_ = new QPushButton(QStringLiteral("▶ 回放"), this);
    btn_stop_   = new QPushButton(QStringLiteral("⏸ 停止"), this);
    btn_replay_->setFixedHeight(28);
    btn_stop_->setFixedHeight(28);
    btn_replay_->setStyleSheet(
        "QPushButton{ background:#2d5fb2; color:white; border-radius:4px; "
        "font-weight:bold; padding:2px 8px;}"
        "QPushButton:hover{ background:#3a73c9;}"
        "QPushButton:disabled{ background:#446; color:#aab;}");
    btn_stop_->setStyleSheet(
        "QPushButton{ background:#c33; color:white; border-radius:4px; "
        "font-weight:bold; padding:2px 8px;}"
        "QPushButton:disabled{ background:#553; color:#aab;}");
    btn_stop_->setEnabled(false);
    row3->addWidget(btn_replay_);
    row3->addWidget(btn_stop_);
    root->addLayout(row3);

    // ── Waypoint list ─────────────────────────────────────────────────
    count_label_ = new QLabel(QStringLiteral("0 个点  (双击行修改夹爪/停留)"), this);
    count_label_->setStyleSheet("color:#9aa0b8; font-family: Consolas;");
    root->addWidget(count_label_);

    wp_list_ = new QListWidget(this);
    wp_list_->setMinimumHeight(140);
    wp_list_->setMaximumHeight(220);
    wp_list_->setStyleSheet(
        "QListWidget{ background:#10141d; color:#d8e0f0; border:1px solid #2a3247;"
        " font-family: Consolas; font-size: 11px;}"
        "QListWidget::item{ padding: 2px 6px; }"
        "QListWidget::item:selected{ background:#2d5fb2; }");
    root->addWidget(wp_list_);

    // ── Log strip ────────────────────────────────────────────────────
    log_view_ = new QPlainTextEdit(this);
    log_view_->setReadOnly(true);
    log_view_->setMaximumHeight(70);
    log_view_->setStyleSheet(
        "QPlainTextEdit{ background:#0e131c; color:#cad3e4; border:1px solid #2a3247;"
        " font-family: Consolas; font-size: 10px; padding:2px;}");
    root->addWidget(log_view_);

    connect(btn_record_, &QPushButton::clicked, this, &TeachWidget::onRecordPoint);
    connect(auto_record_chk_, &QCheckBox::toggled, this, &TeachWidget::onAutoRecordToggled);
    // Rate is applied live so the operator can tune it between drags
    // without having to toggle the mode off and on.
    connect(auto_rate_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int hz) { if (auto_timer_) auto_timer_->setInterval(1000 / qBound(5, hz, 50)); });
    connect(btn_delete_, &QPushButton::clicked, this, &TeachWidget::onDeleteSelected);
    connect(btn_clear_,  &QPushButton::clicked, this, &TeachWidget::onClearAll);
    connect(btn_save_,   &QPushButton::clicked, this, &TeachWidget::onSaveToFile);
    connect(btn_load_,   &QPushButton::clicked, this, &TeachWidget::onLoadFromFile);
    connect(btn_replay_, &QPushButton::clicked, this, &TeachWidget::onStartReplay);
    connect(btn_stop_,   &QPushButton::clicked, this, &TeachWidget::onStopReplay);
    connect(wp_list_,    &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        onRowDoubleClicked(wp_list_->row(it));
    });
}

void TeachWidget::appendLog(const QString &msg)
{
    const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    log_view_->appendPlainText(QString("[%1] %2").arg(ts, msg));
}

void TeachWidget::rebuildList()
{
    wp_list_->clear();
    for (int i = 0; i < waypoints_.size(); ++i) {
        const Waypoint &w = waypoints_[i];
        QString joints;
        for (int k = 0; k < w.joints.size(); ++k) {
            if (k) joints += " ";
            joints += QString::number(w.joints[k], 'f', 1);
        }
        // Note (备注) goes right after the label so the operator can
        // see at a glance what each point is for. If empty, the cell
        // is just blank padding.
        const QString note_cell = w.note.isEmpty()
            ? QString(20, QChar(' '))
            : QString("📝 %1").arg(w.note).leftJustified(20, ' ', true);
        const QString line = QStringLiteral("#%1 %2 %3 [%4] %5 ms")
            .arg(i + 1, 2, 10, QChar('0'))
            .arg(w.label.leftJustified(8, ' '))
            .arg(note_cell)
            .arg(joints)
            .arg(w.dwell_ms);
        auto *item = new QListWidgetItem(line);
        item->setToolTip(QString("%1\n备注: %2\n%3\ndwell %4 ms")
                             .arg(w.label,
                                  w.note.isEmpty() ? QStringLiteral("(无)") : w.note,
                                  gripperStateText(w.gripper_state))
                             .arg(w.dwell_ms));
        item->setForeground(QColor(gripperStateColor(w.gripper_state)));
        wp_list_->addItem(item);
    }
    count_label_->setText(QStringLiteral("%1 个点  (双击行修改备注/夹爪/停留)")
                              .arg(waypoints_.size()));
}

// ── Record current arm pose as a new waypoint ──────────────────────
// ── 连续轨迹录制 ────────────────────────────────────────────────────────
//
// Sampling rate: proc_piper refreshes its status cache every 2 heartbeats
// (~20 ms), so 50 Hz is the ceiling — polling faster only re-reads the same
// snapshot. 20 Hz is the default: hand-dragging carries well under 5 Hz of
// real content, so 20 Hz is comfortably past Nyquist, and it keeps a 30 s
// teach to ~600 points instead of ~1500.
void TeachWidget::onAutoRecordToggled(bool on)
{
    if (!on) {
        auto_timer_->stop();
        if (auto_capturing_) {
            // Toggled off mid-drag: keep what was captured rather than
            // discarding the operator's work.
            appendLog(QStringLiteral("⏹ 录制模式关闭 — 本次已保留 %1 点")
                          .arg(auto_session_count_));
        }
        auto_capturing_ = false;
        auto_last_teach_status_ = -1;
        auto_state_label_->setText(QStringLiteral("未启用"));
        auto_state_label_->setStyleSheet("color:#888aaa; font-family: Consolas;");
        return;
    }
    if (!rpc_ || !rpc_->isConnected()) {
        appendLog(QStringLiteral("⚠ RPC 未连接, 无法启用录制模式"));
        auto_record_chk_->setChecked(false);
        return;
    }
    auto_capturing_    = false;
    auto_last_teach_status_ = -1;
    auto_poll_inflight_ = false;
    auto_timer_->setInterval(1000 / qBound(5, auto_rate_spin_->value(), 50));
    auto_timer_->start();
    auto_state_label_->setText(QStringLiteral("等待示教 — 用手拖动机械臂即开始"));
    auto_state_label_->setStyleSheet("color:#e0a030; font-family: Consolas;");
    appendLog(QStringLiteral("▶ 录制模式已启用 (%1Hz) — 监测 teach_status")
                  .arg(auto_rate_spin_->value()));
}

void TeachWidget::onAutoRecordTick()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    // Never stack requests. If a reply is late, skipping this tick keeps the
    // timeline honest; queueing would build a backlog that reports poses
    // long after they happened.
    if (auto_poll_inflight_) return;
    auto_poll_inflight_ = true;

    rpc_->call(Protocol::Methods::PIPER_GET_STATUS, QJsonObject{},
        [this](QJsonObject reply) {
            auto_poll_inflight_ = false;
            if (!auto_record_chk_ || !auto_record_chk_->isChecked()) return;

            const int ts = reply.value("teach_status").toInt(0);
            const QJsonArray arr = reply.value(Protocol::Fields::ANGLES).toArray();

            // ── 进入示教 ──
            if (ts == 1 && !auto_capturing_) {
                auto_capturing_     = true;
                auto_capture_t0_    = QDateTime::currentMSecsSinceEpoch();
                auto_session_count_ = 0;
                auto_skipped_still_ = 0;
                auto_last_joints_.clear();
                auto_state_label_->setText(QStringLiteral("● 录制中…"));
                auto_state_label_->setStyleSheet(
                    "color:#e05050; font-family: Consolas; font-weight:bold;");
                appendLog(QStringLiteral("● 检测到进入示教模式 — 开始采集"));
            }

            // ── 退出示教 ──
            // Any value other than 1 ends the session: 2 is "released", 0 is
            // a full reset via the 使能 handshake. Both mean the operator is
            // no longer dragging.
            if (ts != 1 && auto_capturing_) {
                auto_capturing_ = false;
                const qint64 dur = QDateTime::currentMSecsSinceEpoch() - auto_capture_t0_;
                auto_state_label_->setText(
                    QStringLiteral("等待示教 — 上次 %1 点 / %2s")
                        .arg(auto_session_count_).arg(dur / 1000.0, 0, 'f', 1));
                auto_state_label_->setStyleSheet("color:#3ac06a; font-family: Consolas;");
                appendLog(QStringLiteral("■ 退出示教 (teach_status=%1) — 采集 %2 点, "
                                         "耗时 %3s, 静止跳过 %4 帧")
                              .arg(ts).arg(auto_session_count_)
                              .arg(dur / 1000.0, 0, 'f', 1).arg(auto_skipped_still_));
                rebuildList();
            }
            auto_last_teach_status_ = ts;
            if (!auto_capturing_) return;

            if (arr.size() != 6) {
                // Older proc_piper builds don't put `angles` in get_status.
                // Fall back to a second call rather than silently recording
                // nothing — a recorder that captures zero points while
                // showing "录制中" is the worst possible failure here.
                // The two samples are then up to one tick apart; that is
                // acceptable, and the fix is to deploy the newer proc_piper.
                if (!auto_warned_no_angles_) {
                    auto_warned_no_angles_ = true;
                    appendLog(QStringLiteral(
                        "⚠ piper.get_status 未返回 angles — 回退到单独调用 "
                        "arm.get_angles (建议更新 proc_piper)"));
                }
                rpc_->call(Protocol::Methods::ARM_GET_ANGLES, QJsonObject{},
                    [this](QJsonObject r2) {
                        const QJsonArray a2 = r2.value(Protocol::Fields::ANGLES).toArray();
                        if (a2.size() != 6 || !auto_capturing_) return;
                        QVector<float> j2;
                        for (const auto &v : a2) j2.append(float(v.toDouble()));
                        appendAutoSample(j2);
                    });
                return;
            }

            QVector<float> j;
            j.reserve(6);
            for (const auto &v : arr) j.append(float(v.toDouble()));
            appendAutoSample(j);
        });
}

// Shared tail for both sampling paths: dedup against the previous sample,
// then append with a timestamp.
void TeachWidget::appendAutoSample(const QVector<float> &j)
{
    if (!auto_capturing_ || j.size() != 6) return;

    // Drop samples where nothing moved. A teach session usually starts and
    // ends with the operator holding still, and those stretches would
    // otherwise become hundreds of identical waypoints that replay as dead
    // time. The timestamp is still taken from the wall clock, so skipping
    // does not compress the trajectory — the pause is preserved as a longer
    // interval between the two points that bracket it.
    if (!auto_last_joints_.isEmpty()) {
        float max_delta = 0.0f;
        for (int i = 0; i < 6 && i < auto_last_joints_.size(); ++i) {
            max_delta = std::max(max_delta, std::abs(j[i] - auto_last_joints_[i]));
        }
        if (max_delta < auto_mindeg_spin_->value() / 10.0f) {
            ++auto_skipped_still_;
            return;
        }
    }

    Waypoint w;
    w.label  = QStringLiteral("t%1").arg(waypoints_.size() + 1);
    w.joints = j;
    w.gripper_state     = GripperUnchanged;
    w.gripper_angle     = 60;
    w.gripper_force_pct = 30;
    w.dwell_ms = 0;
    w.t_ms = int(QDateTime::currentMSecsSinceEpoch() - auto_capture_t0_);
    waypoints_.append(w);
    auto_last_joints_ = j;
    ++auto_session_count_;

    // The list widget is rebuilt when the session ends, not per sample — at
    // 20 Hz a full rebuild per point would dominate the UI thread and add
    // jitter to the very timestamps being recorded.
    auto_state_label_->setText(
        QStringLiteral("● 录制中… %1 点").arg(auto_session_count_));
    count_label_->setText(QStringLiteral("共 %1 点").arg(waypoints_.size()));
}

void TeachWidget::onRecordPoint()
{
    if (!rpc_ || !rpc_->isConnected()) {
        appendLog(QStringLiteral("⚠ RPC 未连接"));
        return;
    }
    rpc_->call(Protocol::Methods::ARM_GET_ANGLES, QJsonObject{},
        [this](QJsonObject reply) {
            const QJsonArray arr = reply.value(Protocol::Fields::ANGLES).toArray();
            if (arr.size() != 6) {
                appendLog(QStringLiteral("标记失败: arm.get_angles 返回非 6 元素"));
                return;
            }
            Waypoint w;
            w.label = QStringLiteral("wp_%1").arg(waypoints_.size() + 1);
            for (const auto &v : arr) w.joints.append(float(v.toDouble()));
            w.gripper_state    = GripperUnchanged;
            w.gripper_angle    = 60;
            w.gripper_force_pct = 30;
            w.dwell_ms = 0;
            waypoints_.append(w);
            rebuildList();
            appendLog(QStringLiteral("✓ 标记第 %1 点").arg(waypoints_.size()));
        });
}

void TeachWidget::onDeleteSelected()
{
    const int row = wp_list_->currentRow();
    if (row < 0 || row >= waypoints_.size()) return;
    waypoints_.removeAt(row);
    rebuildList();
    appendLog(QStringLiteral("删除第 %1 点").arg(row + 1));
}

void TeachWidget::onClearAll()
{
    if (waypoints_.isEmpty()) return;
    if (QMessageBox::question(this, QStringLiteral("清空"),
            QStringLiteral("确定清空所有 %1 个点?").arg(waypoints_.size()))
        != QMessageBox::Yes) return;
    waypoints_.clear();
    rebuildList();
    appendLog(QStringLiteral("已清空"));
}

// ── Save to JSON ───────────────────────────────────────────────────
void TeachWidget::onSaveToFile()
{
    if (waypoints_.isEmpty()) {
        appendLog(QStringLiteral("无点可保存"));
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this,
        QStringLiteral("保存轨迹"),
        QStringLiteral("轨迹名称:"),
        QLineEdit::Normal,
        QStringLiteral("teach_%1")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
        &ok);
    if (!ok || name.isEmpty()) return;

    const QString path = QFileDialog::getSaveFileName(this,
        QStringLiteral("保存轨迹文件"),
        name + ".json",
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;

    QJsonArray jarr;
    for (const Waypoint &w : waypoints_) {
        QJsonObject jo;
        jo["label"]             = w.label;
        jo["note"]              = w.note;
        QJsonArray jj;
        for (float v : w.joints) jj.append(v);
        jo["joints"]            = jj;
        jo["gripper_state"]     = int(w.gripper_state);   // 0..3
        jo["gripper_angle"]     = w.gripper_angle;
        jo["gripper_force_pct"] = w.gripper_force_pct;
        jo["dwell_ms"]          = w.dwell_ms;
        // Only emitted for continuously captured points. Its presence is
        // what tells replay to use the taught timing instead of the
        // per-step estimate, so it must survive a save/load round-trip.
        if (w.t_ms >= 0) jo["t_ms"] = w.t_ms;
        jarr.append(jo);
    }
    QJsonObject root;
    root["name"]            = name;
    root["captured"]        = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["default_step_ms"] = step_ms_spin_->value();
    root["waypoints"]       = jarr;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        appendLog(QStringLiteral("写入失败: %1").arg(f.errorString()));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    appendLog(QStringLiteral("✓ 已保存 %1 个点 → %2").arg(waypoints_.size()).arg(path));
}

// ── Load JSON ──────────────────────────────────────────────────────
void TeachWidget::onLoadFromFile()
{
    const QString path = QFileDialog::getOpenFileName(this,
        QStringLiteral("加载轨迹文件"), QString(), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        appendLog(QStringLiteral("打开失败: %1").arg(f.errorString()));
        return;
    }
    const QByteArray raw = f.readAll();
    f.close();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        appendLog(QStringLiteral("JSON 解析失败: %1").arg(err.errorString()));
        return;
    }
    const QJsonObject root = doc.object();
    const QJsonArray jarr = root.value("waypoints").toArray();
    waypoints_.clear();
    for (const auto &v : jarr) {
        const QJsonObject jo = v.toObject();
        Waypoint w;
        w.label = jo.value("label").toString();
        w.note  = jo.value("note").toString();   // empty for older files
        for (const auto &j : jo.value("joints").toArray()) {
            w.joints.append(float(j.toDouble()));
        }
        w.gripper_state    = GripperState(jo.value("gripper_state").toInt(0));
        w.gripper_angle    = jo.value("gripper_angle").toInt(60);
        w.gripper_force_pct = jo.value("gripper_force_pct").toInt(30);
        w.dwell_ms          = jo.value("dwell_ms").toInt(0);
        w.t_ms              = jo.value("t_ms").toInt(-1);   // absent → hand-marked
        waypoints_.append(w);
    }
    const int step = root.value("default_step_ms").toInt(1500);
    step_ms_spin_->setValue(step);
    rebuildList();
    appendLog(QStringLiteral("✓ 加载 %1 个点").arg(waypoints_.size()));
}

// ── Replay state machine ───────────────────────────────────────────
void TeachWidget::onStartReplay()
{
    if (waypoints_.isEmpty()) {
        appendLog(QStringLiteral("无轨迹可回放"));
        return;
    }
    if (!rpc_ || !rpc_->isConnected()) {
        appendLog(QStringLiteral("⚠ RPC 未连接"));
        return;
    }
    // Pre-flight: confirm the arm is in CAN_CTRL. After a physical-
    // button teach session it'll likely still be in ctrl_mode=2
    // TEACHING, and arm.move_joints in that state is silently dropped
    // by the firmware → arm doesn't move and the operator sees no
    // visible effect from the replay. Ask user to exit teach first.
    rpc_->call("piper.get_status", QJsonObject{},
        [this](QJsonObject status) {
            const int ctrl_mode    = status.value("ctrl_mode").toInt(1);
            const int teach_status = status.value("teach_status").toInt(0);
            if (ctrl_mode != 1 || teach_status != 0) {
                QString msg = QStringLiteral(
                    "机械臂当前不在 CAN_CTRL (ctrl_mode=%1, teach_status=%2).\n"
                    "回放会被固件静默丢弃 — 机械臂不会动.\n\n"
                    "请先扶住机械臂, 点 急停 → 自动恢复后 → 使能,\n"
                    "状态栏显示 CAN_CTRL 之后再点 回放.")
                    .arg(ctrl_mode).arg(teach_status);
                QMessageBox::warning(this, QStringLiteral("不能回放"), msg);
                appendLog(QStringLiteral("⚠ 取消回放: 机械臂不在 CAN_CTRL"));
                return;
            }
            startReplayConfirmed();
        });
}

// Split out so the pre-flight status check can call back into it.
void TeachWidget::startReplayConfirmed()
{
    if (QMessageBox::warning(this, QStringLiteral("回放确认"),
            QStringLiteral("即将回放 %1 个点到真实机械臂\n"
                           "确保工作空间无障碍 + 急停在手边")
              .arg(waypoints_.size()),
            QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }

    // Fetch the live joint angles + current speed before kicking off.
    // We need both for the dynamic-step-time calculator:
    //   - replay_prev_joints_ seeds the first step's delta (this is
    //     where the arm starts from)
    //   - replay_speed_pct_   sets the °/s estimate used by the timer
    //
    // arm.get_angles + piper.get_status are issued in sequence; replay
    // actually starts inside the second callback, so we know both are
    // populated by the time executeStep(0) fires.
    rpc_->call(Protocol::Methods::ARM_GET_ANGLES, QJsonObject{},
        [this](QJsonObject reply) {
            replay_prev_joints_.clear();
            const QJsonArray arr = reply.value(Protocol::Fields::ANGLES).toArray();
            for (const auto &v : arr) replay_prev_joints_.append(float(v.toDouble()));
            rpc_->call("piper.get_status", QJsonObject{},
                [this](QJsonObject st) {
                    replay_speed_pct_ = st.value("speed_pct").toInt(30);
                    if (replay_speed_pct_ < 5) replay_speed_pct_ = 5;
                    kickReplay();
                });
        });
}

// Estimate how long the arm needs to travel from `from` to `to` at the
// current speed. Used to set each step's effective duration so big
// joint moves don't get cut off by the next step's command.
//
// Empirical baseline: Piper at 30% speed_pct does ~18 °/s per joint.
// So 0.6 × speed_pct ≈ °/s for the slowest joint at default tune. The
// +500 ms buffer absorbs accel/decel ramps + CAN propagation jitter.
static int estimateStepMs(const QVector<float> &from,
                          const QVector<float> &to,
                          int speed_pct, int min_ms)
{
    float max_delta = 0.0f;
    const int n = std::min(from.size(), to.size());
    for (int i = 0; i < n; ++i) max_delta = std::max(max_delta, std::abs(to[i] - from[i]));
    const float deg_per_s = 0.6f * float(std::max(5, speed_pct));
    const int travel_ms = int(max_delta * 1000.0f / deg_per_s);
    return std::max(min_ms, travel_ms + 500);
}

void TeachWidget::kickReplay()
{
    btn_replay_->setEnabled(false);
    btn_stop_->setEnabled(true);
    replay_idx_ = 0;
    replay_step_dur_ms_ = step_ms_spin_->value();
    // First-step duration based on travel from current pose to wp[0]
    replay_effective_step_ms_ = estimateStepMs(
        replay_prev_joints_, waypoints_[0].joints,
        replay_speed_pct_, replay_step_dur_ms_);
    replay_start_ms_ = QDateTime::currentMSecsSinceEpoch();
    executeStep(0);
    appendLog(QStringLiteral("▶ 开始回放 (speed=%1%%, 第1步预计 %2ms)")
                  .arg(replay_speed_pct_).arg(replay_effective_step_ms_));
    replay_timer_->start();
}

void TeachWidget::onStopReplay()
{
    // User-pressed STOP button → halt motion immediately.
    finishReplay(/*send_arm_stop=*/true, QStringLiteral("⏸ 已停止"));
}

void TeachWidget::finishReplay(bool send_arm_stop, const QString &log_msg)
{
    if (replay_timer_) replay_timer_->stop();
    replay_idx_ = -1;
    btn_replay_->setEnabled(true);
    btn_stop_->setEnabled(false);
    // The CRITICAL distinction. Natural end-of-replay must NOT call
    // arm.stop — the last waypoint is the operator's intended pose,
    // and arm.stop would freeze the arm wherever it is mid-flight
    // (often visibly short of the target, especially on joints that
    // had to travel far inside the step_ms window). For natural end
    // we just stop scheduling and let the heartbeat keep pushing
    // target_joints (still set to the last waypoint) until the
    // motors actually arrive.
    if (send_arm_stop && rpc_ && rpc_->isConnected()) {
        rpc_->call(Protocol::Methods::ARM_STOP, QJsonObject{});
    }
    if (!log_msg.isEmpty()) appendLog(log_msg);
}

void TeachWidget::executeStep(int idx)
{
    if (idx < 0 || idx >= waypoints_.size()) return;
    const Waypoint &w = waypoints_[idx];

    // 1. arm.move_joints — with a callback so that when proc_piper
    // refuses (e.g. ctrl_mode!=CAN_CTRL because the arm is stuck in
    // TEACHING), the operator SEES the error in the log instead of
    // wondering why the arm isn't moving.
    QJsonObject p;
    QJsonArray j;
    for (float v : w.joints) j.append(v);
    p[Protocol::Fields::JOINTS] = j;
    const int step_num = idx + 1;
    rpc_->call(Protocol::Methods::ARM_MOVE_JOINTS, p,
        [this, step_num](QJsonObject reply) {
            if (!reply.value("ok").toBool(true)) {
                const QString err = reply.value("error").toString();
                appendLog(QStringLiteral("⚠ 第 %1 点 move_joints 拒绝: %2")
                              .arg(step_num).arg(err));
            }
        });

    // 2. gripper command (if state changed). proc_piper's
    // piper.set_gripper_angle expects "angle_mm" + "effort_mNm". The
    // stored gripper_angle is treated as mm (0=closed, ~70=fully open
    // on Piper hardware); gripper_force_pct (0-100) maps to mN·m via
    // ×20 (100% → 2000 mN·m, plenty for any real grasp).
    if (w.gripper_state != GripperUnchanged) {
        QJsonObject g;
        g["angle_mm"]   = double(w.gripper_angle);
        g["effort_mNm"] = double(w.gripper_force_pct) * 20.0;
        rpc_->call(Protocol::Methods::PIPER_SET_GRIPPER_ANGLE, g);
    }

    wp_list_->setCurrentRow(idx);
    appendLog(QStringLiteral("→ 执行第 %1 点  %2").arg(idx + 1).arg(w.label));
}

void TeachWidget::onReplayTick()
{
    if (replay_idx_ < 0 || replay_idx_ >= waypoints_.size()) {
        finishReplay(/*send_arm_stop=*/false, QString());
        return;
    }
    const Waypoint &w = waypoints_[replay_idx_];
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsed = now - replay_start_ms_;
    // Use the dynamically estimated duration (based on this step's max
    // joint delta) instead of a fixed step_ms — otherwise big joint
    // moves get cut short when the next step fires too early.
    const qint64 want    = replay_effective_step_ms_ + w.dwell_ms;
    if (elapsed >= want) {
        // The arm should now be at waypoints_[replay_idx_] — use that
        // as the "from" pose for the next step's duration estimate.
        replay_prev_joints_ = w.joints;
        ++replay_idx_;
        if (replay_idx_ >= waypoints_.size()) {
            // Natural end of replay — let the arm finish travelling to
            // the last commanded waypoint. heartbeat keeps pushing the
            // target. No arm.stop here, otherwise the arm freezes mid-
            // flight on joints that had to move a lot.
            finishReplay(/*send_arm_stop=*/false,
                         QStringLiteral("✓ 回放完成 (%1 点) — heartbeat 继续守住最后一点")
                             .arg(waypoints_.size()));
            return;
        }
        // Recompute step duration for the new step. A pair of continuously
        // captured points already knows how long the operator took to move
        // between them, and that is the correct answer — estimateStepMs
        // enforces the operator's minimum step time (1500 ms by default),
        // which would turn a 50 ms capture interval into a 30x slowdown.
        const Waypoint &prev_wp = waypoints_[replay_idx_ - 1];
        const Waypoint &next_wp = waypoints_[replay_idx_];
        if (prev_wp.t_ms >= 0 && next_wp.t_ms > prev_wp.t_ms) {
            replay_effective_step_ms_ = next_wp.t_ms - prev_wp.t_ms;
        } else {
            replay_effective_step_ms_ = estimateStepMs(
                replay_prev_joints_, next_wp.joints,
                replay_speed_pct_, replay_step_dur_ms_);
        }
        replay_start_ms_ = now;
        appendLog(QStringLiteral("→ 第 %1 点预计耗时 %2ms")
                      .arg(replay_idx_ + 1).arg(replay_effective_step_ms_));
        executeStep(replay_idx_);
    }
}

// ── Per-row edit dialog: gripper state + dwell + label ────────────
void TeachWidget::onRowDoubleClicked(int row)
{
    if (row < 0 || row >= waypoints_.size()) return;
    Waypoint &w = waypoints_[row];

    // Step 1: edit free-form note. The operator uses this to remember
    // what the waypoint is for ("电池上方 5cm 安全位", "末端朝下抓取
    // 准备", etc.) so the row in the list is meaningful days later.
    // Cancelling here aborts the whole edit so the operator doesn't get
    // stuck answering the gripper/dwell dialogs just to view the note.
    bool ok = false;
    const QString note = QInputDialog::getText(this,
        QStringLiteral("备注"),
        QStringLiteral("第 %1 点 — 备注 (用途/位置/注意事项):").arg(row + 1),
        QLineEdit::Normal, w.note, &ok);
    if (!ok) return;
    w.note = note;

    // Step 2: gripper state combo.
    QStringList items = {
        QStringLiteral("不变"),
        QStringLiteral("张开 (60°, 30% 力)"),
        QStringLiteral("闭合-轻 (5°, 30% 力)"),
        QStringLiteral("闭合-紧 (5°, 90% 力)"),
    };
    const QString chosen = QInputDialog::getItem(this,
        QStringLiteral("修改夹爪状态"),
        QStringLiteral("第 %1 点 — 夹爪动作:").arg(row + 1),
        items, int(w.gripper_state), false, &ok);
    if (!ok) {
        // Operator cancelled gripper edit but still wants the note saved.
        rebuildList();
        wp_list_->setCurrentRow(row);
        return;
    }
    const int idx = items.indexOf(chosen);
    w.gripper_state = GripperState(idx);
    switch (w.gripper_state) {
        case GripperOpen:      w.gripper_angle = 60; w.gripper_force_pct = 30; break;
        case GripperCloseSoft: w.gripper_angle =  5; w.gripper_force_pct = 30; break;
        case GripperCloseFirm: w.gripper_angle =  5; w.gripper_force_pct = 90; break;
        default: break;
    }

    // Step 3: dwell.
    const int dwell = QInputDialog::getInt(this,
        QStringLiteral("修改停留"),
        QStringLiteral("第 %1 点 — 到位后停留 (ms):").arg(row + 1),
        w.dwell_ms, 0, 10000, 100, &ok);
    if (ok) w.dwell_ms = dwell;

    rebuildList();
    wp_list_->setCurrentRow(row);
    appendLog(QStringLiteral("第 %1 点: %2, %3, dwell %4 ms")
                  .arg(row + 1)
                  .arg(w.note.isEmpty() ? QStringLiteral("(无备注)") : w.note)
                  .arg(gripperStateText(w.gripper_state))
                  .arg(w.dwell_ms));
}
