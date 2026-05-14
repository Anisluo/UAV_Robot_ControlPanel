#include "StageConfigDialog.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStackedWidget>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QMessageBox>
#include <QJsonArray>


// ════════════════════════════════════════════════════════════════════════
// Construction
// ════════════════════════════════════════════════════════════════════════
StageConfigDialog::StageConfigDialog(const QString &stage_id,
                                       const QString &stage_title,
                                       const QVector<TaskStep> &existing_steps,
                                       RpcClient *rpc,
                                       QWidget *parent)
    : QDialog(parent), stage_id_(stage_id), stage_title_(stage_title),
      rpc_(rpc), steps_(existing_steps)
{
    setWindowTitle(QStringLiteral("配置 stage: %1 (%2)").arg(stage_title, stage_id));
    resize(900, 540);
    setModal(true);
    buildUi();
    refreshList();
    if (!steps_.isEmpty()) {
        list_->setCurrentRow(0);
    } else {
        showRow(-1);
    }
}


// ════════════════════════════════════════════════════════════════════════
// UI assembly
// ════════════════════════════════════════════════════════════════════════
void StageConfigDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    // Header
    auto *header = new QLabel(QStringLiteral("<b>Stage:</b> %1  <span style='color:#888'>(%2)</span>")
                                   .arg(stage_title_, stage_id_), this);
    header->setStyleSheet("font-size:14px; padding:4px;");
    root->addWidget(header);

    auto *body = new QHBoxLayout;
    root->addLayout(body, /*stretch=*/1);

    // ── Left: list + add/del/move buttons ───────────────────────────
    auto *left = new QVBoxLayout;

    list_ = new QListWidget(this);
    list_->setMinimumWidth(380);
    list_->setStyleSheet("QListWidget::item { padding: 4px; }");
    connect(list_, &QListWidget::currentRowChanged,
            this, &StageConfigDialog::onRowChanged);
    left->addWidget(list_, /*stretch=*/1);

    auto *add_row = new QHBoxLayout;
    cmb_add_ = new QComboBox(this);
    for (int t = int(StepType::MOVE_JOINTS); t <= int(StepType::DWELL); ++t) {
        cmb_add_->addItem(TaskStep::typeLabel(StepType(t)), int(t));
    }
    btn_add_  = new QPushButton(QStringLiteral("+ 加步骤"), this);
    add_row->addWidget(cmb_add_);
    add_row->addWidget(btn_add_);
    left->addLayout(add_row);

    auto *btn_row = new QHBoxLayout;
    btn_del_  = new QPushButton(QStringLiteral("✗ 删"),   this);
    btn_up_   = new QPushButton(QStringLiteral("↑ 上移"), this);
    btn_dn_   = new QPushButton(QStringLiteral("↓ 下移"), this);
    btn_row->addWidget(btn_del_);
    btn_row->addWidget(btn_up_);
    btn_row->addWidget(btn_dn_);
    left->addLayout(btn_row);

    btn_record_ = new QPushButton(QStringLiteral("📍 录入当前关节为新点"), this);
    btn_record_->setToolTip(QStringLiteral("拖动机械臂到位置后点击, "
                                          "把当前 6 关节角度作为新 MOVE_JOINTS 步追加到列表末尾"));
    btn_record_->setStyleSheet("QPushButton{ background:#3a8; color:white; font-weight:bold; padding:6px;}"
                              "QPushButton:hover{ background:#4ab;}"
                              "QPushButton:disabled{ background:#446; color:#aab;}");
    btn_record_->setEnabled(rpc_ != nullptr);
    left->addWidget(btn_record_);

    connect(btn_add_,    &QPushButton::clicked, this, &StageConfigDialog::onAddStep);
    connect(btn_del_,    &QPushButton::clicked, this, &StageConfigDialog::onRemoveStep);
    connect(btn_up_,     &QPushButton::clicked, this, &StageConfigDialog::onMoveUp);
    connect(btn_dn_,     &QPushButton::clicked, this, &StageConfigDialog::onMoveDown);
    connect(btn_record_, &QPushButton::clicked, this, &StageConfigDialog::onRecordCurrentJoints);

    body->addLayout(left, /*stretch=*/2);

    // ── Right: stacked editor panels ────────────────────────────────
    auto *right = new QVBoxLayout;
    editor_title_ = new QLabel(QStringLiteral("<i>选中左侧某行查看参数</i>"), this);
    editor_title_->setStyleSheet("font-size:13px; padding:2px;");
    right->addWidget(editor_title_);

    // 备注 — free-form operator memo, always visible regardless of step
    // type. Stored in TaskStep::label (which the struct comment already
    // labels as an operator note). Edits flow through onParamChanged so
    // the list row updates live as the operator types.
    auto *note_row = new QHBoxLayout;
    auto *note_lbl = new QLabel(QStringLiteral("📝 备注:"), this);
    note_lbl->setStyleSheet("color:#aab;");
    step_note_edit_ = new QLineEdit(this);
    step_note_edit_->setPlaceholderText(
        QStringLiteral("这一步是干啥用的 (例如: 电池上方安全位 / 抓取准备 / 等待对中)"));
    note_row->addWidget(note_lbl);
    note_row->addWidget(step_note_edit_, /*stretch=*/1);
    right->addLayout(note_row);
    connect(step_note_edit_, &QLineEdit::textEdited,
            this, &StageConfigDialog::onParamChanged);

    editor_stack_ = new QStackedWidget(this);
    buildEditPanels();
    right->addWidget(editor_stack_, /*stretch=*/1);

    body->addLayout(right, /*stretch=*/3);

    // ── Bottom: OK / Cancel ─────────────────────────────────────────
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    btns->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    btns->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(btns);
}

