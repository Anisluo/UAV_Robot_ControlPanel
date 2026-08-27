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
#include <QTime>
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
    for (int t = int(StepType::MOVE_JOINTS); t <= int(StepType::ARM_TRAJECTORY); ++t) {
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

    // 自定义 保存 / 取消, 不用 QDialogButtonBox::Save 是因为后者点击
    // 后会自动 accept() 关掉对话框. 我们要的是: 保存 → 持久化但保留
    // 对话框 (操作员可以接着改 / 验证). 取消 → 关掉, 弃改.
    auto *btn_save  = new QPushButton(QStringLiteral("保存"), this);
    auto *btn_close = new QPushButton(QStringLiteral("取消"), this);
    btn_save->setStyleSheet(
        "QPushButton{ background:#3a8; color:white; font-weight:bold;"
        " padding:6px 18px; border-radius:4px; }"
        "QPushButton:hover{ background:#4ba; }");
    btn_close->setStyleSheet(
        "QPushButton{ padding:6px 18px; border-radius:4px; }");

    connect(btn_save, &QPushButton::clicked, this, [this, btn_save]() {
        // Brute-force commit every spinbox: walks all QAbstractSpinBox
        // descendants and interprets their displayed text into value().
        // Without this, "type 30 → 50, click 保存" saved 30 because the
        // typed edit was uncommitted (focus shifted to button before
        // editingFinished fired on the spinbox).
        const auto spinboxes = this->findChildren<QAbstractSpinBox*>();
        for (auto *sb : spinboxes) sb->interpretText();
        if (auto *fw = QApplication::focusWidget()) fw->clearFocus();
        if (cur_row_ >= 0 && cur_row_ < steps_.size()) {
            readEditorsTo(steps_[cur_row_]);
        }
        // 让 Tab4 立即写 JSON, 对话框保持打开
        emit saveStage(steps_);
        // 临时改文本作为视觉反馈, 1.5s 后恢复
        btn_save->setText(QStringLiteral("✓ 已保存 ") +
            QTime::currentTime().toString("HH:mm:ss"));
        QTimer::singleShot(1500, btn_save,
            [btn_save]() { btn_save->setText(QStringLiteral("保存")); });
    });
    connect(btn_close, &QPushButton::clicked, this, &QDialog::reject);

    bottom_row->addWidget(btn_close);
    bottom_row->addWidget(btn_save);

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
            // ±15° + 2500ms: ±5° 时 Piper 控制器在死区/量化范围内 hunting
            // (反复微调) → 看起来还是抖. 加回大幅度但保留长间隔, 单帧是
            // 干净的大位移, 走完才反向.
            const QVector<double> ry_seq = { ry0 + 15.0, ry0 - 15.0,
                                              ry0 + 15.0, ry0 };
            constexpr int kStepMs = 2500;   // 单帧走完留足余量
            btn_fix->setEnabled(false);
            for (int i = 0; i < ry_seq.size(); ++i) {
                QTimer::singleShot(i * kStepMs, this,
                    [this, x, y, z, rx, ry = ry_seq[i], rz]() {
                        if (!rpc_) return;
                        // mode="L" (笛卡尔线性): 起终 X/Y/Z 一致 → TCP 全程
                        // 锁住; 只有姿态 slerp. "P" 走关节空间 → TCP 弧线漂移.
                        QJsonObject p;
                        p["X_mm"]   = x;  p["Y_mm"]   = y;  p["Z_mm"]   = z;
                        p["RX_deg"] = rx; p["RY_deg"] = ry; p["RZ_deg"] = rz;
                        p["mode"]   = "L";
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
                           "在同一点就说明 OK. 大约 10 秒.</i>"),
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

        // Sign convention, same as the AirportWidget buttons on the
        // dashboard: 负方向 (−) is always the direction that runs toward the
        // homed zero. "前进/后退" said nothing about which physical way that
        // was and got read backwards more than once.
        ar_action_ = new QComboBox(w);
        ar_action_->addItem(QStringLiteral("机场平台锁定 (+) (导轨 1+3)"),  "lock");
        ar_action_->addItem(QStringLiteral("机场平台释放 (−) (导轨 1+3)"),  "release");
        ar_action_->addItem(QStringLiteral("机场夹爪导轨 正向 (+) (导轨 2)"), "rail2_fwd");
        ar_action_->addItem(QStringLiteral("机场夹爪导轨 负向 (−) (导轨 2)"), "rail2_back");

        ar_stop_mode_ = new QComboBox(w);
        ar_stop_mode_->addItem(QStringLiteral("堵转停 (撞死自动停)"),      "stall");
        ar_stop_mode_->addItem(QStringLiteral("固定距离 (相对走 N mm)"),   "distance");
        ar_stop_mode_->addItem(QStringLiteral("绝对位置 (走到距零点 N mm)"), "position");

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

        // Absolute target, measured from the zero that 归零 latched (the
        // release-end hard stop). Never negative: zero IS the stop, so a
        // negative target can only mean grinding into it.
        ar_position_ = new QDoubleSpinBox(w);
        ar_position_->setRange(0.0, 2000.0);
        ar_position_->setDecimals(1);
        ar_position_->setSingleStep(1.0);
        ar_position_->setValue(100.0);
        ar_position_->setSuffix("mm");

        ar_max_ms_ = new QSpinBox(w);
        ar_max_ms_->setRange(1000, 60000);
        ar_max_ms_->setValue(7000);
        ar_max_ms_->setSingleStep(500);
        ar_max_ms_->setSuffix("ms");

        f->addRow(QStringLiteral("动作"), ar_action_);
        f->addRow(QStringLiteral("停止方式"), ar_stop_mode_);
        f->addRow(QStringLiteral("速度"), ar_speed_);
        f->addRow(QStringLiteral("距离 (相对)"), ar_distance_);
        f->addRow(QStringLiteral("目标位置 (绝对)"), ar_position_);
        f->addRow(QStringLiteral("最长等待"), ar_max_ms_);
        f->addRow(new QLabel(
            QStringLiteral("<i>堵转停: 电机一直走, 驱动器判定堵转 (转速&lt;8RPM<br>"
                           "且电流&gt;阈值且持续超时) 置 status 0x04/0x08,<br>"
                           "网关读到就急停; 或读 CAN 失败 8 次自动停。<br>"
                           "固定距离: 从当前位置相对走 N mm (闭环编码器计数)。<br>"
                           "绝对位置: 走到距零点 N mm 处, 增量由网关算;<br>"
                           "<b>必须先归零</b>, 未归零该步会被拒绝。<br>"
                           "最长等待是安全兜底, 提前到位会立即推进。</i>"),
            w));

        // Only one of 距离/目标位置 applies at a time — hide the other
        // entirely rather than greying it out (cleaner visual: operator only
        // sees the fields that actually apply).
        // QFormLayout::setRowVisible(QWidget*, bool) needs Qt 6.4+; we're
        // on 6.11.
        auto refreshAirportRailFields = [this, f]() {
            const QString mode = ar_stop_mode_->currentData().toString();
            f->setRowVisible(ar_distance_, mode == "distance");
            f->setRowVisible(ar_position_, mode == "position");
        };

        connect(ar_action_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &StageConfigDialog::onParamChanged);
        connect(ar_stop_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, refreshAirportRailFields]() {
            refreshAirportRailFields();
            // A full-travel absolute move can take tens of seconds (150mm at
            // 1500rpm ≈ 38s on rail 2); the 7000ms stall default would fire
            // the safety bound mid-move. Nudge it up, but only from the stall
            // default — never stomp a value the operator chose themselves.
            if (ar_stop_mode_->currentData().toString() == "position" &&
                ar_max_ms_->value() <= 7000) {
                const QSignalBlocker b(ar_max_ms_);
                ar_max_ms_->setValue(45000);
            }
            onParamChanged();
        });
        connect(ar_speed_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &StageConfigDialog::onParamChanged);
        connect(ar_distance_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &StageConfigDialog::onParamChanged);
        connect(ar_position_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
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
                    // proc_piper returns arm.get_pose as a bare List[float],
                    // which RpcClient stashes under "_array". Fallback to
                    // legacy named-key form.
                    QVector<double> v(6, 0.0);
                    QJsonArray arr = reply.value("_array").toArray();
                    if (arr.size() != 6) arr = reply.value("pose").toArray();
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

    // 9 DOOR (舱门) / 10 HELIPAD (停机坪升降)
    //
    // Both are the same shape — an action combo, a GUI-side upper bound, a
    // live-state readout and a 录当前状态 button — so build them from one
    // lambda instead of duplicating the wiring twice.
    {
        auto buildAxisPanel = [this](bool helipad,
                                     QComboBox **action_out,
                                     QSpinBox  **max_ms_out,
                                     QLabel    **state_out,
                                     QPushButton **record_out,
                                     int default_max_ms,
                                     const QString &hint) {
            auto *w = new QWidget(this);
            auto *f = new QFormLayout(w);
            f->setContentsMargins(8, 8, 8, 8);

            auto *action = new QComboBox(w);
            if (helipad) {
                action->addItem(QStringLiteral("上升 (到上限位 X1)"), "up");
                action->addItem(QStringLiteral("下降 (到下限位 X2)"), "down");
                action->addItem(QStringLiteral("停止 (立即断电)"),     "stop");
            } else {
                action->addItem(QStringLiteral("开舱门 (到开限位 X3)"), "open");
                action->addItem(QStringLiteral("关舱门 (到关限位 X4)"), "close");
                action->addItem(QStringLiteral("停止 (立即断电)"),       "stop");
            }

            auto *max_ms = new QSpinBox(w);
            max_ms->setRange(500, 120000);
            max_ms->setValue(default_max_ms);
            max_ms->setSingleStep(1000);
            max_ms->setSuffix("ms");

            auto *state = new QLabel(QStringLiteral("<i>未读取</i>"), w);
            state->setStyleSheet("color:#888aaa; font-family: Consolas;");

            auto *record = new QPushButton(QStringLiteral("📍 录当前状态为本步动作"), w);
            record->setStyleSheet(
                "QPushButton{ background:#3a8; color:white; font-weight:bold;"
                " padding:4px 10px; border-radius:4px; }"
                "QPushButton:hover{ background:#4ba; }");

            connect(action, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, &StageConfigDialog::onParamChanged);
            connect(max_ms, QOverload<int>::of(&QSpinBox::valueChanged),
                    this, &StageConfigDialog::onParamChanged);
            connect(record, &QPushButton::clicked, this,
                    [this, helipad]() { recordDoorState(helipad); });

            f->addRow(QStringLiteral("动作"), action);
            f->addRow(QStringLiteral("最长等待"), max_ms);
            f->addRow(QStringLiteral("当前状态"), state);
            f->addRow(record);
            f->addRow(new QLabel(hint, w));

            *action_out = action;
            *max_ms_out = max_ms;
            *state_out  = state;
            *record_out = record;
            return w;
        };

        editor_stack_->insertWidget(
            int(StepType::DOOR),
            buildAxisPanel(false, &dr_action_, &dr_max_ms_, &dr_state_, &btn_dr_record_,
                           20000,
                           QStringLiteral(
                               "<i>door.open / door.close / door.stop → proc_door (RS485)。"
                               "后端异步驱动 Y3/Y4 并自己盯 X3/X4 限位, 到位或超时即断电; "
                               "本步在轴报 moving=false 时立刻推进, 不会白等满最长时间。</i>")));

        editor_stack_->insertWidget(
            int(StepType::HELIPAD),
            buildAxisPanel(true, &hp_action_, &hp_max_ms_, &hp_state_, &btn_hp_record_,
                           30000,
                           QStringLiteral(
                               "<i>helipad.up / helipad.down / helipad.stop → proc_door (RS485)。"
                               "后端异步驱动 Y1/Y2 并自己盯 X1/X2 限位, 到位或超时即断电; "
                               "本步在轴报 moving=false 时立刻推进。</i>")));
    }

    // 11 ARM_TRAJECTORY (机械臂连续轨迹)
    {
        auto *w = new QWidget(this);
        auto *f = new QFormLayout(w);
        f->setContentsMargins(8, 8, 8, 8);

        tj_rate_ = new QSpinBox(w);
        tj_rate_->setRange(5, 50);
        tj_rate_->setValue(20);
        tj_rate_->setSuffix(QStringLiteral(" Hz"));
        tj_rate_->setToolTip(QStringLiteral(
            "proc_piper 的状态缓存约每 20ms 刷新一次, 采样再快只是重复读同一份快照。"
            "而且实测 RPC 往返 p90 是 40ms, 50Hz 会大量丢 tick, 采样间隔变得不均匀。"));

        tj_mindeg_ = new QDoubleSpinBox(w);
        tj_mindeg_->setRange(0.0, 5.0);
        tj_mindeg_->setDecimals(2);
        tj_mindeg_->setSingleStep(0.05);
        tj_mindeg_->setValue(0.05);
        tj_mindeg_->setSuffix(QStringLiteral("°"));
        tj_mindeg_->setToolTip(QStringLiteral(
            "相邻采样最大关节变化小于该值就丢弃。设大了会让点间距变得不均匀, "
            "回放时速度一跳一跳; 只用来滤掉完全静止的那几秒。0 = 全保留。"));

        tj_speed_ = new QSpinBox(w);
        tj_speed_->setRange(1, 100);
        tj_speed_->setValue(30);
        tj_speed_->setSuffix(QStringLiteral(" %"));

        // Smoothing OFF by default. A moving average cuts corners, and a
        // corner the operator deliberately drove around an obstacle is
        // exactly the shape that must not be rounded off. Faithful capture
        // is the safe default; smoothing is opt-in for open space.
        tj_smooth_on_ = new QCheckBox(QStringLiteral("平滑处理 (靠近障碍物时不要勾)"), w);
        tj_smooth_on_->setChecked(false);
        tj_smooth_on_->setToolTip(QStringLiteral(
            "不勾选 = 完全不处理: 原样保存录到的每一个点和它的时间戳, "
            "不重采样、不平均。回放会更抖, 但轨迹和你手拖的完全一致。\n"
            "勾选 = 重采样到均匀时间网格 + 滑动平均, 回放更顺, "
            "但拐角会被削圆 —— 绕障碍物的轨迹不要用。"));

        tj_smooth_ = new QSpinBox(w);
        tj_smooth_->setRange(0, 31);
        tj_smooth_->setSingleStep(2);
        tj_smooth_->setValue(5);
        tj_smooth_->setEnabled(false);          // follows the checkbox
        tj_smooth_->setSuffix(QStringLiteral(" 点"));
        tj_smooth_->setToolTip(QStringLiteral(
            "录制结束后做居中滑动平均的窗口宽度 (0/1/2 = 不平滑)。"
            "人手震颤在 8~12Hz 会被原样录进去, 回放时就是振动; "
            "有意的动作在 5Hz 以下, 平滑基本不影响。首末点固定不参与平滑。"));

        tj_state_ = new QLabel(QStringLiteral("<i>未录制</i>"), w);
        tj_state_->setStyleSheet(QStringLiteral("color:#888aaa; font-family: Consolas;"));

        btn_tj_record_ = new QPushButton(QStringLiteral("● 开始录制 (拖动机械臂)"), w);
        btn_tj_record_->setCheckable(true);
        btn_tj_record_->setStyleSheet(QStringLiteral(
            "QPushButton{ background:#3a8; color:white; font-weight:bold;"
            " padding:4px 10px; border-radius:4px; }"
            "QPushButton:hover{ background:#4ba; }"
            "QPushButton:checked{ background:#c33; }"));

        btn_tj_clear_ = new QPushButton(QStringLiteral("🗑 清空本步轨迹"), w);

        // Same 使能/恢复 as the main PiperWidget. Recording lives and dies by
        // the arm's mode: after a drag the firmware stays in ctrl_mode=0x02
        // and the next recording (or any replay) will not work until the
        // handshake pulls it back to CAN_CTRL. Having the button here saves
        // hopping back to the main window between takes.
        btn_tj_enable_ = new QPushButton(QStringLiteral("⚡ 使能 / 恢复"), w);
        btn_tj_enable_->setToolTip(QStringLiteral(
            "重新执行 V1.8-2 握手 (Resume → MasterSlave → STANDBY → CAN_CTRL → Enable)，"
            "和主界面那个按钮完全一样。\n"
            "拖动示教退出后固件停在 ctrl_mode=2, 点这个拉回 CAN_CTRL。\n\n"
            "⚠️ 握手途中会经过 STANDBY, 电机可能瞬间失力 —— 机械臂伸展时请用手托住。"));
        btn_tj_enable_->setStyleSheet(QStringLiteral(
            "QPushButton{ background:#3a8; color:white; font-weight:bold;"
            " padding:4px 14px; border-radius:4px; }"
            "QPushButton:hover{ background:#4ba; }"));

        connect(tj_rate_, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int hz) {
                    if (tj_timer_) tj_timer_->setInterval(1000 / qBound(5, hz, 50));
                    onParamChanged();
                });
        connect(tj_mindeg_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &StageConfigDialog::onParamChanged);
        connect(tj_speed_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &StageConfigDialog::onParamChanged);
        connect(tj_smooth_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &StageConfigDialog::onParamChanged);
        connect(tj_smooth_on_, &QCheckBox::toggled, this, [this](bool on) {
            tj_smooth_->setEnabled(on);
            onParamChanged();
        });
        connect(btn_tj_record_, &QPushButton::toggled,
                this, &StageConfigDialog::onTrajRecordToggled);
        connect(btn_tj_enable_, &QPushButton::clicked, this, [this]() {
            if (!rpc_ || !rpc_->isConnected()) {
                tj_state_->setText(QStringLiteral("未连接 RK3588"));
                tj_state_->setStyleSheet(
                    QStringLiteral("color:#e05050; font-family: Consolas;"));
                return;
            }
            tj_state_->setText(QStringLiteral("握手中…"));
            tj_state_->setStyleSheet(QStringLiteral("color:#e0a030; font-family: Consolas;"));
            rpc_->call(Protocol::Methods::PIPER_HANDSHAKE, QJsonObject{},
                [this](QJsonObject reply) {
                    // Report the resulting mode rather than just "sent" — the
                    // handshake can be refused, and a silent button would
                    // leave the operator retrying a recording that cannot work.
                    const bool ok = reply.value(QStringLiteral("ok")).toBool(true);
                    const int cm = reply.value(QStringLiteral("ctrl_mode")).toInt(-1);
                    if (ok) {
                        tj_state_->setText(QStringLiteral("✓ 已使能 (ctrl_mode=%1)")
                                               .arg(cm < 0 ? 1 : cm));
                        tj_state_->setStyleSheet(QStringLiteral(
                            "color:#3ac06a; font-family: Consolas; font-weight:bold;"));
                    } else {
                        tj_state_->setText(QStringLiteral("使能失败: %1")
                            .arg(reply.value(QStringLiteral("error")).toString(
                                     QStringLiteral("未知"))));
                        tj_state_->setStyleSheet(
                            QStringLiteral("color:#e05050; font-family: Consolas;"));
                    }
                });
        });
        connect(btn_tj_clear_, &QPushButton::clicked, this, [this]() {
            if (cur_row_ < 0 || cur_row_ >= steps_.size()) return;
            steps_[cur_row_].params.remove(QStringLiteral("t_ms"));
            steps_[cur_row_].params.remove(QStringLiteral("joints_flat"));
            tj_state_->setText(QStringLiteral("<i>已清空</i>"));
            tj_state_->setStyleSheet(QStringLiteral("color:#888aaa; font-family: Consolas;"));
            refreshList();
        });

        f->addRow(QStringLiteral("采样频率"), tj_rate_);
        f->addRow(QStringLiteral("静止阈值"), tj_mindeg_);
        f->addRow(QStringLiteral("回放速度"), tj_speed_);
        f->addRow(tj_smooth_on_);
        f->addRow(QStringLiteral("平滑窗口"), tj_smooth_);
        f->addRow(QStringLiteral("轨迹"), tj_state_);
        f->addRow(btn_tj_record_);
        auto *tjBtnRow = new QHBoxLayout;
        tjBtnRow->addWidget(btn_tj_enable_);
        tjBtnRow->addWidget(btn_tj_clear_);
        f->addRow(tjBtnRow);
        f->addRow(new QLabel(QStringLiteral(
            "<i>点「开始录制」后监测固件 teach_status: 用手拖动机械臂即开始采集, "
            "松手自动结束并存入本步。整段存成<b>一个步骤</b>, 回放时按录制的"
            "时间戳逐点下发, 还原当时的速度。<br>"
            "<b>首点和末点原样保留</b>, 不参与平滑与重采样。</i>"), w));

        editor_stack_->insertWidget(int(StepType::ARM_TRAJECTORY), w);
    }
}

// ── 连续轨迹录制 ────────────────────────────────────────────────────────
void StageConfigDialog::onTrajRecordToggled(bool on)
{
    if (!tj_timer_) {
        tj_timer_ = new QTimer(this);
        connect(tj_timer_, &QTimer::timeout, this, &StageConfigDialog::onTrajRecordTick);
    }
    if (!on) {
        tj_timer_->stop();
        tj_capturing_ = false;
        btn_tj_record_->setText(QStringLiteral("● 开始录制 (拖动机械臂)"));
        return;
    }
    if (!rpc_ || !rpc_->isConnected()) {
        tj_state_->setText(QStringLiteral("未连接 RK3588"));
        tj_state_->setStyleSheet(QStringLiteral("color:#e05050; font-family: Consolas;"));
        btn_tj_record_->setChecked(false);
        return;
    }
    if (cur_row_ < 0 || cur_row_ >= steps_.size()) {
        btn_tj_record_->setChecked(false);
        return;
    }
    tj_capturing_ = false;
    tj_inflight_  = false;
    tj_skipped_   = 0;
    tj_t_ms_.clear();
    tj_flat_.clear();
    tj_last_joints_.clear();
    tj_timer_->setInterval(1000 / qBound(5, tj_rate_->value(), 50));
    tj_timer_->start();
    btn_tj_record_->setText(QStringLiteral("■ 待命中 — 拖动机械臂开始"));
    tj_state_->setText(QStringLiteral("等待示教…"));
    tj_state_->setStyleSheet(QStringLiteral("color:#e0a030; font-family: Consolas;"));
}

void StageConfigDialog::onTrajRecordTick()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    // Never stack polls — a reply that lands after the next tick carries a
    // pose stamped with the wrong time.
    if (tj_inflight_) return;
    tj_inflight_ = true;

    rpc_->call(Protocol::Methods::PIPER_GET_STATUS, QJsonObject{},
        [this](QJsonObject reply) {
            tj_inflight_ = false;
            if (!btn_tj_record_ || !btn_tj_record_->isChecked()) return;

            const int ts = reply.value(QStringLiteral("teach_status")).toInt(0);

            if (ts == 1 && !tj_capturing_) {
                tj_capturing_ = true;
                tj_t0_ = QDateTime::currentMSecsSinceEpoch();
                tj_skipped_ = 0;
                tj_t_ms_.clear();
                tj_flat_.clear();
                tj_last_joints_.clear();
                btn_tj_record_->setText(QStringLiteral("● 录制中 — 松手结束"));
                tj_state_->setText(QStringLiteral("● 录制中…"));
                tj_state_->setStyleSheet(QStringLiteral(
                    "color:#e05050; font-family: Consolas; font-weight:bold;"));
            }

            // Anything other than 1 ends the drag: 2 = released, 0 = reset by
            // the 使能 handshake.
            if (ts != 1 && tj_capturing_) {
                tj_capturing_ = false;
                commitTrajectory();
                btn_tj_record_->setChecked(false);   // one drag = one step
                return;
            }
            if (!tj_capturing_) return;

            const QJsonArray arr = reply.value(Protocol::Fields::ANGLES).toArray();
            if (arr.size() == 6) {
                QVector<float> j;
                for (const auto &v : arr) j.append(float(v.toDouble()));
                appendTrajSample(j);
                return;
            }
            // Older proc_piper has no `angles` in get_status. Fall back so the
            // recorder still works — but this costs a second round trip per
            // sample, which is enough to start dropping ticks at 20 Hz.
            rpc_->call(Protocol::Methods::ARM_GET_ANGLES, QJsonObject{},
                [this](QJsonObject r2) {
                    const QJsonArray a2 = r2.value(Protocol::Fields::ANGLES).toArray();
                    if (a2.size() != 6 || !tj_capturing_) return;
                    QVector<float> j2;
                    for (const auto &v : a2) j2.append(float(v.toDouble()));
                    appendTrajSample(j2);
                });
        });
}

