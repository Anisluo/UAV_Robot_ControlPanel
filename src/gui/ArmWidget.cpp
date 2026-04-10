#include "ArmWidget.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QVariant>

ArmWidget::ArmWidget(RpcClient *rpc, QWidget *parent)
    : QGroupBox("六轴机械臂 [CAN]", parent)
    , rpc_(rpc)
    , poll_timer_(new QTimer(this))
{
    buildUi();

    connect(poll_timer_, &QTimer::timeout, this, &ArmWidget::requestAngles);
    poll_timer_->start(500);
}

void ArmWidget::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(8, 18, 8, 8);

    auto *grid = new QGridLayout;
    grid->setSpacing(4);
    grid->setHorizontalSpacing(6);

    auto makeHdr = [&](const QString &text, int col, Qt::Alignment align = Qt::AlignCenter) {
        auto *l = new QLabel(text, this);
        l->setStyleSheet("color: #607d8b; font-size: 11px;");
        l->setAlignment(align);
        grid->addWidget(l, 0, col);
    };
    makeHdr("轴", 0, Qt::AlignCenter);
    makeHdr("当前位置", 1, Qt::AlignCenter);
    makeHdr("目标位置", 2, Qt::AlignCenter);
    makeHdr("移动", 3, Qt::AlignCenter);
    makeHdr("安全位置", 4, Qt::AlignCenter);
    makeHdr("回零", 5, Qt::AlignCenter);

    const QStringList names = {"J1", "J2", "J3", "J4", "J5", "J6"};

    for (int i = 0; i < 6; ++i) {
        const int row = i + 1;

        // axis label
        auto *axLabel = new QLabel(names[i], this);
        axLabel->setFixedWidth(28);
        axLabel->setAlignment(Qt::AlignCenter);
        axLabel->setStyleSheet("font-family: Consolas; font-weight: bold; color: #00c8d7;");

        // current angle label
        auto *curLabel = new QLabel("--.-°", this);
        curLabel->setFixedWidth(82);
        curLabel->setAlignment(Qt::AlignCenter);
        curLabel->setStyleSheet("color: #9fb3c8; font-family: Consolas;");

        // target angle spinbox（目标位置，移动用）
        auto *targetSpin = new QDoubleSpinBox(this);
        targetSpin->setRange(-360.0, 360.0);
        targetSpin->setSingleStep(1.0);
        targetSpin->setDecimals(1);
        targetSpin->setValue(0.0);
        targetSpin->setSuffix("°");
        targetSpin->setAlignment(Qt::AlignCenter);
        targetSpin->setFixedWidth(82);

        // safe angle spinbox（安全位置，回零后移动到此）
        auto *safeSpin = new QDoubleSpinBox(this);
        safeSpin->setRange(-360.0, 360.0);
        safeSpin->setSingleStep(1.0);
        safeSpin->setDecimals(1);
        safeSpin->setValue(0.0);
        safeSpin->setSuffix("°");
        safeSpin->setAlignment(Qt::AlignCenter);
        safeSpin->setFixedWidth(82);

        auto *moveBtn = new QPushButton("移动", this);
        auto *homeBtn = new QPushButton("回零", this);
        homeBtn->setProperty("axis_index", i);
        moveBtn->setProperty("axis_index", i);
        homeBtn->setFixedHeight(26);
        moveBtn->setFixedHeight(26);
        homeBtn->setFixedWidth(60);
        moveBtn->setFixedWidth(60);

        target_spins_.append(targetSpin);
        safe_spins_.append(safeSpin);
        current_labels_.append(curLabel);
        axis_home_buttons_.append(homeBtn);
        axis_move_buttons_.append(moveBtn);

        grid->addWidget(axLabel,    row, 0);
        grid->addWidget(curLabel,   row, 1);
        grid->addWidget(targetSpin, row, 2);
        grid->addWidget(moveBtn,    row, 3);
        grid->addWidget(safeSpin,   row, 4);
        grid->addWidget(homeBtn,    row, 5);

        connect(homeBtn, &QPushButton::clicked, this, &ArmWidget::onHomeAxis);
        connect(moveBtn, &QPushButton::clicked, this, &ArmWidget::onMoveAxis);
    }

    grid->setColumnStretch(6, 1);
    mainLayout->addLayout(grid);

    // ── Button row ───────────────────────────────────────────────────────────
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);

    btn_estop_ = new QPushButton("急停",   this);
    btn_home_  = new QPushButton("全部回零", this);

    for (auto *b : {btn_estop_, btn_home_})
        b->setFixedHeight(26);

    btn_estop_->setStyleSheet(
        "QPushButton { background: #c0392b; color: white; font-weight: bold; }"
        "QPushButton:hover { background: #e74c3c; }"
        "QPushButton:pressed { background: #962d22; }"
    );

    // 移动速度 (RPM) — 推送给后端的 UAV_ARM_RPM 覆盖
    move_speed_spin_ = new QSpinBox(this);
    move_speed_spin_->setRange(1, 3000);
    move_speed_spin_->setSingleStep(10);
    move_speed_spin_->setValue(300);
    move_speed_spin_->setSuffix(" RPM");
    move_speed_spin_->setFixedWidth(96);
    move_speed_spin_->setFixedHeight(26);
    move_speed_spin_->setToolTip("各轴移动指令的目标转速。修改后会立即发送到机器人。");

    auto *moveSpeedLabel = new QLabel("移动速度:", this);
    moveSpeedLabel->setStyleSheet("color: #607d8b; font-size: 11px;");

    // 归零速度 (RPM) — 推送给后端的 UAV_ARM_ZERO_SPEED_RPM 覆盖
    zero_speed_spin_ = new QSpinBox(this);
    zero_speed_spin_->setRange(1, 3000);
    zero_speed_spin_->setSingleStep(5);
    zero_speed_spin_->setValue(30);
    zero_speed_spin_->setSuffix(" RPM");
    zero_speed_spin_->setFixedWidth(96);
    zero_speed_spin_->setFixedHeight(26);
    zero_speed_spin_->setToolTip("回零时的目标转速 (碰撞/堵转检测速度)。修改后会立即发送。");

    auto *zeroSpeedLabel = new QLabel("归零速度:", this);
    zeroSpeedLabel->setStyleSheet("color: #607d8b; font-size: 11px;");

    // 零点停留时长（到达零点后等待此时间再移动到安全位）
    zero_dwell_spin_ = new QSpinBox(this);
    zero_dwell_spin_->setRange(0, 10000);
    zero_dwell_spin_->setSingleStep(100);
    zero_dwell_spin_->setValue(1000);
    zero_dwell_spin_->setSuffix(" ms");
    zero_dwell_spin_->setFixedWidth(80);
    zero_dwell_spin_->setFixedHeight(26);
    zero_dwell_spin_->setToolTip("到达零点后停留时间，再移动到安全位");

    auto *dwellLabel = new QLabel("零点停留:", this);
    dwellLabel->setStyleSheet("color: #607d8b; font-size: 11px;");

    btnRow->addWidget(btn_estop_);
    btnRow->addWidget(btn_home_);
    btnRow->addSpacing(12);
    btnRow->addWidget(moveSpeedLabel);
    btnRow->addWidget(move_speed_spin_);
    btnRow->addSpacing(8);
    btnRow->addWidget(zeroSpeedLabel);
    btnRow->addWidget(zero_speed_spin_);
    btnRow->addSpacing(8);
    btnRow->addWidget(dwellLabel);
    btnRow->addWidget(zero_dwell_spin_);
    btnRow->addStretch();

    mainLayout->addLayout(btnRow);

    // ── Status label ─────────────────────────────────────────────────────────
    status_label_ = new QLabel("就绪", this);
    status_label_->setStyleSheet("color: #8aa1b4;");
    mainLayout->addWidget(status_label_);

    connect(btn_estop_,   &QPushButton::clicked, this, &ArmWidget::onEstop);
    connect(btn_home_,    &QPushButton::clicked, this, &ArmWidget::onHome);
    connect(move_speed_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ArmWidget::onSpeedsChanged);
    connect(zero_speed_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ArmWidget::onSpeedsChanged);
}

