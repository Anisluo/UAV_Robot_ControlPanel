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
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(8, 18, 8, 8);

    auto *grid = new QGridLayout;
    grid->setSpacing(6);

    // Vx row
    auto *vxLabel = new QLabel("Vx (m/s)", this);
    vxLabel->setStyleSheet("color: #00c8d7; font-family: Consolas;");
    vx_slider_ = new QSlider(Qt::Horizontal, this);
    vx_slider_->setRange(-100, 100);
    vx_slider_->setValue(0);
    vx_spin_ = new QDoubleSpinBox(this);
    vx_spin_->setRange(-1.0, 1.0);
    vx_spin_->setSingleStep(0.05);
    vx_spin_->setDecimals(2);
    vx_spin_->setValue(0.0);
    vx_spin_->setFixedWidth(85);

    // Vy row
    auto *vyLabel = new QLabel("Vy (m/s)", this);
    vyLabel->setStyleSheet("color: #00c8d7; font-family: Consolas;");
    vy_slider_ = new QSlider(Qt::Horizontal, this);
    vy_slider_->setRange(-100, 100);
    vy_slider_->setValue(0);
    vy_spin_ = new QDoubleSpinBox(this);
    vy_spin_->setRange(-1.0, 1.0);
    vy_spin_->setSingleStep(0.05);
    vy_spin_->setDecimals(2);
    vy_spin_->setValue(0.0);
    vy_spin_->setFixedWidth(85);

    // Omega row
    auto *omegaLabel = new QLabel("ω (rad/s)", this);
    omegaLabel->setStyleSheet("color: #00c8d7; font-family: Consolas;");
    omega_slider_ = new QSlider(Qt::Horizontal, this);
    omega_slider_->setRange(-314, 314);
    omega_slider_->setValue(0);
    omega_spin_ = new QDoubleSpinBox(this);
    omega_spin_->setRange(-3.14, 3.14);
    omega_spin_->setSingleStep(0.05);
    omega_spin_->setDecimals(3);
    omega_spin_->setValue(0.0);
    omega_spin_->setFixedWidth(85);

    grid->addWidget(vxLabel,      0, 0);
    grid->addWidget(vx_slider_,   0, 1);
    grid->addWidget(vx_spin_,     0, 2);
    grid->addWidget(vyLabel,      1, 0);
    grid->addWidget(vy_slider_,   1, 1);
    grid->addWidget(vy_spin_,     1, 2);
    grid->addWidget(omegaLabel,   2, 0);
    grid->addWidget(omega_slider_, 2, 1);
    grid->addWidget(omega_spin_,  2, 2);
    grid->setColumnStretch(1, 1);

    mainLayout->addLayout(grid);

    // Buttons
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);

    btn_send_ = new QPushButton("发送速度", this);
    btn_stop_ = new QPushButton("急停", this);
    btn_stop_->setObjectName("stopButton");
    btn_stop_->setFixedHeight(36);

    btnRow->addWidget(btn_send_);
    btnRow->addWidget(btn_stop_);
    mainLayout->addLayout(btnRow);

    // Connect sliders
    connect(vx_slider_, &QSlider::valueChanged, this, &UGVWidget::onVxSliderChanged);
    connect(vx_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &UGVWidget::onVxSpinChanged);

    connect(vy_slider_, &QSlider::valueChanged, this, &UGVWidget::onVySliderChanged);
    connect(vy_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &UGVWidget::onVySpinChanged);

    connect(omega_slider_, &QSlider::valueChanged, this, &UGVWidget::onOmegaSliderChanged);
    connect(omega_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &UGVWidget::onOmegaSpinChanged);

    connect(btn_send_, &QPushButton::clicked, this, &UGVWidget::sendVelocity);
    connect(btn_stop_, &QPushButton::clicked, this, &UGVWidget::onStop);
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

void UGVWidget::onVySliderChanged(int value)
{
    double v = value / 100.0;
    bool blocked = vy_spin_->blockSignals(true);
    vy_spin_->setValue(v);
    vy_spin_->blockSignals(blocked);
}

void UGVWidget::onVySpinChanged(double value)
{
    bool blocked = vy_slider_->blockSignals(true);
    vy_slider_->setValue(static_cast<int>(value * 100));
    vy_slider_->blockSignals(blocked);
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

void UGVWidget::sendVelocity()
{
    if (!rpc_ || !rpc_->isConnected()) return;

    QJsonObject params;
    params[Protocol::Fields::VX]    = vx_spin_->value();
    params[Protocol::Fields::VY]    = vy_spin_->value();
    params[Protocol::Fields::OMEGA] = omega_spin_->value();
    rpc_->call(Protocol::Methods::UGV_SET_VELOCITY, params);
}

void UGVWidget::onStop()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    rpc_->call(Protocol::Methods::UGV_STOP, QJsonObject{});

    // Reset sliders to zero
    bool b1 = vx_slider_->blockSignals(true);
    bool b2 = vx_spin_->blockSignals(true);
    bool b3 = vy_slider_->blockSignals(true);
    bool b4 = vy_spin_->blockSignals(true);
    bool b5 = omega_slider_->blockSignals(true);
    bool b6 = omega_spin_->blockSignals(true);

    vx_slider_->setValue(0);
    vx_spin_->setValue(0.0);
    vy_slider_->setValue(0);
    vy_spin_->setValue(0.0);
    omega_slider_->setValue(0);
    omega_spin_->setValue(0.0);

    vx_slider_->blockSignals(b1);
    vx_spin_->blockSignals(b2);
    vy_slider_->blockSignals(b3);
    vy_spin_->blockSignals(b4);
    omega_slider_->blockSignals(b5);
    omega_spin_->blockSignals(b6);
}
