#include "GripperWidget.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QSlider>
#include <QSpinBox>
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

    auto *posLabel = new QLabel("位置:", armGroup);
    posLabel->setStyleSheet("color: #00c8d7; font-family: Consolas;");

    arm_slider_ = new QSlider(Qt::Horizontal, armGroup);
    arm_slider_->setRange(0, 100);
    arm_slider_->setValue(0);

    arm_spin_ = new QSpinBox(armGroup);
    arm_spin_->setRange(0, 100);
    arm_spin_->setSuffix("%");
    arm_spin_->setValue(0);
    arm_spin_->setFixedWidth(75);

    btn_arm_set_ = new QPushButton("设置", armGroup);
    btn_arm_set_->setFixedWidth(50);
    btn_arm_set_->setFixedHeight(28);

    armLayout->addWidget(posLabel);
    armLayout->addWidget(arm_slider_, 1);
    armLayout->addWidget(arm_spin_);
    armLayout->addWidget(btn_arm_set_);

    mainLayout->addWidget(armGroup);

    // Connections
    connect(btn_ap_open_,  &QPushButton::clicked, this, &GripperWidget::onAirportOpen);
    connect(btn_ap_close_, &QPushButton::clicked, this, &GripperWidget::onAirportClose);

    connect(arm_slider_, &QSlider::valueChanged, this, &GripperWidget::onArmSliderChanged);
    connect(arm_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &GripperWidget::onArmSpinChanged);
    connect(btn_arm_set_, &QPushButton::clicked, this, &GripperWidget::onArmGripperSet);
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

void GripperWidget::onArmSliderChanged(int value)
{
    bool blocked = arm_spin_->blockSignals(true);
    arm_spin_->setValue(value);
    arm_spin_->blockSignals(blocked);
}

void GripperWidget::onArmSpinChanged(int value)
{
    bool blocked = arm_slider_->blockSignals(true);
    arm_slider_->setValue(value);
    arm_slider_->blockSignals(blocked);
}

void GripperWidget::onArmGripperSet()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject params;
    params[Protocol::Fields::POS] = arm_spin_->value();
    rpc_->call(Protocol::Methods::ARM_GRIPPER_SET, params);
}
