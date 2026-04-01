#include "ArmWidget.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
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
    grid->setSpacing(6);
    grid->setHorizontalSpacing(10);

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
    makeHdr("回零", 4, Qt::AlignCenter);

    const QStringList names = {"J1", "J2", "J3", "J4", "J5", "J6"};

    for (int i = 0; i < 6; ++i) {
        const int row = i + 1;

        // axis label
        auto *axLabel = new QLabel(names[i], this);
        axLabel->setFixedWidth(36);
        axLabel->setAlignment(Qt::AlignCenter);
        axLabel->setStyleSheet("font-family: Consolas; font-weight: bold; color: #00c8d7;");

        // current angle label
        auto *curLabel = new QLabel("--.-°", this);
        curLabel->setMinimumWidth(88);
        curLabel->setAlignment(Qt::AlignCenter);
        curLabel->setStyleSheet("color: #9fb3c8; font-family: Consolas;");

        // target angle spinbox
        auto *safeSpin = new QDoubleSpinBox(this);
        safeSpin->setRange(-360.0, 360.0);
        safeSpin->setSingleStep(1.0);
        safeSpin->setDecimals(1);
        safeSpin->setValue(0.0);
        safeSpin->setSuffix("°");
        safeSpin->setAlignment(Qt::AlignCenter);
        safeSpin->setFixedWidth(96);

        auto *moveBtn = new QPushButton("移动", this);
        auto *homeBtn = new QPushButton("回零", this);
        homeBtn->setProperty("axis_index", i);
        moveBtn->setProperty("axis_index", i);
        homeBtn->setFixedHeight(28);
        moveBtn->setFixedHeight(28);
        homeBtn->setFixedWidth(72);
        moveBtn->setFixedWidth(72);

        target_spins_.append(safeSpin);
        current_labels_.append(curLabel);
        axis_home_buttons_.append(homeBtn);
        axis_move_buttons_.append(moveBtn);

        grid->addWidget(axLabel, row, 0);
        grid->addWidget(curLabel, row, 1);
        grid->addWidget(safeSpin, row, 2);
        grid->addWidget(moveBtn, row, 3);
        grid->addWidget(homeBtn, row, 4);

        connect(homeBtn, &QPushButton::clicked, this, &ArmWidget::onHomeAxis);
        connect(moveBtn, &QPushButton::clicked, this, &ArmWidget::onMoveAxis);
    }

    grid->setColumnStretch(5, 1);
    mainLayout->addLayout(grid);

    // ── Button row ───────────────────────────────────────────────────────────
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);

    btn_enable_  = new QPushButton("使能",   this);
    btn_disable_ = new QPushButton("失能",   this);
    btn_estop_   = new QPushButton("急停",   this);
    btn_home_    = new QPushButton("全部回零", this);

    for (auto *b : {btn_enable_, btn_disable_, btn_estop_, btn_home_})
        b->setFixedHeight(28);

    btn_estop_->setStyleSheet(
        "QPushButton { background: #c0392b; color: white; font-weight: bold; }"
        "QPushButton:hover { background: #e74c3c; }"
        "QPushButton:pressed { background: #962d22; }"
    );

    btnRow->addWidget(btn_enable_);
    btnRow->addWidget(btn_disable_);
    btnRow->addWidget(btn_estop_);
    btnRow->addWidget(btn_home_);
    btnRow->addStretch();

    mainLayout->addLayout(btnRow);

    // ── Status label ─────────────────────────────────────────────────────────
    status_label_ = new QLabel("就绪", this);
    status_label_->setStyleSheet("color: #8aa1b4;");
    mainLayout->addWidget(status_label_);

    connect(btn_enable_,  &QPushButton::clicked, this, &ArmWidget::onEnable);
    connect(btn_disable_, &QPushButton::clicked, this, &ArmWidget::onDisable);
    connect(btn_estop_,   &QPushButton::clicked, this, &ArmWidget::onEstop);
    connect(btn_home_,    &QPushButton::clicked, this, &ArmWidget::onHome);
}

// Sequential home: each axis homes then moves to its safe position
void ArmWidget::onHome()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    status_label_->setText("回零中...");

    QJsonArray safeDegrees;
    for (int i = 0; i < 6; ++i)
        safeDegrees.append(target_spins_[i]->value());

    QJsonObject params;
    params["action"]       = QString("home_seq");
    params["safe_degrees"] = safeDegrees;
    // keep j2_safe_deg for backward compat with server
    params["j2_safe_deg"]  = target_spins_[1]->value();

    rpc_->call(Protocol::Methods::ARM_HOME, params,
               [this](QJsonObject result) {
        const bool ok = result.value("ok").toBool(false);
        if (!ok) {
            status_label_->setText("回零失败: " + result.value("error").toString("未知错误"));
            return;
        }
        status_label_->setText("回零完成");
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
    status_label_->setText(QString("J%1 单轴回零中...").arg(jointIndex));
    setPerAxisButtonsEnabled(false);

    QJsonObject params;
    params["joint_index"] = jointIndex;
    params["joint"] = jointIndex;
    rpc_->call(Protocol::Methods::ARM_HOME_JOINT, params,
               [this, axis, jointIndex](QJsonObject result) {
        const bool ok = result.value("ok").toBool(false);
        if (!ok) {
            setPerAxisButtonsEnabled(true);
            status_label_->setText(QString("J%1 单轴回零失败").arg(jointIndex));
            return;
        }

        const double safeTarget = target_spins_[axis]->value();
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
                    ? QString("J%1 已回零并移动到 %2°").arg(jointIndex).arg(safeTarget, 0, 'f', 1)
                    : QString("J%1 已回零，但移动到安全位失败").arg(jointIndex));
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

void ArmWidget::onEnable()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject params;
    params["enable"] = true;
    rpc_->call("arm.enable", params,
               [this](QJsonObject result) {
        status_label_->setText(result.value("ok").toBool(false) ? "使能成功" : "使能失败");
    });
}

void ArmWidget::onDisable()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject params;
    params["enable"] = false;
    rpc_->call("arm.enable", params,
               [this](QJsonObject result) {
        status_label_->setText(result.value("ok").toBool(false) ? "失能成功" : "失能失败");
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
    current_labels_[axis]->setText(QString("%1°").arg(degrees, 6, 'f', 1));
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