void StageConfigDialog::appendTrajSample(const QVector<float> &j)
{
    if (!tj_capturing_ || j.size() != 6) return;

    // Drop samples where nothing moved. Timestamps still advance, so a pause
    // survives as a longer gap rather than as hundreds of identical rows.
    if (!tj_last_joints_.isEmpty()) {
        float d = 0.0f;
        for (int i = 0; i < 6 && i < tj_last_joints_.size(); ++i) {
            d = qMax(d, float(qAbs(j[i] - tj_last_joints_[i])));
        }
        if (d < tj_mindeg_->value()) { ++tj_skipped_; return; }
    }

    tj_t_ms_.append(int(QDateTime::currentMSecsSinceEpoch() - tj_t0_));
    for (int i = 0; i < 6; ++i) tj_flat_.append(double(j[i]));
    tj_last_joints_ = j;

    tj_state_->setText(QStringLiteral("● 录制中… %1 点 / %2s")
                           .arg(tj_t_ms_.size())
                           .arg(tj_t_ms_.last() / 1000.0, 0, 'f', 1));
}

// Raw capture → uniform time grid → moving average, with the first and last
// samples pinned to their exact recorded values.
//
// Why resample: the raw samples are NOT evenly spaced. The still-threshold
// drops samples while the operator moves slowly, and any RPC round trip
// slower than the tick period skips one outright (measured p90 is 40 ms
// against a 50 ms budget, so this happens routinely). Replaying unevenly
// spaced points on an even clock makes the commanded velocity jump between
// every pair — jerk that was never in the taught motion.
//
// Why smooth: a human hand shakes at roughly 8-12 Hz and that tremor is in
// the capture. Replayed faithfully it becomes vibration. Deliberate motion
// lives below ~5 Hz, so a short centred average removes the shake and leaves
// the move. This is also why sampling faster does not help — it captures the
// tremor better.
//
// Why pin the endpoints: the first point is where the arm must start and the
// last is where it must end — those two are commonly aligned to a gripper or
// a socket, and an averaged endpoint would sit somewhere the operator never
// put the arm. A centred average necessarily pulls end samples toward their
// neighbours, so they are restored verbatim afterwards.
static void resampleAndSmooth(const QVector<int> &t_in, const QVector<double> &flat_in,
                              int period_ms, int smooth_win,
                              QVector<int> *t_out, QVector<double> *flat_out)
{
    t_out->clear();
    flat_out->clear();
    const int n_in = t_in.size();
    if (n_in == 0) return;
    const int total = t_in.last();
    if (n_in < 3 || total <= 0 || period_ms <= 0) {
        *t_out = t_in;
        *flat_out = flat_in;
        return;
    }

    QVector<double> grid;
    int src = 0;
    for (int tt = 0; tt <= total; tt += period_ms) {
        while (src + 1 < n_in && t_in[src + 1] < tt) ++src;
        const int i0 = qMin(src, n_in - 1);
        const int i1 = qMin(src + 1, n_in - 1);
        const int t0 = t_in[i0];
        const int t1 = t_in[i1];
        const double a = (t1 > t0) ? qBound(0.0, double(tt - t0) / double(t1 - t0), 1.0) : 0.0;
        for (int k = 0; k < 6; ++k) {
            const double v0 = flat_in[i0 * 6 + k];
            const double v1 = flat_in[i1 * 6 + k];
            grid.append(v0 + (v1 - v0) * a);
        }
        t_out->append(tt);
    }
    // Make sure the grid really ends on the recorded end time, so the last
    // sample is the taught final pose and not an interpolation one tick short.
    if (t_out->last() != total) {
        t_out->append(total);
        for (int k = 0; k < 6; ++k) grid.append(flat_in[(n_in - 1) * 6 + k]);
    }

    const int n = t_out->size();
    if (smooth_win < 3 || n < 3) { *flat_out = grid; return; }

    const int half = qMin(smooth_win / 2, (n - 1) / 2);
    flat_out->resize(n * 6);
    for (int i = 0; i < n; ++i) {
        for (int k = 0; k < 6; ++k) {
            double sum = 0.0;
            int cnt = 0;
            for (int d = -half; d <= half; ++d) {
                const int idx = i + d;
                if (idx < 0 || idx >= n) continue;   // shrink at the ends rather
                sum += grid[idx * 6 + k];            // than pad, so the taper is
                ++cnt;                               // symmetric
            }
            (*flat_out)[i * 6 + k] = sum / cnt;
        }
    }

    // Endpoints restored verbatim — see the note above.
    for (int k = 0; k < 6; ++k) {
        (*flat_out)[k]                 = flat_in[k];
        (*flat_out)[(n - 1) * 6 + k]   = flat_in[(n_in - 1) * 6 + k];
    }
}

