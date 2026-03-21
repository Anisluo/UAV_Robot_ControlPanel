#include "ArmWidget.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QJsonArray>
#include <QJsonObject>

ArmWidget::ArmWidget(RpcClient *rpc, QWidget *parent)
    : QGroupBox("六轴机械臂 [CAN]", parent)
    , rpc_(rpc)
    , debounce_timer_(new QTimer(this))
{
    debounce_timer_->setSingleShot(true);
    debounce_timer_->setInterval(150);
    connect(debounce_timer_, &QTimer::timeout, this, &ArmWidget::sendJoints);

    buildUi();
}

void ArmWidget::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(8, 18, 8, 8);

    auto *grid = new QGridLayout;
    grid->setSpacing(6);

    const QStringList jointNames = {"J1", "J2", "J3", "J4", "J5", "J6"};

    for (int i = 0; i < 6; ++i) {
        auto *label = new QLabel(jointNames[i], this);
        label->setFixedWidth(24);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setStyleSheet("font-family: Consolas; font-weight: bold; color: #00c8d7;");

        auto *slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(-180, 180);
        slider->setValue(0);
        slider->setTickInterval(30);

        auto *spinbox = new QDoubleSpinBox(this);
        spinbox->setRange(-180.0, 180.0);
        spinbox->setSingleStep(1.0);
        spinbox->setDecimals(1);
        spinbox->setValue(0.0);
        spinbox->setFixedWidth(80);

        auto *degLabel = new QLabel("°", this);
        degLabel->setFixedWidth(14);

        sliders_.append(slider);
        spinboxes_.append(spinbox);

        int axis = i;
        connect(slider, &QSlider::valueChanged, this, [this, axis](int v) {
            onSliderChanged(axis, v);
        });
        connect(spinbox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, axis](double v) {
            onSpinboxChanged(axis, v);
        });

        grid->addWidget(label,   i, 0);
        grid->addWidget(slider,  i, 1);
        grid->addWidget(spinbox, i, 2);
        grid->addWidget(degLabel, i, 3);
    }

    grid->setColumnStretch(1, 1);
    mainLayout->addLayout(grid);

    // Buttons row
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);

    btn_home_    = new QPushButton("回零",    this);
    btn_enable_  = new QPushButton("使能",  this);
    btn_disable_ = new QPushButton("失能", this);

    btn_home_->setFixedHeight(28);
    btn_enable_->setFixedHeight(28);
    btn_disable_->setFixedHeight(28);

    btnRow->addWidget(btn_home_);
    btnRow->addWidget(btn_enable_);
    btnRow->addWidget(btn_disable_);
    btnRow->addStretch();

    mainLayout->addLayout(btnRow);

    connect(btn_home_,    &QPushButton::clicked, this, &ArmWidget::onHome);
    connect(btn_enable_,  &QPushButton::clicked, this, &ArmWidget::onEnable);
    connect(btn_disable_, &QPushButton::clicked, this, &ArmWidget::onDisable);
}

void ArmWidget::onSliderChanged(int axis, int value)
{
    QDoubleSpinBox *sb = spinboxes_[axis];
    bool blocked = sb->blockSignals(true);
    sb->setValue(static_cast<double>(value));
    sb->blockSignals(blocked);
    debounce_timer_->start();
}

void ArmWidget::onSpinboxChanged(int axis, double value)
{
    QSlider *sl = sliders_[axis];
    bool blocked = sl->blockSignals(true);
    sl->setValue(static_cast<int>(value));
    sl->blockSignals(blocked);
    debounce_timer_->start();
}

void ArmWidget::sendJoints()
{
    if (!rpc_ || !rpc_->isConnected()) return;

    QJsonArray joints;
    for (int i = 0; i < 6; ++i) {
        joints.append(spinboxes_[i]->value());
    }
    QJsonObject params;
    params[Protocol::Fields::JOINTS] = joints;
    rpc_->call(Protocol::Methods::ARM_SET_JOINTS, params);
}

void ArmWidget::onHome()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    rpc_->call(Protocol::Methods::ARM_HOME, QJsonObject{});
}

void ArmWidget::onEnable()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject params;
    params["enable"] = true;
    rpc_->call("arm.enable", params);
}

void ArmWidget::onDisable()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject params;
    params["enable"] = false;
    rpc_->call("arm.enable", params);
}

void ArmWidget::setJointValue(int axis, double degrees, bool blockSig)
{
    Q_UNUSED(blockSig)
    QSlider *sl = sliders_[axis];
    QDoubleSpinBox *sb = spinboxes_[axis];

    bool bs1 = sl->blockSignals(true);
    bool bs2 = sb->blockSignals(true);
    sl->setValue(static_cast<int>(degrees));
    sb->setValue(degrees);
    sl->blockSignals(bs1);
    sb->blockSignals(bs2);
}

double ArmWidget::jointValue(int axis) const
{
    return spinboxes_[axis]->value();
}
