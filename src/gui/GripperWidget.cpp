#include "GripperWidget.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QJsonObject>

GripperWidget::GripperWidget(RpcClient *rpc, QWidget *parent)
    : QGroupBox("夹爪控制", parent)
    , rpc_(rpc)
{
    buildUi();
}

void GripperWidget::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(8, 18, 8, 8);

    // ── Airport Gripper section ──
    auto *apGroup = new QGroupBox("机场夹爪 [继电器]", this);
    auto *apLayout = new QHBoxLayout(apGroup);
    apLayout->setSpacing(8);

    btn_ap_open_  = new QPushButton("张开",  apGroup);
    btn_ap_close_ = new QPushButton("闭合", apGroup);
    ap_status_label_ = new QLabel("状态: --", apGroup);
    ap_status_label_->setStyleSheet("font-family: Consolas; color: #888aaa;");

    btn_ap_open_->setFixedHeight(28);
    btn_ap_close_->setFixedHeight(28);

    apLayout->addWidget(btn_ap_open_);
    apLayout->addWidget(btn_ap_close_);
    apLayout->addWidget(ap_status_label_);
    apLayout->addStretch();

    mainLayout->addWidget(apGroup);

    // ── Arm Gripper section ──
    auto *armGroup = new QGroupBox("机械臂夹爪 [串口]", this);
    auto *armLayout = new QHBoxLayout(armGroup);
    armLayout->setSpacing(8);

    btn_arm_open_  = new QPushButton("张开", armGroup);
    btn_arm_close_ = new QPushButton("闭合", armGroup);
    arm_status_label_ = new QLabel("状态: --", armGroup);
    arm_status_label_->setStyleSheet("font-family: Consolas; color: #888aaa;");

    btn_arm_open_->setFixedHeight(28);
    btn_arm_close_->setFixedHeight(28);

    armLayout->addWidget(btn_arm_open_);
    armLayout->addWidget(btn_arm_close_);
    armLayout->addWidget(arm_status_label_);
    armLayout->addStretch();

    mainLayout->addWidget(armGroup);

    // Connections
    connect(btn_ap_open_,  &QPushButton::clicked, this, &GripperWidget::onAirportOpen);
    connect(btn_ap_close_, &QPushButton::clicked, this, &GripperWidget::onAirportClose);
    connect(btn_arm_open_,  &QPushButton::clicked, this, &GripperWidget::onArmOpen);
    connect(btn_arm_close_, &QPushButton::clicked, this, &GripperWidget::onArmClose);
}

void GripperWidget::onAirportOpen()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject params;
    params[Protocol::Fields::OPEN] = true;
    rpc_->call(Protocol::Methods::AIRPORT_GRIPPER, params);
    ap_status_label_->setText("状态: 已张开");
    ap_status_label_->setStyleSheet("font-family: Consolas; color: #4caf50;");
}

void GripperWidget::onAirportClose()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject params;
    params[Protocol::Fields::OPEN] = false;
    rpc_->call(Protocol::Methods::AIRPORT_GRIPPER, params);
    ap_status_label_->setText("状态: 已闭合");
    ap_status_label_->setStyleSheet("font-family: Consolas; color: #ff9800;");
}

void GripperWidget::onArmOpen()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject params;
    params[Protocol::Fields::OPEN] = true;
    rpc_->call(Protocol::Methods::ARM_GRIPPER_SET, params);
    arm_status_label_->setText("状态: 已张开");
    arm_status_label_->setStyleSheet("font-family: Consolas; color: #4caf50;");
}

void GripperWidget::onArmClose()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject params;
    params[Protocol::Fields::OPEN] = false;
    rpc_->call(Protocol::Methods::ARM_GRIPPER_SET, params);
    arm_status_label_->setText("状态: 已闭合");
    arm_status_label_->setStyleSheet("font-family: Consolas; color: #ff9800;");
}
