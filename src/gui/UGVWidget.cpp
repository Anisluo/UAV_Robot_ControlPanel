#include "UGVWidget.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QJsonObject>

UGVWidget::UGVWidget(RpcClient *rpc, QWidget *parent)
    : QGroupBox("六轮无人车 [CAN]", parent)
    , rpc_(rpc)
{
    buildUi();
}

void UGVWidget::buildUi()
{
    auto *mainLayout = new QVBoxLayout;
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(8, 18, 8, 8);
    setLayout(mainLayout);

    auto *hint = new QLabel("差速底盘: 前后=Vx, 左右=转向, 不支持横移", this);
    hint->setStyleSheet("color: #9ad7de; font-family: Consolas; font-size: 12px;");
    mainLayout->addWidget(hint);

    auto *grid = new QGridLayout;
    grid->setSpacing(6);

    auto *vxLabel = new QLabel("Vx (m/s)", this);
    vxLabel->setStyleSheet("color: #00c8d7; font-family: Consolas;");
    vx_slider_ = new QSlider(Qt::Horizontal, this);
    vx_slider_->setRange(-100, 100);
    vx_slider_->setValue(20);
    vx_spin_ = new QDoubleSpinBox(this);
    vx_spin_->setRange(-1.0, 1.0);
    vx_spin_->setSingleStep(0.05);
    vx_spin_->setDecimals(2);
    vx_spin_->setValue(0.20);
    vx_spin_->setFixedWidth(85);

    auto *omegaLabel = new QLabel("ω (rad/s)", this);
    omegaLabel->setStyleSheet("color: #00c8d7; font-family: Consolas;");
    omega_slider_ = new QSlider(Qt::Horizontal, this);
    omega_slider_->setRange(-314, 314);
    omega_slider_->setValue(60);
    omega_spin_ = new QDoubleSpinBox(this);
    omega_spin_->setRange(-3.14, 3.14);
    omega_spin_->setSingleStep(0.05);
    omega_spin_->setDecimals(3);
    omega_spin_->setValue(0.60);
    omega_spin_->setFixedWidth(85);

    grid->addWidget(vxLabel,       0, 0);
    grid->addWidget(vx_slider_,    0, 1);
    grid->addWidget(vx_spin_,      0, 2);
    grid->addWidget(omegaLabel,    1, 0);
    grid->addWidget(omega_slider_, 1, 1);
    grid->addWidget(omega_spin_,   1, 2);
    grid->setColumnStretch(1, 1);
    mainLayout->addLayout(grid);

    auto *pad = new QGridLayout;
    pad->setSpacing(8);

    btn_forward_ = new QPushButton("前进", this);
    btn_backward_ = new QPushButton("后退", this);
    btn_turn_left_ = new QPushButton("左转", this);
    btn_turn_right_ = new QPushButton("右转", this);
    btn_stop_ = new QPushButton("停止", this);
    btn_send_ = new QPushButton("发送当前速度", this);

    btn_stop_->setObjectName("stopButton");
    btn_forward_->setFixedHeight(34);
    btn_backward_->setFixedHeight(34);
    btn_turn_left_->setFixedHeight(34);
    btn_turn_right_->setFixedHeight(34);
    btn_stop_->setFixedHeight(34);
    btn_send_->setFixedHeight(34);

    pad->addWidget(btn_forward_,    0, 1);
    pad->addWidget(btn_turn_left_,  1, 0);
    pad->addWidget(btn_stop_,       1, 1);
    pad->addWidget(btn_turn_right_, 1, 2);
    pad->addWidget(btn_backward_,   2, 1);
    mainLayout->addLayout(pad);
    mainLayout->addWidget(btn_send_);

    connect(vx_slider_, &QSlider::valueChanged, this, &UGVWidget::onVxSliderChanged);
    connect(vx_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &UGVWidget::onVxSpinChanged);
    connect(omega_slider_, &QSlider::valueChanged, this, &UGVWidget::onOmegaSliderChanged);
    connect(omega_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &UGVWidget::onOmegaSpinChanged);

    connect(btn_send_, &QPushButton::clicked, this, [this]() { sendVelocity(); });
    connect(btn_stop_, &QPushButton::clicked, this, &UGVWidget::onStop);

    connect(btn_forward_, &QPushButton::clicked, this, [this]() {
        sendVelocity(vx_spin_->value(), 0.0);
    });
    connect(btn_backward_, &QPushButton::clicked, this, [this]() {
        sendVelocity(-vx_spin_->value(), 0.0);
    });
    connect(btn_turn_left_, &QPushButton::clicked, this, [this]() {
        sendVelocity(0.0, omega_spin_->value());
    });
    connect(btn_turn_right_, &QPushButton::clicked, this, [this]() {
        sendVelocity(0.0, -omega_spin_->value());
    });
}

void UGVWidget::onVxSliderChanged(int value)
{
    double v = value / 100.0;
    bool blocked = vx_spin_->blockSignals(true);
    vx_spin_->setValue(v);
    vx_spin_->blockSignals(blocked);
}

void UGVWidget::onVxSpinChanged(double value)
{
    bool blocked = vx_slider_->blockSignals(true);
    vx_slider_->setValue(static_cast<int>(value * 100));
    vx_slider_->blockSignals(blocked);
}

void UGVWidget::onOmegaSliderChanged(int value)
{
    double v = value / 100.0;
    bool blocked = omega_spin_->blockSignals(true);
    omega_spin_->setValue(v);
    omega_spin_->blockSignals(blocked);
}

void UGVWidget::onOmegaSpinChanged(double value)
{
    bool blocked = omega_slider_->blockSignals(true);
    omega_slider_->setValue(static_cast<int>(value * 100));
    omega_slider_->blockSignals(blocked);
}

void UGVWidget::sendVelocity(double vx, double omega)
{
    if (!rpc_ || !rpc_->isConnected()) return;

    QJsonObject params;
    params[Protocol::Fields::VX]    = vx;
    params[Protocol::Fields::VY]    = 0.0;
    params[Protocol::Fields::OMEGA] = omega;
    rpc_->call(Protocol::Methods::UGV_SET_VELOCITY, params);
}

void UGVWidget::sendVelocity()
{
    sendVelocity(vx_spin_->value(), omega_spin_->value());
}

void UGVWidget::resetInputs()
{
    bool b1 = vx_slider_->blockSignals(true);
    bool b2 = vx_spin_->blockSignals(true);
    bool b3 = omega_slider_->blockSignals(true);
    bool b4 = omega_spin_->blockSignals(true);

    vx_slider_->setValue(0);
    vx_spin_->setValue(0.0);
    omega_slider_->setValue(0);
    omega_spin_->setValue(0.0);

    vx_slider_->blockSignals(b1);
    vx_spin_->blockSignals(b2);
    omega_slider_->blockSignals(b3);
    omega_spin_->blockSignals(b4);
}

void UGVWidget::onStop()
{
    if (rpc_ && rpc_->isConnected()) {
        rpc_->call(Protocol::Methods::UGV_STOP, QJsonObject{});
    }
}