// Sequential home: each axis homes then moves to its safe position
void ArmWidget::onHome()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    // Full sequence on server side: for each joint — stall home → set zero → move to safe position
    status_label_->setText("全轴堵转回零 + 移动到安全位中...");
    setPerAxisButtonsEnabled(false);

    QJsonArray safeDegrees;
    for (int i = 0; i < 6; ++i)
        safeDegrees.append(safe_spins_[i]->value());

    QJsonObject params;
    params["action"]       = QString("home_seq");
    params["safe_degrees"] = safeDegrees;
    // keep j2_safe_deg for backward compat with server
    params["j2_safe_deg"]  = safe_spins_[1]->value();

    rpc_->call(Protocol::Methods::ARM_HOME, params,
               [this](QJsonObject result) {
        setPerAxisButtonsEnabled(true);
        const bool ok = result.value("ok").toBool(false);
        if (!ok) {
            status_label_->setText("回零失败: " + result.value("error").toString("未知错误"));
            return;
        }
        // Server completed: all joints homed and moved to safe positions
        for (int i = 0; i < 6 && i < safe_spins_.size(); ++i)
            setCurrentAngleValue(i, safe_spins_[i]->value());
        status_label_->setText("全轴已就位 (堵转回零 → 安全位完成)");
    });
}