void StageConfigDialog::commitTrajectory()
{
    if (cur_row_ < 0 || cur_row_ >= steps_.size()) return;
    if (tj_t_ms_.isEmpty()) {
        tj_state_->setText(QStringLiteral("未采到点 (未检测到拖动?)"));
        tj_state_->setStyleSheet(QStringLiteral("color:#e05050; font-family: Consolas;"));
        return;
    }

    const int raw_n = tj_t_ms_.size();
    const bool do_smooth = tj_smooth_on_ && tj_smooth_on_->isChecked();
    QVector<int> rt;
    QVector<double> rf;
    if (do_smooth) {
        resampleAndSmooth(tj_t_ms_, tj_flat_,
                          1000 / qBound(5, tj_rate_->value(), 50),
                          tj_smooth_->value(), &rt, &rf);
    } else {
        // Verbatim. Not even resampled: interpolating onto a uniform grid
        // puts every intermediate point on the straight chord between two
        // captured samples, which shaves the outside of a curve. That is
        // harmless in open space and unacceptable when the curve is how the
        // operator got around an obstacle. Replay already honours each
        // point's own timestamp, so uneven spacing costs smoothness, not
        // accuracy.
        rt = tj_t_ms_;
        rf = tj_flat_;
    }

    QVariantList t, flat;
    t.reserve(rt.size());
    flat.reserve(rf.size());
    for (int v : rt)    t.append(v);
    for (double v : rf) flat.append(v);

    steps_[cur_row_].params[QStringLiteral("t_ms")]        = t;
    steps_[cur_row_].params[QStringLiteral("joints_flat")] = flat;
    steps_[cur_row_].params[QStringLiteral("sample_hz")]   = tj_rate_->value();
    steps_[cur_row_].params[QStringLiteral("speed_ratio")] = tj_speed_->value() / 100.0;
    steps_[cur_row_].params[QStringLiteral("smooth_win")]  = tj_smooth_->value();
    steps_[cur_row_].params[QStringLiteral("smooth_on")]   = do_smooth;

    tj_state_->setText(do_smooth
        ? QStringLiteral("✓ 原始 %1 点 → 均匀 %2 点 / %3s  平滑%4点  首末点已固定")
              .arg(raw_n).arg(rt.size())
              .arg(rt.isEmpty() ? 0.0 : rt.last() / 1000.0, 0, 'f', 1)
              .arg(tj_smooth_->value())
        : QStringLiteral("✓ 原样保存 %1 点 / %2s  (未平滑, 未重采样)")
              .arg(rt.size())
              .arg(rt.isEmpty() ? 0.0 : rt.last() / 1000.0, 0, 'f', 1));
    tj_state_->setStyleSheet(QStringLiteral(
        "color:#3ac06a; font-family: Consolas; font-weight:bold;"));
    refreshList();
}