void StageConfigDialog::buildEditPanels()
{
    // 0 MOVE_JOINTS
    {
        auto *w = new QWidget(this);
        auto *grid = new QGridLayout(w);
        grid->setContentsMargins(8, 8, 8, 8);
        const char* names[6] = {"J1", "J2", "J3", "J4", "J5", "J6"};
        constexpr double LIM[6][2] = {{-150, 150}, {0, 180}, {-170, 0},
                                       {-100, 100}, {-70, 70}, {-180, 180}};
        for (int i = 0; i < 6; ++i) {
            grid->addWidget(new QLabel(names[i], w), i, 0);
            mj_j_[i] = new QDoubleSpinBox(w);
            mj_j_[i]->setRange(LIM[i][0], LIM[i][1]);
            mj_j_[i]->setDecimals(2);
            mj_j_[i]->setSuffix("°");
            mj_j_[i]->setSingleStep(1.0);
            connect(mj_j_[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this, &StageConfigDialog::onParamChanged);
            grid->addWidget(mj_j_[i], i, 1);
            grid->addWidget(new QLabel(QString("[%1, %2]").arg(LIM[i][0]).arg(LIM[i][1]), w), i, 2);
        }
        grid->addWidget(new QLabel("速度 (%)", w), 6, 0);
        mj_speed_ = new QSpinBox(w);
        mj_speed_->setRange(1, 100);
        mj_speed_->setValue(30);
        connect(mj_speed_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &StageConfigDialog::onParamChanged);
        grid->addWidget(mj_speed_, 6, 1);
        grid->setRowStretch(7, 1);
        editor_stack_->insertWidget(int(StepType::MOVE_JOINTS), w);
    }

    // 1 MOVE_CARTESIAN
    {
        auto *w = new QWidget(this);
        auto *f = new QFormLayout(w);
        f->setContentsMargins(8, 8, 8, 8);
        auto mk = [&](double lo, double hi, const char *suf) {
            auto *s = new QDoubleSpinBox(w);
            s->setRange(lo, hi); s->setDecimals(2); s->setSuffix(suf); s->setSingleStep(1.0);
            connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this, &StageConfigDialog::onParamChanged);
            return s;
        };
        mc_x_  = mk(-1000, 1000, "mm");
        mc_y_  = mk(-1000, 1000, "mm");
        mc_z_  = mk(    0, 1500, "mm");
        mc_rx_ = mk(-180, 180, "°");
        mc_ry_ = mk(-180, 180, "°");
        mc_rz_ = mk(-180, 180, "°");
        f->addRow("X", mc_x_);
        f->addRow("Y", mc_y_);
        f->addRow("Z", mc_z_);
        f->addRow("RX", mc_rx_);
        f->addRow("RY", mc_ry_);
        f->addRow("RZ", mc_rz_);
        mc_mode_ = new QComboBox(w);
        mc_mode_->addItems({"P (点位)", "L (直线)"});
        connect(mc_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &StageConfigDialog::onParamChanged);
        f->addRow("模式", mc_mode_);
        editor_stack_->insertWidget(int(StepType::MOVE_CARTESIAN), w);
    }

    // 2 GRIPPER
    {
        auto *w = new QWidget(this);
        auto *f = new QFormLayout(w);
        f->setContentsMargins(8, 8, 8, 8);
        gr_angle_ = new QDoubleSpinBox(w);
        gr_angle_->setRange(0, 80); gr_angle_->setDecimals(1); gr_angle_->setSuffix("mm");
        gr_angle_->setSingleStep(5.0);
        gr_force_ = new QSpinBox(w);
        gr_force_->setRange(0, 100); gr_force_->setValue(30); gr_force_->setSuffix("%");
        connect(gr_angle_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &StageConfigDialog::onParamChanged);
        connect(gr_force_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &StageConfigDialog::onParamChanged);
        f->addRow("开度", gr_angle_);
        f->addRow("力度", gr_force_);
        f->addRow(new QLabel("<i>0 = 全闭, ~70 = 全开 (硬件极限)</i>", w));
        editor_stack_->insertWidget(int(StepType::GRIPPER), w);
    }

    // 3 AIRPORT_RAIL — lock/release picker, mirrors AirportWidget on the
    // dashboard. The motor runs until the backend stall monitor (in
    // proc_gateway) cuts it; max_ms is the GUI's hard upper bound.
    {
        auto *w = new QWidget(this);
        auto *f = new QFormLayout(w);
        f->setContentsMargins(8, 8, 8, 8);

        ar_action_ = new QComboBox(w);
        ar_action_->addItem(QStringLiteral("机场平台锁定 (导轨 1+3)"),  "lock");
        ar_action_->addItem(QStringLiteral("机场平台释放 (导轨 1+3)"),  "release");
        ar_action_->addItem(QStringLiteral("机场夹爪导轨 前进 (导轨 2)"), "rail2_fwd");
        ar_action_->addItem(QStringLiteral("机场夹爪导轨 后退 (导轨 2)"), "rail2_back");

        ar_stop_mode_ = new QComboBox(w);
        ar_stop_mode_->addItem(QStringLiteral("堵转停 (撞死自动停)"),  "stall");
        ar_stop_mode_->addItem(QStringLiteral("固定距离 (走 N mm)"),    "distance");

        ar_speed_ = new QSpinBox(w);
        ar_speed_->setRange(50, 3000);
        ar_speed_->setValue(1500);
        ar_speed_->setSuffix("rpm");

        ar_distance_ = new QDoubleSpinBox(w);
        ar_distance_->setRange(0.0, 2000.0);
        ar_distance_->setDecimals(1);
        ar_distance_->setSingleStep(1.0);
        ar_distance_->setValue(50.0);
        ar_distance_->setSuffix("mm");

        ar_max_ms_ = new QSpinBox(w);
        ar_max_ms_->setRange(1000, 60000);
        ar_max_ms_->setValue(7000);
        ar_max_ms_->setSingleStep(500);
        ar_max_ms_->setSuffix("ms");

        f->addRow(QStringLiteral("动作"), ar_action_);
        f->addRow(QStringLiteral("停止方式"), ar_stop_mode_);
        f->addRow(QStringLiteral("速度"), ar_speed_);
        f->addRow(QStringLiteral("距离"), ar_distance_);
        f->addRow(QStringLiteral("最长等待"), ar_max_ms_);
        f->addRow(new QLabel(
            QStringLiteral("<i>堵转停: 电机一直走, 检测到 status 0x04/0x08<br>"
                           "或读 CAN 失败 8 次自动停。<br>"
                           "固定距离: 走配置的 mm 数停, 用 pulse 计数;<br>"
                           "中途撞死也会停。最长等待是安全兜底。</i>"),
            w));

        // Distance row is only meaningful in distance mode — hide it
        // entirely when stop_mode = stall instead of greying out (cleaner
        // visual: operator only sees the fields that actually apply).
        // QFormLayout::setRowVisible(QWidget*, bool) needs Qt 6.4+; we're
        // on 6.11.
        auto refreshAirportRailFields = [this, f]() {
            const bool dist = (ar_stop_mode_->currentData().toString() == "distance");
            f->setRowVisible(ar_distance_, dist);
        };

        connect(ar_action_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &StageConfigDialog::onParamChanged);
        connect(ar_stop_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, refreshAirportRailFields]() {
            refreshAirportRailFields();
            onParamChanged();
        });
        connect(ar_speed_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &StageConfigDialog::onParamChanged);
        connect(ar_distance_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &StageConfigDialog::onParamChanged);
        connect(ar_max_ms_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &StageConfigDialog::onParamChanged);

        refreshAirportRailFields();
        editor_stack_->insertWidget(int(StepType::AIRPORT_RAIL), w);
    }

    // 4 AIRPORT_GRIPPER
    {
        auto *w = new QWidget(this);
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(8, 8, 8, 8);
        ag_open_ = new QCheckBox(QStringLiteral("张开 (取消勾选 = 闭合, 锁住 UAV)"), w);
        connect(ag_open_, &QCheckBox::toggled, this, &StageConfigDialog::onParamChanged);
        v->addWidget(ag_open_);
        v->addStretch(1);
        editor_stack_->insertWidget(int(StepType::AIRPORT_GRIPPER), w);
    }

    // 5 WAIT_DETECT_UAV
    {
        auto *w = new QWidget(this);
        auto *f = new QFormLayout(w);
        f->setContentsMargins(8, 8, 8, 8);
        wd_present_ = new QCheckBox(QStringLiteral("等待 UAV 出现 (取消 = 等到消失)"), w);
        wd_present_->setChecked(true);
        wd_timeout_ = new QSpinBox(w);
        wd_timeout_->setRange(100, 60000); wd_timeout_->setValue(10000); wd_timeout_->setSuffix("ms");
        wd_timeout_->setSingleStep(500);
        connect(wd_present_, &QCheckBox::toggled, this, &StageConfigDialog::onParamChanged);
        connect(wd_timeout_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &StageConfigDialog::onParamChanged);
        f->addRow(wd_present_);
        f->addRow(QStringLiteral("超时"), wd_timeout_);
        f->addRow(new QLabel(QStringLiteral("<i>每 200ms 轮询 npu.get_detections, 找 class=4 (mavic3_drone)</i>"), w));
        editor_stack_->insertWidget(int(StepType::WAIT_DETECT_UAV), w);
    }

    // 6 WAIT_DETECT_BAT
    {
        auto *w = new QWidget(this);
        auto *f = new QFormLayout(w);
        f->setContentsMargins(8, 8, 8, 8);
        wd_bat_present_ = new QCheckBox(QStringLiteral("等待电池 出现 (取消 = 等到消失)"), w);
        wd_bat_present_->setChecked(true);
        wd_bat_timeout_ = new QSpinBox(w);
        wd_bat_timeout_->setRange(100, 60000); wd_bat_timeout_->setValue(10000); wd_bat_timeout_->setSuffix("ms");
        wd_bat_timeout_->setSingleStep(500);
        connect(wd_bat_present_, &QCheckBox::toggled, this, &StageConfigDialog::onParamChanged);
        connect(wd_bat_timeout_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &StageConfigDialog::onParamChanged);
        f->addRow(wd_bat_present_);
        f->addRow(QStringLiteral("超时"), wd_bat_timeout_);
        f->addRow(new QLabel(QStringLiteral("<i>每 200ms 轮询 class=200 (battery_tracker)</i>"), w));
        editor_stack_->insertWidget(int(StepType::WAIT_DETECT_BAT), w);
    }

    // 7 DWELL
    {
        auto *w = new QWidget(this);
        auto *f = new QFormLayout(w);
        f->setContentsMargins(8, 8, 8, 8);
        dw_ms_ = new QSpinBox(w);
        dw_ms_->setRange(0, 60000); dw_ms_->setValue(1000); dw_ms_->setSuffix("ms");
        dw_ms_->setSingleStep(100);
        connect(dw_ms_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &StageConfigDialog::onParamChanged);
        f->addRow(QStringLiteral("时长"), dw_ms_);
        editor_stack_->insertWidget(int(StepType::DWELL), w);
    }
}