void ArmWidget::onHomeAxis()
{
    if (!rpc_ || !rpc_->isConnected()) return;

    auto *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    const int axis = btn->property("axis_index").toInt();
    if (axis < 0 || axis >= target_spins_.size()) return;

    const int jointIndex = axis + 1;
    // Phase 1: homing (stall detection + zero calibration)
    status_label_->setText(QString("J%1 堵转回零中...").arg(jointIndex));
    setPerAxisButtonsEnabled(false);

    QJsonObject params;
    params["joint_index"] = jointIndex;
    params["joint"] = jointIndex;
    rpc_->call(Protocol::Methods::ARM_HOME_JOINT, params,
               [this, axis, jointIndex](QJsonObject result) {
        const bool ok = result.value("ok").toBool(false);
        if (!ok) {
            setPerAxisButtonsEnabled(true);
            status_label_->setText(QString("J%1 回零失败").arg(jointIndex));
            return;
        }

        // Phase 2: zero reached — update status and display 0° immediately
        status_label_->setText(QString("J%1 已到达零点 (0.0°)").arg(jointIndex));
        setCurrentAngleValue(axis, 0.0);  // motor defines this position as zero

        // Phase 3: wait the user-configured dwell time, then move to safe position
        const double safeTarget = safe_spins_[axis]->value();
        const int dwellMs = zero_dwell_spin_->value();
        QTimer::singleShot(dwellMs, this, [this, axis, jointIndex, safeTarget]() {
            if (!rpc_ || !rpc_->isConnected()) {
                setPerAxisButtonsEnabled(true);
                return;
            }
            status_label_->setText(
                QString("J%1 正在移动到安全位 %2°...").arg(jointIndex).arg(safeTarget, 0, 'f', 1));

            QJsonObject moveParams;
            moveParams["joint_index"] = jointIndex;
            moveParams["joint"] = jointIndex;
            moveParams["target_deg"] = safeTarget;
            rpc_->call(Protocol::Methods::ARM_MOVE_JOINT, moveParams,
                       [this, jointIndex, safeTarget](QJsonObject moveResult) {
                const bool moveOk = moveResult.value("ok").toBool(false);
                setPerAxisButtonsEnabled(true);
                status_label_->setText(
                    moveOk
                        ? QString("J%1 已就位 %2°").arg(jointIndex).arg(safeTarget, 0, 'f', 1)
                        : QString("J%1 已回零，移动到安全位失败").arg(jointIndex));
            });
        });
    });
}

void ArmWidget::onMoveAxis()
{
    if (!rpc_ || !rpc_->isConnected()) return;

    auto *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    const int axis = btn->property("axis_index").toInt();
    if (axis < 0 || axis >= target_spins_.size()) return;

    const int jointIndex = axis + 1;
    const double targetDeg = target_spins_[axis]->value();
    status_label_->setText(QString("J%1 正在运动到 %2°...").arg(jointIndex).arg(targetDeg, 0, 'f', 1));
    setPerAxisButtonsEnabled(false);

    QJsonObject params;
    params["joint_index"] = jointIndex;
    params["joint"] = jointIndex;
    params["target_deg"] = targetDeg;
    rpc_->call(Protocol::Methods::ARM_MOVE_JOINT, params,
               [this, jointIndex, targetDeg](QJsonObject result) {
        const bool ok = result.value("ok").toBool(false);
        setPerAxisButtonsEnabled(true);
        status_label_->setText(
            ok
                ? QString("J%1 已运动到 %2°").arg(jointIndex).arg(targetDeg, 0, 'f', 1)
                : QString("J%1 单轴运动失败").arg(jointIndex));
    });
}

void ArmWidget::onSpeedsChanged()
{
    // The user just edited one of the speed spin boxes - immediately push
    // the new pair to the backend so subsequent move/home commands honor it.
    pushSpeeds();
}