// 录当前状态: read door.get_status and select the action matching the
// position the axis is resting at. The arm's 录当前关节 captures a live
// pose; for a two-ended axis the equivalent capture is "which end is it at
// right now" — that is exactly the action that would reproduce it.
void StageConfigDialog::recordDoorState(bool helipad)
{
    QLabel *state_lbl = helipad ? hp_state_ : dr_state_;
    if (!rpc_ || !rpc_->isConnected()) {
        if (state_lbl) {
            state_lbl->setText(QStringLiteral("未连接 RK3588"));
            state_lbl->setStyleSheet("color:#e05050; font-family: Consolas;");
        }
        return;
    }

    rpc_->call(Protocol::Methods::DOOR_GET_STATUS, QJsonObject{},
        [this, helipad, state_lbl](QJsonObject reply) {
            QComboBox *combo = helipad ? hp_action_ : dr_action_;
            if (!combo || !state_lbl) return;

            if (!reply.value("connected").toBool(false)) {
                state_lbl->setText(QStringLiteral("继电器模块无响应"));
                state_lbl->setStyleSheet("color:#e05050; font-family: Consolas;");
                return;
            }

            const QJsonObject axis =
                reply.value(helipad ? "helipad" : "hatch").toObject();
            const QString st = axis.value("state").toString();

            // state → the action that puts the axis back here.
            QString action;
            QString shown;
            if (helipad) {
                if      (st == "top")    { action = "up";   shown = QStringLiteral("已到顶"); }
                else if (st == "bottom") { action = "down"; shown = QStringLiteral("已到底"); }
            } else {
                if      (st == "opened") { action = "open";  shown = QStringLiteral("已开到位"); }
                else if (st == "closed") { action = "close"; shown = QStringLiteral("已关到位"); }
            }

            if (action.isEmpty()) {
                // Mid-travel / fault / no reading — there is no end position
                // to capture, so change nothing rather than guess.
                state_lbl->setText(
                    QStringLiteral("%1 — 不在限位上, 未录入").arg(st.isEmpty() ? "unknown" : st));
                state_lbl->setStyleSheet("color:#e0a030; font-family: Consolas;");
                return;
            }

            const int idx = combo->findData(action);
            if (idx >= 0) {
                const QSignalBlocker block(combo);
                combo->setCurrentIndex(idx);
            }
            state_lbl->setText(shown + QStringLiteral(" → 已录为本步动作"));
            state_lbl->setStyleSheet("color:#3ac06a; font-family: Consolas;");
            onParamChanged();
        });
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
        case StepType::DOOR:
            s.params["action"] = "open";
            s.params["max_ms"] = 20000;    // matches UAV_DOOR_HATCH_TIMEOUT_MS
            break;
        case StepType::HELIPAD:
            s.params["action"] = "up";
            s.params["max_ms"] = 30000;    // matches UAV_DOOR_PAD_TIMEOUT_MS
            break;
        case StepType::ARM_TRAJECTORY:
            // Starts empty on purpose — points only exist after a drag.
            s.params["sample_hz"]   = 20;
            s.params["speed_ratio"] = 0.30;
            s.params["smooth_win"]  = 5;
            s.params["smooth_on"]   = false;   // faithful capture by default
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
            // Per-field signal blockers — without these, the chain of
            // setValue calls fires valueChanged → onParamChanged →
            // readEditorsTo, which writes editor state (with some fields
            // still stale from a previous row's view) into steps_[cur_row_].
            // Brief data corruption window that can persist if anything
            // else fires onParamChanged before the chain finishes.
            const QSignalBlocker b0(mj_j_[0]), b1(mj_j_[1]), b2(mj_j_[2]);
            const QSignalBlocker b3(mj_j_[3]), b4(mj_j_[4]), b5(mj_j_[5]);
            const QSignalBlocker bs(mj_speed_);
            const auto j = s.params.value("joints").toList();
            for (int i = 0; i < 6; ++i)
                if (j.size() > i) mj_j_[i]->setValue(j[i].toDouble());
            mj_speed_->setValue(int(s.params.value("speed_ratio", 0.3).toDouble() * 100));
            break;
        }
        case StepType::MOVE_CARTESIAN: {
            const QSignalBlocker bx(mc_x_), by(mc_y_), bz(mc_z_);
            const QSignalBlocker brx(mc_rx_), bry(mc_ry_), brz(mc_rz_);
            const QSignalBlocker bm(mc_mode_);
            mc_x_->setValue(s.params.value("x_mm").toDouble());
            mc_y_->setValue(s.params.value("y_mm").toDouble());
            mc_z_->setValue(s.params.value("z_mm").toDouble());
            mc_rx_->setValue(s.params.value("rx_deg").toDouble());
            mc_ry_->setValue(s.params.value("ry_deg").toDouble());
            mc_rz_->setValue(s.params.value("rz_deg").toDouble());
            mc_mode_->setCurrentIndex(s.params.value("mode", "P").toString() == "L" ? 1 : 0);
            break;
        }
        case StepType::GRIPPER: {
            // Per-widget signal blockers — same fix as AIRPORT_RAIL.
            // Without this, setValue(angle) fires valueChanged →
            // onParamChanged → readEditorsTo, which reads the STALE
            // gr_force_ (default 30) and overwrites the just-loaded
            // saved force in steps_[cur_row_]. The next setValue(force)
            // restores it, but the brief window where steps_[cur_row_]
            // has stale data can cause data loss if onParamChanged is
            // called from any other code path during the window.
            const QSignalBlocker b_angle(gr_angle_);
            const QSignalBlocker b_force(gr_force_);
            gr_angle_->setValue(s.params.value("angle_mm").toDouble());
            gr_force_->setValue(s.params.value("force_pct").toInt());
            break;
        }
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
            const QSignalBlocker b_pos     (ar_position_);
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
            ar_position_->setValue(s.params.value("position_mm", 100.0).toDouble());
            ar_max_ms_->setValue(s.params.value("max_ms", 7000).toInt());

            // Re-apply row visibility (the lambda hooked to ar_stop_mode_'s
            // currentIndexChanged is blocked by the QSignalBlocker above).
            if (auto *form = qobject_cast<QFormLayout*>(ar_distance_->parentWidget()->layout())) {
                form->setRowVisible(ar_distance_, stop_mode == "distance");
                form->setRowVisible(ar_position_, stop_mode == "position");
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
        case StepType::DOOR: {
            const QSignalBlocker b_a(dr_action_), b_m(dr_max_ms_);
            int idx = dr_action_->findData(s.params.value("action", "open").toString());
            dr_action_->setCurrentIndex(idx < 0 ? 0 : idx);
            dr_max_ms_->setValue(s.params.value("max_ms", 20000).toInt());
            break;
        }
        case StepType::HELIPAD: {
            const QSignalBlocker b_a(hp_action_), b_m(hp_max_ms_);
            int idx = hp_action_->findData(s.params.value("action", "up").toString());
            hp_action_->setCurrentIndex(idx < 0 ? 0 : idx);
            hp_max_ms_->setValue(s.params.value("max_ms", 30000).toInt());
            break;
        }
        case StepType::ARM_TRAJECTORY: {
            const QSignalBlocker b_r(tj_rate_), b_d(tj_mindeg_),
                                 b_s(tj_speed_), b_w(tj_smooth_),
                                 b_o(tj_smooth_on_);
            tj_rate_->setValue(s.params.value("sample_hz", 20).toInt());
            tj_speed_->setValue(int(s.params.value("speed_ratio", 0.3).toDouble() * 100));
            tj_smooth_->setValue(s.params.value("smooth_win", 5).toInt());
            // Older steps predate the flag; they were recorded with smoothing
            // on, so default true for them and false for anything new.
            const bool son = s.params.value("smooth_on",
                                            s.params.contains("smooth_win")).toBool();
            tj_smooth_on_->setChecked(son);
            tj_smooth_->setEnabled(son);
            const QVariantList tl = s.params.value("t_ms").toList();
            if (tl.isEmpty()) {
                tj_state_->setText(QStringLiteral("<i>未录制</i>"));
                tj_state_->setStyleSheet(
                    QStringLiteral("color:#888aaa; font-family: Consolas;"));
            } else {
                tj_state_->setText(QStringLiteral("已录制 %1 点 / %2s")
                                       .arg(tl.size())
                                       .arg(tl.last().toInt() / 1000.0, 0, 'f', 1));
                tj_state_->setStyleSheet(QStringLiteral(
                    "color:#3ac06a; font-family: Consolas; font-weight:bold;"));
            }
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
            s.params["position_mm"] = ar_position_->value();
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
        case StepType::DOOR:
            s.params["action"] = dr_action_->currentData().toString();
            s.params["max_ms"] = dr_max_ms_->value();
            break;
        case StepType::HELIPAD:
            s.params["action"] = hp_action_->currentData().toString();
            s.params["max_ms"] = hp_max_ms_->value();
            break;
        case StepType::ARM_TRAJECTORY:
            // Only the knobs. t_ms / joints_flat are written by
            // commitTrajectory() and must survive every editor round-trip —
            // this runs on any spinbox change, and rebuilding the whole param
            // map here would silently erase a finished recording.
            s.params["sample_hz"]   = tj_rate_->value();
            s.params["speed_ratio"] = tj_speed_->value() / 100.0;
            s.params["smooth_win"]  = tj_smooth_->value();
            s.params["smooth_on"]   = tj_smooth_on_->isChecked();
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
            const double pos  = s.params.value("position_mm", 100.0).toDouble();
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
                [this, action, stop_mode, rpm, dist, pos, direction, watched_rails]() {
                    if (!rpc_) return;
                    if (stop_mode == "position") {
                        // ABSOLUTE — go to pos_mm from the homed zero. Must be
                        // listed explicitly: this chain used to end in a plain
                        // `else` that meant "stall", so a position step fell
                        // through to airport.set_speed and ran the rail all the
                        // way into its hard stop. Anything unrecognised now
                        // still lands on stall, but position no longer does.
                        for (const QJsonValue &rv : watched_rails) {
                            const int rail_idx = rv.toInt();
                            QJsonObject p;
                            p["rail"]      = rail_idx;
                            p["pos_mm"]    = pos;
                            p["speed_rpm"] = std::abs(rpm);
                            rpc_->call("airport.move_to_mm", p,
                                [this, rail_idx, pos](QJsonObject reply) {
                                    if (reply.value("ok").toBool(true)) return;
                                    // Refusals are silent at the RPC layer, and
                                    // a step that quietly does nothing is worse
                                    // than one that errors — say why.
                                    const bool homed = reply.value("homed").toBool(true);
                                    QMessageBox::warning(this,
                                        QStringLiteral("绝对位置被拒绝"),
                                        homed
                                          ? QStringLiteral("导轨%1 走到 %2mm 被拒绝: 超出软限位, "
                                                           "或读取当前位置失败。")
                                                .arg(rail_idx + 1).arg(pos, 0, 'f', 1)
                                          : QStringLiteral("导轨%1 尚未归零, 无法走绝对位置。\n\n"
                                                           "请先在主界面机场面板点「导轨2 归零」。\n"
                                                           "驱动器多圈计数掉电不保持, 未归零时 "
                                                           "%2mm 只是个任意偏移量。")
                                                .arg(rail_idx + 1).arg(pos, 0, 'f', 1));
                                });
                        }
                    } else if (stop_mode == "distance") {
                        // CLOSED-LOOP precise move — identical to Tab4
                        // dispatchScriptStep. Gateway airport.move_mm
                        // (velocity + 0x36 encoder feedback) stops at the
                        // exact target distance. Signed dist_mm carries the
                        // direction. Replaces the old set_speed + timer + stop
                        // (distance ≈ speed×time) which overshot (50mm→70mm).
                        for (const QJsonValue &rv : watched_rails) {
                            QJsonObject p;
                            p["rail"]      = rv.toInt();
                            p["dist_mm"]   = std::abs(dist) * (direction >= 0 ? +1.0 : -1.0);
                            p["speed_rpm"] = std::abs(rpm);
                            rpc_->call("airport.move_mm", p, [](QJsonObject){});
                        }
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
        case StepType::DOOR:
        case StepType::HELIPAD: {
            // Fire the same RPC the orchestrator would. Motion is async on
            // proc_door, so this returns immediately and the operator
            // watches the DoorWidget LEDs (or 当前状态 here) for progress.
            const QString a = s.params.value("action",
                                             s.type == StepType::DOOR ? "open" : "up").toString();
            QString method;
            if (s.type == StepType::DOOR) {
                if      (a == "close") method = Protocol::Methods::DOOR_CLOSE;
                else if (a == "stop")  method = Protocol::Methods::DOOR_STOP;
                else                   method = Protocol::Methods::DOOR_OPEN;
            } else {
                if      (a == "down")  method = Protocol::Methods::HELIPAD_DOWN;
                else if (a == "stop")  method = Protocol::Methods::HELIPAD_STOP;
                else                   method = Protocol::Methods::HELIPAD_UP;
            }
            rpc_->call(method, QJsonObject{}, nullCb);
            break;
        }
        case StepType::ARM_TRAJECTORY: {
            const QVariantList tl   = s.params.value("t_ms").toList();
            const QVariantList flat = s.params.value("joints_flat").toList();
            if (tl.isEmpty() || flat.size() != tl.size() * 6) {
                QMessageBox::warning(this, QStringLiteral("轨迹"),
                    QStringLiteral("本步还没有录制轨迹, 无法执行。\n"
                                   "点「开始录制」后用手拖动机械臂。"));
                break;
            }
            const double sr = s.params.value("speed_ratio", 0.3).toDouble();
            // Replay at the recorded timestamps so the arm retraces the motion
            // at the speed it was taught.
            auto *timer = new QTimer(this);
            auto *idx   = new int(0);
            const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
            timer->setInterval(10);
            connect(timer, &QTimer::timeout, this, [this, timer, idx, tl, flat, sr, t0]() {
                const qint64 el = QDateTime::currentMSecsSinceEpoch() - t0;
                int sent = -1;
                // Catch up if a tick was late: send the newest sample already
                // due rather than falling progressively behind.
                while (*idx < tl.size() && tl[*idx].toInt() <= el) { sent = *idx; ++(*idx); }
                if (sent >= 0 && rpc_ && rpc_->isConnected()) {
                    QJsonArray j;
                    for (int k = 0; k < 6; ++k) j.append(flat[sent * 6 + k].toDouble());
                    QJsonObject p;
                    p["joints"]      = j;
                    p["speed_ratio"] = sr;
                    rpc_->call(QStringLiteral("arm.move_joints"), p, [](QJsonObject){});
                }
                if (*idx >= tl.size()) {
                    timer->stop();
                    timer->deleteLater();
                    delete idx;
                }
            });
            timer->start();
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