// ════════════════════════════════════════════════════════════════════════
// List management
// ════════════════════════════════════════════════════════════════════════
// Helper: format one row label-string. Includes 📝 备注 when present so
// the operator can identify what each step is for at a glance.
static QString stepRowText(int idx_0based, const TaskStep &s)
{
    QString text = QStringLiteral("%1. %2  —  %3")
        .arg(idx_0based + 1, 2, 10, QChar('0'))
        .arg(TaskStep::typeLabel(s.type))
        .arg(s.summary());
    if (!s.label.isEmpty()) {
        text += QStringLiteral("   📝 %1").arg(s.label);
    }
    return text;
}

void StageConfigDialog::refreshList()
{
    const int prev = list_->currentRow();
    list_->clear();
    for (int i = 0; i < steps_.size(); ++i) {
        auto *item = new QListWidgetItem(stepRowText(i, steps_[i]));
        list_->addItem(item);
    }
    if (prev >= 0 && prev < steps_.size()) {
        list_->setCurrentRow(prev);
    }
}

void StageConfigDialog::onAddStep()
{
    TaskStep s;
    s.type = StepType(cmb_add_->currentData().toInt());
    // sensible defaults
    switch (s.type) {
        case StepType::MOVE_JOINTS:
            // MOVE_JOINTS gets pre-populated with the LIVE arm pose
            // (via arm.get_angles RPC) so the new step represents
            // "current position" instead of all-zeros. Operators
            // were confused when they added a step, typed a note,
            // and the joints stayed at 0 — they expected the new
            // row to capture wherever the arm was at click time.
            // RPC unavailable → fall back to all-zeros so the dialog
            // still works offline.
            s.params["joints"]      = QVariantList{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
            s.params["speed_ratio"] = 0.30;
            steps_.append(s);
            refreshList();
            list_->setCurrentRow(steps_.size() - 1);
            // Now overwrite with live angles asynchronously
            if (rpc_ && rpc_->isConnected()) {
                const int row_added = steps_.size() - 1;
                rpc_->call(Protocol::Methods::ARM_GET_ANGLES, QJsonObject{},
                    [this, row_added](QJsonObject reply) {
                        const auto arr = reply.value(Protocol::Fields::ANGLES).toArray();
                        if (arr.size() != 6) return;
                        if (row_added < 0 || row_added >= steps_.size()) return;
                        QVariantList j; for (const auto &v : arr) j << v.toDouble();
                        steps_[row_added].params["joints"] = j;
                        // Refresh the row label AND, if it's still selected,
                        // re-populate the editor so the operator sees the
                        // live values rather than the stale zeros.
                        if (auto *item = list_->item(row_added)) {
                            item->setText(stepRowText(row_added, steps_[row_added]));
                        }
                        if (list_->currentRow() == row_added) {
                            showRow(row_added);
                        }
                    });
            }
            return;     // step + RPC handled above
        case StepType::MOVE_CARTESIAN:
            s.params["x_mm"] = 58.0; s.params["y_mm"] = 0.0;  s.params["z_mm"] = 213.0;
            s.params["rx_deg"] = 0.0; s.params["ry_deg"] = 85.0; s.params["rz_deg"] = 0.0;
            s.params["mode"] = "P";
            break;
        case StepType::GRIPPER:
            s.params["angle_mm"]  = 60.0;
            s.params["force_pct"] = 30;
            break;
        case StepType::AIRPORT_RAIL:
            s.params["action"]      = "lock";
            s.params["stop_mode"]   = "stall";
            s.params["speed_rpm"]   = 1500;
            s.params["distance_mm"] = 50.0;
            s.params["max_ms"]      = 7000;
            break;
        case StepType::AIRPORT_GRIPPER:
            s.params["open"] = true;
            break;
        case StepType::WAIT_DETECT_UAV:
        case StepType::WAIT_DETECT_BAT:
            s.params["present"]    = true;
            s.params["timeout_ms"] = 10000;
            break;
        case StepType::DWELL:
            s.params["ms"] = 1000;
            break;
    }
    steps_.append(s);
    refreshList();
    list_->setCurrentRow(steps_.size() - 1);
}

void StageConfigDialog::onRemoveStep()
{
    const int r = list_->currentRow();
    if (r < 0 || r >= steps_.size()) return;
    steps_.removeAt(r);
    refreshList();
    if (!steps_.isEmpty()) {
        list_->setCurrentRow(qMin(r, steps_.size() - 1));
    } else {
        showRow(-1);
    }
}

void StageConfigDialog::onMoveUp()
{
    const int r = list_->currentRow();
    if (r <= 0) return;
    steps_.swapItemsAt(r, r - 1);
    refreshList();
    list_->setCurrentRow(r - 1);
}

void StageConfigDialog::onMoveDown()
{
    const int r = list_->currentRow();
    if (r < 0 || r >= steps_.size() - 1) return;
    steps_.swapItemsAt(r, r + 1);
    refreshList();
    list_->setCurrentRow(r + 1);
}

void StageConfigDialog::onRowChanged(int row)
{
    cur_row_ = row;
    showRow(row);
}

void StageConfigDialog::showRow(int row)
{
    const bool valid = (row >= 0 && row < steps_.size());
    btn_del_->setEnabled(valid);
    btn_up_->setEnabled(valid && row > 0);
    btn_dn_->setEnabled(valid && row < steps_.size() - 1);
    editor_stack_->setEnabled(valid);

    if (!valid) {
        editor_title_->setText(QStringLiteral("<i>选中左侧某行查看参数</i>"));
        editor_stack_->setCurrentIndex(0);
        if (step_note_edit_) {
            const QSignalBlocker block(step_note_edit_);
            step_note_edit_->clear();
            step_note_edit_->setEnabled(false);
        }
        return;
    }

    const TaskStep &s = steps_[row];
    editor_title_->setText(
        QStringLiteral("<b>步骤 %1</b> · %2").arg(row + 1).arg(TaskStep::typeLabel(s.type)));
    editor_stack_->setCurrentIndex(int(s.type));
    if (step_note_edit_) {
        const QSignalBlocker block(step_note_edit_);
        step_note_edit_->setEnabled(true);
        step_note_edit_->setText(s.label);
    }

    // Block signals while populating to avoid round-tripping into onParamChanged
    const QList<QWidget*> editors = {
        editor_stack_->currentWidget()
    };
    for (auto *e : editors) e->blockSignals(true);

    switch (s.type) {
        case StepType::MOVE_JOINTS: {
            const auto j = s.params.value("joints").toList();
            for (int i = 0; i < 6; ++i)
                if (j.size() > i) mj_j_[i]->setValue(j[i].toDouble());
            mj_speed_->setValue(int(s.params.value("speed_ratio", 0.3).toDouble() * 100));
            break;
        }
        case StepType::MOVE_CARTESIAN:
            mc_x_->setValue(s.params.value("x_mm").toDouble());
            mc_y_->setValue(s.params.value("y_mm").toDouble());
            mc_z_->setValue(s.params.value("z_mm").toDouble());
            mc_rx_->setValue(s.params.value("rx_deg").toDouble());
            mc_ry_->setValue(s.params.value("ry_deg").toDouble());
            mc_rz_->setValue(s.params.value("rz_deg").toDouble());
            mc_mode_->setCurrentIndex(s.params.value("mode", "P").toString() == "L" ? 1 : 0);
            break;
        case StepType::GRIPPER:
            gr_angle_->setValue(s.params.value("angle_mm").toDouble());
            gr_force_->setValue(s.params.value("force_pct").toInt());
            break;
        case StepType::AIRPORT_RAIL: {
            const QString action = s.params.value("action", "lock").toString();
            int aidx = ar_action_->findData(action);
            if (aidx < 0) aidx = 0;
            ar_action_->setCurrentIndex(aidx);

            const QString stop_mode = s.params.value("stop_mode", "stall").toString();
            int sidx = ar_stop_mode_->findData(stop_mode);
            if (sidx < 0) sidx = 0;
            ar_stop_mode_->setCurrentIndex(sidx);

            ar_speed_->setValue(s.params.value("speed_rpm", 1500).toInt());
            ar_distance_->setValue(s.params.value("distance_mm", 50.0).toDouble());
            ar_max_ms_->setValue(s.params.value("max_ms", 7000).toInt());

            // Re-apply row visibility after setCurrentIndex (signal blocked
            // during showRow so the lambda above didn't fire).
            if (auto *form = qobject_cast<QFormLayout*>(ar_distance_->parentWidget()->layout())) {
                form->setRowVisible(ar_distance_, stop_mode == "distance");
            }
            break;
        }
        case StepType::AIRPORT_GRIPPER:
            ag_open_->setChecked(s.params.value("open").toBool());
            break;
        case StepType::WAIT_DETECT_UAV:
            wd_present_->setChecked(s.params.value("present", true).toBool());
            wd_timeout_->setValue(s.params.value("timeout_ms", 10000).toInt());
            break;
        case StepType::WAIT_DETECT_BAT:
            wd_bat_present_->setChecked(s.params.value("present", true).toBool());
            wd_bat_timeout_->setValue(s.params.value("timeout_ms", 10000).toInt());
            break;
        case StepType::DWELL:
            dw_ms_->setValue(s.params.value("ms", 1000).toInt());
            break;
    }

    for (auto *e : editors) e->blockSignals(false);
}

void StageConfigDialog::readEditorsTo(TaskStep &s)
{
    // Note is shared across all step types.
    if (step_note_edit_) {
        s.label = step_note_edit_->text();
    }
    switch (s.type) {
        case StepType::MOVE_JOINTS: {
            QVariantList j; for (int i = 0; i < 6; ++i) j << mj_j_[i]->value();
            s.params["joints"]      = j;
            s.params["speed_ratio"] = mj_speed_->value() / 100.0;
            break;
        }
        case StepType::MOVE_CARTESIAN:
            s.params["x_mm"]  = mc_x_->value();
            s.params["y_mm"]  = mc_y_->value();
            s.params["z_mm"]  = mc_z_->value();
            s.params["rx_deg"]= mc_rx_->value();
            s.params["ry_deg"]= mc_ry_->value();
            s.params["rz_deg"]= mc_rz_->value();
            s.params["mode"]  = (mc_mode_->currentIndex() == 1 ? "L" : "P");
            break;
        case StepType::GRIPPER:
            s.params["angle_mm"]  = gr_angle_->value();
            s.params["force_pct"] = gr_force_->value();
            break;
        case StepType::AIRPORT_RAIL:
            s.params["action"]      = ar_action_->currentData().toString();
            s.params["stop_mode"]   = ar_stop_mode_->currentData().toString();
            s.params["speed_rpm"]   = ar_speed_->value();
            s.params["distance_mm"] = ar_distance_->value();
            s.params["max_ms"]      = ar_max_ms_->value();
            break;
        case StepType::AIRPORT_GRIPPER:
            s.params["open"] = ag_open_->isChecked();
            break;
        case StepType::WAIT_DETECT_UAV:
            s.params["present"]    = wd_present_->isChecked();
            s.params["timeout_ms"] = wd_timeout_->value();
            break;
        case StepType::WAIT_DETECT_BAT:
            s.params["present"]    = wd_bat_present_->isChecked();
            s.params["timeout_ms"] = wd_bat_timeout_->value();
            break;
        case StepType::DWELL:
            s.params["ms"] = dw_ms_->value();
            break;
    }
}

void StageConfigDialog::onParamChanged()
{
    if (cur_row_ < 0 || cur_row_ >= steps_.size()) return;
    readEditorsTo(steps_[cur_row_]);
    // Refresh just the current row text without reloading everything (avoids
    // wiping the focus from the active editor).
    auto *item = list_->item(cur_row_);
    if (item) {
        item->setText(stepRowText(cur_row_, steps_[cur_row_]));
    }
}


// ════════════════════════════════════════════════════════════════════════
// Record current joints (calls arm.get_angles over RPC)
// ════════════════════════════════════════════════════════════════════════
void StageConfigDialog::onRecordCurrentJoints()
{
    if (!rpc_ || !rpc_->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("录点失败"),
                              QStringLiteral("RPC 未连接, 请先连接到 RK3588"));
        return;
    }
    rpc_->call(Protocol::Methods::ARM_GET_ANGLES, QJsonObject{},
        [this](QJsonObject reply) {
            const auto arr = reply.value(Protocol::Fields::ANGLES).toArray();
            if (arr.size() != 6) {
                QMessageBox::warning(this, QStringLiteral("录点失败"),
                                      QStringLiteral("arm.get_angles 返回非 6 元素"));
                return;
            }
            TaskStep s;
            s.type = StepType::MOVE_JOINTS;
            QVariantList j;
            for (const auto &v : arr) j << v.toDouble();
            s.params["joints"]      = j;
            s.params["speed_ratio"] = 0.30;
            steps_.append(s);
            refreshList();
            list_->setCurrentRow(steps_.size() - 1);
        });
}
