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
#include <QAbstractSpinBox>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>
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
    for (int t = int(StepType::MOVE_JOINTS); t <= int(StepType::FIX_POINT); ++t) {
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

    btn_record_ = new QPushButton(QStringLiteral("📍 录入当前关节为当前节点"), this);
    btn_record_->setToolTip(QStringLiteral("先在左边选中要修改的 \"机械臂关节\" 步骤, "
                                          "拖动机械臂到位置后点击, "
                                          "把当前 6 关节角度覆盖到选中的那一步上"));
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

    // ── Bottom: 保存 / 取消 + 执行 (all on the right) ──────────────
    auto *bottom_row = new QHBoxLayout;
    bottom_row->addStretch(1);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    btns->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    btns->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    // CRITICAL: before accepting we have to commit any pending spinbox /
    // line-edit. Qt's QSpinBox::value() returns the last *committed* value,
    // NOT what's currently displayed if the user typed but didn't tab/Enter.
    // Without this, "change force_pct 30 → 50, click 保存" saved 30 because
    // the in-progress edit hadn't been interpreted yet.
    connect(btns, &QDialogButtonBox::accepted, this, [this]() {
        // 1. Force the active edit-in-progress widget to commit.
        if (auto *fw = QApplication::focusWidget()) {
            if (auto *sb = qobject_cast<QAbstractSpinBox*>(fw)) sb->interpretText();
            fw->clearFocus();   // triggers editingFinished on every editor
        }
        // 2. Flush editor fields → steps_[cur_row_] one more time after
        //    any deferred valueChanged signals fire.
        if (cur_row_ >= 0 && cur_row_ < steps_.size()) {
            readEditorsTo(steps_[cur_row_]);
        }
        accept();
    });
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    bottom_row->addWidget(btns);

    btn_execute_ = new QPushButton(QStringLiteral("▶ 执行"), this);
    btn_execute_->setToolTip(QStringLiteral("用当前编辑器里的参数, 立即在真机上执行选中的步骤"));
    btn_execute_->setStyleSheet(
        "QPushButton{ background:#3a8; color:white; font-weight:bold;"
        " padding:6px 18px; border-radius:4px; }"
        "QPushButton:hover{ background:#4ba; }"
        "QPushButton:disabled{ background:#446; color:#aab; }");
    btn_execute_->setEnabled(false);
    connect(btn_execute_, &QPushButton::clicked,
            this, &StageConfigDialog::onExecuteCurrentStep);
    bottom_row->addWidget(btn_execute_);

    root->addLayout(bottom_row);
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

        // 🎯 定点测试 — 实时打个示范动作: TCP 锁在配置的 (X,Y,Z), 但把
        // RY 周期性扫 ±15° 来回, 关节角度跟着重新分配. 操作员能直观看到
        // "笛卡尔点位锁住, 关节自己换姿态" 的效果. 不影响录制的步骤,
        // 跟保存按钮无关.
        auto *btn_fix = new QPushButton(QStringLiteral("🎯 定点测试 (TCP 锁住, 摆姿态)"), w);
        btn_fix->setStyleSheet(
            "QPushButton{ background:#357ec7; color:white; font-weight:bold;"
            " padding:4px 10px; border-radius:4px; }"
            "QPushButton:hover{ background:#4689d8; }"
            "QPushButton:disabled{ background:#446; color:#aab; }");
        connect(btn_fix, &QPushButton::clicked, this, [this, btn_fix]() {
            if (!rpc_ || !rpc_->isConnected()) {
                QMessageBox::warning(this, "RPC", "未连接到 RK3588, 无法测试");
                return;
            }
            const double x  = mc_x_->value();
            const double y  = mc_y_->value();
            const double z  = mc_z_->value();
            const double rx = mc_rx_->value();
            const double ry0 = mc_ry_->value();
            const double rz = mc_rz_->value();
            // RY 来回扫 +15 / -15, 大约 5 秒走完一圈
            const QVector<double> ry_seq = { ry0 + 15.0, ry0 - 15.0,
                                              ry0 + 15.0, ry0 };
            constexpr int kStepMs = 1200;   // 单段允许时长
            btn_fix->setEnabled(false);
            for (int i = 0; i < ry_seq.size(); ++i) {
                QTimer::singleShot(i * kStepMs, this,
                    [this, x, y, z, rx, ry = ry_seq[i], rz]() {
                        if (!rpc_) return;
                        // piper.move_cartesian needs UPPERCASE X_mm/Y_mm/Z_mm
                        // + RX_deg/RY_deg/RZ_deg (see m_piper_move_cartesian
                        // signature in proc_piper.py).
                        QJsonObject p;
                        p["X_mm"]   = x;  p["Y_mm"]   = y;  p["Z_mm"]   = z;
                        p["RX_deg"] = rx; p["RY_deg"] = ry; p["RZ_deg"] = rz;
                        p["mode"]   = "P";
                        rpc_->call(QStringLiteral("piper.move_cartesian"), p);
                    });
            }
            // 跑完后恢复按钮
            QTimer::singleShot(ry_seq.size() * kStepMs + 500, btn_fix,
                [btn_fix]() { btn_fix->setEnabled(true); });
        });
        f->addRow(btn_fix);
        f->addRow(new QLabel(
            QStringLiteral("<i>定点测试: 点击后臂会在 RY ±15° 之间来回扫 4 次,"
                           "TCP (X,Y,Z) 始终锁在配置位置. 看关节角度变化 + TCP"
                           "在同一点就说明 OK. 大约 5 秒.</i>"),
            w));
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

    // 8 FIX_POINT (定点跟踪 — arm.move_cartesian to a target then hold for ms)
    {
        auto *w = new QWidget(this);
        auto *f = new QFormLayout(w);
        f->setContentsMargins(8, 8, 8, 8);

        auto makePosSpin = [w](double range, int dec, const QString &suffix,
                               double defv) {
            auto *sp = new QDoubleSpinBox(w);
            sp->setRange(-range, range);
            sp->setDecimals(dec);
            sp->setSuffix(suffix);
            sp->setValue(defv);
            sp->setSingleStep(1.0);
            return sp;
        };
        fp_x_  = makePosSpin(1500.0, 1, "mm",  0.0);
        fp_y_  = makePosSpin(1500.0, 1, "mm",  0.0);
        fp_z_  = makePosSpin(1500.0, 1, "mm", 200.0);
        fp_rx_ = makePosSpin(180.0,  1, "°",   0.0);
        fp_ry_ = makePosSpin(180.0,  1, "°",  85.0);
        fp_rz_ = makePosSpin(180.0,  1, "°",   0.0);

        fp_duration_ms_ = new QSpinBox(w);
        fp_duration_ms_->setRange(100, 600000);
        fp_duration_ms_->setValue(5000);
        fp_duration_ms_->setSingleStep(500);
        fp_duration_ms_->setSuffix("ms");

        btn_fp_record_ = new QPushButton(QStringLiteral("📍 录当前 TCP 为目标点"), w);
        btn_fp_record_->setStyleSheet(
            "QPushButton{ background:#3a8; color:white; font-weight:bold;"
            " padding:4px 10px; border-radius:4px; }"
            "QPushButton:hover{ background:#4ba; }");

        auto bindD = [this](QDoubleSpinBox *sp) {
            connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this, &StageConfigDialog::onParamChanged);
        };
        bindD(fp_x_); bindD(fp_y_); bindD(fp_z_);
        bindD(fp_rx_); bindD(fp_ry_); bindD(fp_rz_);
        connect(fp_duration_ms_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &StageConfigDialog::onParamChanged);

        // Record-TCP button: pull live pose via arm.get_pose, fill the
        // 6 spinboxes. Mirrors the 录当前关节 helper for MOVE_JOINTS.
        connect(btn_fp_record_, &QPushButton::clicked, this, [this]() {
            if (!rpc_) return;
            rpc_->call(QStringLiteral("arm.get_pose"), QJsonObject{},
                [this](QJsonObject reply) {
                    // proc_piper returns either {pose:[x,y,z,rx,ry,rz]} (mm,deg)
                    // or the legacy {x_mm, y_mm, z_mm, rx_deg, ry_deg, rz_deg}.
                    // Accept both.
                    QVector<double> v(6, 0.0);
                    const QJsonArray arr = reply.value("pose").toArray();
                    if (arr.size() == 6) {
                        for (int i = 0; i < 6; ++i) v[i] = arr[i].toDouble();
                    } else {
                        v[0] = reply.value("x_mm").toDouble();
                        v[1] = reply.value("y_mm").toDouble();
                        v[2] = reply.value("z_mm").toDouble();
                        v[3] = reply.value("rx_deg").toDouble();
                        v[4] = reply.value("ry_deg").toDouble();
                        v[5] = reply.value("rz_deg").toDouble();
                    }
                    const QSignalBlocker b0(fp_x_), b1(fp_y_), b2(fp_z_);
                    const QSignalBlocker b3(fp_rx_), b4(fp_ry_), b5(fp_rz_);
                    fp_x_->setValue(v[0]); fp_y_->setValue(v[1]); fp_z_->setValue(v[2]);
                    fp_rx_->setValue(v[3]); fp_ry_->setValue(v[4]); fp_rz_->setValue(v[5]);
                    onParamChanged();
                });
        });

        auto *posRow = new QHBoxLayout;
        posRow->addWidget(new QLabel("X")); posRow->addWidget(fp_x_, 1);
        posRow->addWidget(new QLabel("Y")); posRow->addWidget(fp_y_, 1);
        posRow->addWidget(new QLabel("Z")); posRow->addWidget(fp_z_, 1);
        auto *rotRow = new QHBoxLayout;
        rotRow->addWidget(new QLabel("RX")); rotRow->addWidget(fp_rx_, 1);
        rotRow->addWidget(new QLabel("RY")); rotRow->addWidget(fp_ry_, 1);
        rotRow->addWidget(new QLabel("RZ")); rotRow->addWidget(fp_rz_, 1);

        f->addRow(QStringLiteral("位置"), posRow);
        f->addRow(QStringLiteral("姿态"), rotRow);
        f->addRow(QStringLiteral("时长"), fp_duration_ms_);
        f->addRow(btn_fp_record_);
        f->addRow(new QLabel(
            QStringLiteral("<i>arm.move_cartesian 到目标位姿, 保持时长内 TCP "
                           "钉在该点; Piper 控制器自己平衡关节, 不再回报新的目标</i>"),
            w));

        editor_stack_->insertWidget(int(StepType::FIX_POINT), w);
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
        case StepType::FIX_POINT:
            s.params["x_mm"]       = 0.0;
            s.params["y_mm"]       = 0.0;
            s.params["z_mm"]       = 200.0;
            s.params["rx_deg"]     = 0.0;
            s.params["ry_deg"]     = 85.0;
            s.params["rz_deg"]     = 0.0;
            s.params["duration_ms"] = 5000;
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
    if (btn_execute_) btn_execute_->setEnabled(valid);

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
            // CRITICAL: per-widget signal block. The parent-widget
            // blockSignals(true) in the outer loop doesn't propagate to
            // children, so each setCurrentIndex/setValue below would
            // otherwise fire valueChanged → onParamChanged → readEditorsTo,
            // which writes the *current* editor state to steps_[cur_row_]
            // — including stale values from a previously-viewed step. End
            // result: opening a step occasionally clobbers its saved
            // stop_mode with whatever the previously-visible step had.
            const QSignalBlocker b_action  (ar_action_);
            const QSignalBlocker b_mode    (ar_stop_mode_);
            const QSignalBlocker b_speed   (ar_speed_);
            const QSignalBlocker b_dist    (ar_distance_);
            const QSignalBlocker b_max_ms  (ar_max_ms_);

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

            // Re-apply row visibility (the lambda hooked to ar_stop_mode_'s
            // currentIndexChanged is blocked by the QSignalBlocker above).
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
        case StepType::FIX_POINT: {
            const QSignalBlocker b0(fp_x_), b1(fp_y_), b2(fp_z_);
            const QSignalBlocker b3(fp_rx_), b4(fp_ry_), b5(fp_rz_);
            const QSignalBlocker b6(fp_duration_ms_);
            fp_x_->setValue(s.params.value("x_mm").toDouble());
            fp_y_->setValue(s.params.value("y_mm").toDouble());
            fp_z_->setValue(s.params.value("z_mm", 200.0).toDouble());
            fp_rx_->setValue(s.params.value("rx_deg").toDouble());
            fp_ry_->setValue(s.params.value("ry_deg", 85.0).toDouble());
            fp_rz_->setValue(s.params.value("rz_deg").toDouble());
            fp_duration_ms_->setValue(s.params.value("duration_ms", 5000).toInt());
            break;
        }
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
        case StepType::FIX_POINT:
            s.params["x_mm"]        = fp_x_->value();
            s.params["y_mm"]        = fp_y_->value();
            s.params["z_mm"]        = fp_z_->value();
            s.params["rx_deg"]      = fp_rx_->value();
            s.params["ry_deg"]      = fp_ry_->value();
            s.params["rz_deg"]      = fp_rz_->value();
            s.params["duration_ms"] = fp_duration_ms_->value();
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

// ▶ 执行: fire-and-forget the RPC for the currently-selected step using
// the live editor values. Same RPC the script orchestrator would issue,
// minus the advance-timer / status-poll plumbing — this is just a
// "try this one step on the real arm" preview.
void StageConfigDialog::onExecuteCurrentStep()
{
    if (cur_row_ < 0 || cur_row_ >= steps_.size()) return;
    if (!rpc_ || !rpc_->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("RPC"),
            QStringLiteral("未连接到 RK3588, 无法执行"));
        return;
    }
    // Make sure latest editor values are flushed into steps_[cur_row_]
    // (normally happens via valueChanged, but harmless to re-do).
    readEditorsTo(steps_[cur_row_]);
    const TaskStep &s = steps_[cur_row_];

    auto nullCb = [](QJsonObject) {};
    btn_execute_->setEnabled(false);
    QTimer::singleShot(3000, btn_execute_,
        [this]() { if (btn_execute_) btn_execute_->setEnabled(cur_row_ >= 0); });

    switch (s.type) {
        case StepType::MOVE_JOINTS: {
            QJsonArray j;
            for (const QVariant &v : s.params.value("joints").toList()) j.append(v.toDouble());
            QJsonObject p;
            p["joints"]      = j;
            p["speed_ratio"] = s.params.value("speed_ratio", 0.3).toDouble();
            rpc_->call(QStringLiteral("arm.move_joints"), p, nullCb);
            break;
        }
        case StepType::MOVE_CARTESIAN: {
            QJsonObject p;
            p["X_mm"]   = s.params.value("x_mm").toDouble();
            p["Y_mm"]   = s.params.value("y_mm").toDouble();
            p["Z_mm"]   = s.params.value("z_mm").toDouble();
            p["RX_deg"] = s.params.value("rx_deg").toDouble();
            p["RY_deg"] = s.params.value("ry_deg").toDouble();
            p["RZ_deg"] = s.params.value("rz_deg").toDouble();
            p["mode"]   = s.params.value("mode", "P").toString();
            rpc_->call(QStringLiteral("piper.move_cartesian"), p, nullCb);
            break;
        }
        case StepType::GRIPPER: {
            QJsonObject p;
            p["angle_mm"]   = s.params.value("angle_mm", 60.0).toDouble();
            p["effort_mNm"] = s.params.value("force_pct", 30).toInt() * 20.0; // 0..100% → 0..2000
            rpc_->call(QStringLiteral("piper.set_gripper_angle"), p, nullCb);
            break;
        }
        case StepType::AIRPORT_RAIL: {
            // STOP rail then 300 ms later fire the motion command.
            // Matches what dispatchScriptStep does, minus the stall-poll
            // advance machinery.
            const QString action = s.params.value("action", "lock").toString();
            const QString stop_mode = s.params.value("stop_mode", "stall").toString();
            const int rpm = s.params.value("speed_rpm", 1500).toInt();
            const double dist = s.params.value("distance_mm", 50.0).toDouble();
            QString stop_method = "airport.stop_all";
            QJsonObject stop_p;
            int direction = +1;
            QJsonArray watched_rails;
            if (action == "release") { direction = -1; watched_rails = {0,2}; }
            else if (action == "rail2_fwd") { direction = +1; watched_rails = {1};
                stop_method = "airport.stop"; stop_p["rail"] = 1; }
            else if (action == "rail2_back") { direction = -1; watched_rails = {1};
                stop_method = "airport.stop"; stop_p["rail"] = 1; }
            else { direction = +1; watched_rails = {0,2}; }
            rpc_->call(stop_method, stop_p, nullCb);
            QTimer::singleShot(300, this,
                [this, action, stop_mode, rpm, dist, direction, watched_rails]() {
                    if (!rpc_) return;
                    if (stop_mode == "distance") {
                        // GUI 计时方式 (matches Tab4 dispatch)
                        const int time_ms = std::max(100,
                            int(std::abs(dist) * 300000.0 / double(std::abs(rpm))));
                        for (const QJsonValue &rv : watched_rails) {
                            QJsonObject p;
                            p["rail"]      = rv.toInt();
                            p["speed_rpm"] = std::abs(rpm) * (direction >= 0 ? +1 : -1);
                            rpc_->call("airport.set_speed", p, [](QJsonObject){});
                        }
                        const QJsonArray rails_copy = watched_rails;
                        QTimer::singleShot(time_ms, this, [this, rails_copy]() {
                            if (!rpc_) return;
                            for (const QJsonValue &rv : rails_copy) {
                                QJsonObject sp; sp["rail"] = rv.toInt();
                                rpc_->call("airport.stop", sp, [](QJsonObject){});
                            }
                        });
                    } else {
                        // stall mode: lock/release/set_speed → backend monitor 自动停
                        QJsonObject p;
                        p["speed_rpm"] = rpm;
                        if (action == "release") {
                            rpc_->call("airport.release", p, [](QJsonObject){});
                        } else if (action == "rail2_fwd" || action == "rail2_back") {
                            p["rail"] = 1;
                            p["speed_rpm"] = (action == "rail2_fwd") ? rpm : -rpm;
                            rpc_->call("airport.set_speed", p, [](QJsonObject){});
                        } else {
                            rpc_->call("airport.lock", p, [](QJsonObject){});
                        }
                    }
                });
            break;
        }
        case StepType::AIRPORT_GRIPPER: {
            QJsonObject p;
            p["open"] = s.params.value("open").toBool();
            rpc_->call(QStringLiteral("airport.gripper"), p, nullCb);
            break;
        }
        case StepType::WAIT_DETECT_UAV:
        case StepType::WAIT_DETECT_BAT:
            QMessageBox::information(this, QStringLiteral("等待检测"),
                QStringLiteral("此类步骤在脚本执行时轮询检测状态, 单独执行无意义"));
            break;
        case StepType::DWELL: {
            const int ms = s.params.value("ms", 1000).toInt();
            QMessageBox::information(this, QStringLiteral("DWELL"),
                QStringLiteral("DWELL 是纯延时 %1ms, 单独执行只是干等").arg(ms));
            break;
        }
        case StepType::FIX_POINT: {
            QJsonObject p;
            p["X_mm"]   = s.params.value("x_mm").toDouble();
            p["Y_mm"]   = s.params.value("y_mm").toDouble();
            p["Z_mm"]   = s.params.value("z_mm").toDouble();
            p["RX_deg"] = s.params.value("rx_deg").toDouble();
            p["RY_deg"] = s.params.value("ry_deg").toDouble();
            p["RZ_deg"] = s.params.value("rz_deg").toDouble();
            p["mode"]   = "P";
            rpc_->call(QStringLiteral("piper.move_cartesian"), p, nullCb);
            break;
        }
    }
}