void ArmWidget::pushSpeeds()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    if (!move_speed_spin_ || !zero_speed_spin_) return;

    const int moveRpm = move_speed_spin_->value();
    const int zeroRpm = zero_speed_spin_->value();

    QJsonObject params;
    params["move_rpm"] = moveRpm;
    params["zero_rpm"] = zeroRpm;
    rpc_->call(Protocol::Methods::ARM_SET_SPEEDS, params,
               [this, moveRpm, zeroRpm](QJsonObject result) {
        if (result.value("ok").toBool(false)) {
            status_label_->setText(
                QString("速度已更新: 移动=%1 RPM, 归零=%2 RPM").arg(moveRpm).arg(zeroRpm));
        } else {
            status_label_->setText("速度更新失败");
        }
    });
}

void ArmWidget::onEstop()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    rpc_->call(Protocol::Methods::ARM_STOP, QJsonObject{},
               [this](QJsonObject result) {
        status_label_->setText(result.value("ok").toBool(false) ? "急停完成" : "急停失败");
    });
}

void ArmWidget::requestAngles()
{
    if (!rpc_ || !rpc_->isConnected() || angle_request_in_flight_) return;

    angle_request_in_flight_ = true;
    rpc_->call(Protocol::Methods::ARM_GET_ANGLES, QJsonObject{},
               [this](QJsonObject result) {
        angle_request_in_flight_ = false;
        const bool ok = result.value("ok").toBool(false);
        if (!ok) return;

        const QJsonArray angles = result.value(Protocol::Fields::ANGLES).toArray();
        if (angles.size() < 6) return;
        for (int i = 0; i < 6; ++i)
            setCurrentAngleValue(i, angles.at(i).toDouble());
    });
}

void ArmWidget::setCurrentAngleValue(int axis, double degrees)
{
    if (axis < 0 || axis >= current_labels_.size()) return;
    current_labels_[axis]->setText(QString("%1°").arg(degrees, 7, 'f', 1));
}

void ArmWidget::setPerAxisButtonsEnabled(bool enabled)
{
    for (auto *button : axis_home_buttons_) {
        if (button) button->setEnabled(enabled);
    }
    for (auto *button : axis_move_buttons_) {
        if (button) button->setEnabled(enabled);
    }
}

void ArmWidget::loadConfig(QSettings &s)
{
    s.beginGroup("ArmWidget");
    if (zero_dwell_spin_) {
        zero_dwell_spin_->setValue(s.value("zero_dwell_ms", zero_dwell_spin_->value()).toInt());
    }
    if (move_speed_spin_) {
        move_speed_spin_->setValue(s.value("move_rpm", move_speed_spin_->value()).toInt());
    }
    if (zero_speed_spin_) {
        zero_speed_spin_->setValue(s.value("zero_rpm", zero_speed_spin_->value()).toInt());
    }
    for (int i = 0; i < target_spins_.size(); ++i) {
        if (!target_spins_[i]) continue;
        const QString key = QString("target_axis%1").arg(i + 1);
        target_spins_[i]->setValue(s.value(key, target_spins_[i]->value()).toDouble());
    }
    for (int i = 0; i < safe_spins_.size(); ++i) {
        if (!safe_spins_[i]) continue;
        const QString key = QString("safe_axis%1").arg(i + 1);
        safe_spins_[i]->setValue(s.value(key, safe_spins_[i]->value()).toDouble());
    }
    s.endGroup();
}

void ArmWidget::saveConfig(QSettings &s) const
{
    s.beginGroup("ArmWidget");
    if (zero_dwell_spin_) {
        s.setValue("zero_dwell_ms", zero_dwell_spin_->value());
    }
    if (move_speed_spin_) {
        s.setValue("move_rpm", move_speed_spin_->value());
    }
    if (zero_speed_spin_) {
        s.setValue("zero_rpm", zero_speed_spin_->value());
    }
    for (int i = 0; i < target_spins_.size(); ++i) {
        if (!target_spins_[i]) continue;
        s.setValue(QString("target_axis%1").arg(i + 1), target_spins_[i]->value());
    }
    for (int i = 0; i < safe_spins_.size(); ++i) {
        if (!safe_spins_[i]) continue;
        s.setValue(QString("safe_axis%1").arg(i + 1), safe_spins_[i]->value());
    }
    s.endGroup();
}