// ════════════════════════════════════════════════════════════════════════
// Record current joints (calls arm.get_angles over RPC)
// ════════════════════════════════════════════════════════════════════════
// "录入当前关节为当前节点" — overwrites the currently selected MOVE_JOINTS
// step's joint values with the live arm pose. Used to fine-tune a recorded
// waypoint: select the row, drag the arm to the desired physical position,
// click this button, and the row updates in-place. Refuses if no row is
// selected, or the selected row isn't a MOVE_JOINTS step.
void StageConfigDialog::onRecordCurrentJoints()
{
    if (!rpc_ || !rpc_->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("录点失败"),
                              QStringLiteral("RPC 未连接, 请先连接到 RK3588"));
        return;
    }
    if (cur_row_ < 0 || cur_row_ >= steps_.size()) {
        QMessageBox::warning(this, QStringLiteral("录点失败"),
                              QStringLiteral("请先在左侧选中一条机械臂关节步骤"));
        return;
    }
    if (steps_[cur_row_].type != StepType::MOVE_JOINTS) {
        QMessageBox::warning(this, QStringLiteral("录点失败"),
            QStringLiteral("当前选中的不是 \"机械臂关节\" 步骤, 改不了"));
        return;
    }
    const int target_row = cur_row_;
    rpc_->call(Protocol::Methods::ARM_GET_ANGLES, QJsonObject{},
        [this, target_row](QJsonObject reply) {
            // Bail if user changed selection during the round-trip
            if (target_row < 0 || target_row >= steps_.size()) return;
            if (steps_[target_row].type != StepType::MOVE_JOINTS) return;
            const auto arr = reply.value(Protocol::Fields::ANGLES).toArray();
            if (arr.size() != 6) {
                QMessageBox::warning(this, QStringLiteral("录点失败"),
                    QStringLiteral("arm.get_angles 返回非 6 元素"));
                return;
            }
            QVariantList j;
            for (const auto &v : arr) j << v.toDouble();
            steps_[target_row].params["joints"] = j;
            // speed_ratio 不动 — 操作员事先设的速度保留
            refreshList();
            list_->setCurrentRow(target_row);
            showRow(target_row);   // refresh the editor spinboxes from new data
        });
}
