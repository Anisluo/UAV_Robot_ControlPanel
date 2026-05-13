#include "AirportWidget.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

AirportWidget::AirportWidget(RpcClient *rpc, QWidget *parent)
    : QGroupBox("机场平台 [CAN1 / ZDT]", parent)
    , rpc_(rpc)
{
    buildUi();
}

void AirportWidget::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(8, 18, 8, 8);

    auto makeTitle = [this](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setStyleSheet("color: #00c8d7; font-family: Consolas; font-weight: bold;");
        return label;
    };

    auto makePanel = [this]() {
        auto *frame = new QFrame(this);
        frame->setFrameShape(QFrame::StyledPanel);
        frame->setStyleSheet("QFrame { border: 1px solid #2d3a52; border-radius: 6px; }");
        return frame;
    };

    auto *pairPanel = makePanel();
    auto *pairLayout = new QVBoxLayout(pairPanel);
    pairLayout->setSpacing(6);

    pairLayout->addWidget(makeTitle("导轨1 + 导轨3 联动"));

    auto *pairGrid = new QGridLayout;
    pairGrid->setHorizontalSpacing(6);
    pairGrid->setVerticalSpacing(6);

    auto *pairLabel = new QLabel("共用转速", this);
    pairLabel->setFixedWidth(65);

    lock_slider_ = new QSlider(Qt::Horizontal, this);
    lock_slider_->setRange(0, 1500);
    lock_slider_->setValue(300);

    lock_spin_ = new QSpinBox(this);
    lock_spin_->setRange(0, 1500);
    lock_spin_->setSuffix(" rpm");
    lock_spin_->setValue(300);
    lock_spin_->setFixedWidth(90);

    lock_btn_ = new QPushButton("锁定", this);
    lock_btn_->setFixedHeight(28);
    release_btn_ = new QPushButton("释放", this);
    release_btn_->setFixedHeight(28);

    pairGrid->addWidget(pairLabel, 0, 0);
    pairGrid->addWidget(lock_slider_, 0, 1);
    pairGrid->addWidget(lock_spin_, 0, 2);
    pairGrid->addWidget(lock_btn_, 0, 3);
    pairGrid->addWidget(release_btn_, 0, 4);
    pairGrid->setColumnStretch(1, 1);
    pairLayout->addLayout(pairGrid);

    auto *pairHint = new QLabel("锁定: 导轨1/3 正向运行；释放: 导轨1/3 反向运行。后端会轮询驱动堵转状态，检测到堵转保护后自动停止对应导轨。", this);
    pairHint->setWordWrap(true);
    pairHint->setStyleSheet("color: #888aaa;");
    pairLayout->addWidget(pairHint);

    mainLayout->addWidget(pairPanel);

    auto *rail2Panel = makePanel();
    auto *rail2Layout = new QVBoxLayout(rail2Panel);
    rail2Layout->setSpacing(6);
    rail2Layout->addWidget(makeTitle("导轨2 单独控制"));

    auto *rail2Grid = new QGridLayout;
    rail2Grid->setHorizontalSpacing(6);
    rail2Grid->setVerticalSpacing(6);

    auto *rail2Label = new QLabel("导轨2 rpm", this);
    rail2Label->setFixedWidth(65);

    rail2_slider_ = new QSlider(Qt::Horizontal, this);
    rail2_slider_->setRange(0, 1500);
    rail2_slider_->setValue(300);

    rail2_spin_ = new QSpinBox(this);
    rail2_spin_->setRange(0, 1500);
    rail2_spin_->setSuffix(" rpm");
    rail2_spin_->setValue(300);
    rail2_spin_->setFixedWidth(90);

    rail2_fwd_btn_ = new QPushButton("前进", this);
    rail2_fwd_btn_->setFixedHeight(28);
    rail2_back_btn_ = new QPushButton("后退", this);
    rail2_back_btn_->setFixedHeight(28);

    rail2Grid->addWidget(rail2Label, 0, 0);
    rail2Grid->addWidget(rail2_slider_, 0, 1);
    rail2Grid->addWidget(rail2_spin_, 0, 2);
    rail2Grid->addWidget(rail2_fwd_btn_, 0, 3);
    rail2Grid->addWidget(rail2_back_btn_, 0, 4);
    rail2Grid->setColumnStretch(1, 1);
    rail2Layout->addLayout(rail2Grid);

    auto *rail2Hint = new QLabel("导轨2 继续使用单独速度控制，前进/后退直接发送速度命令。", this);
    rail2Hint->setWordWrap(true);
    rail2Hint->setStyleSheet("color: #888aaa;");
    rail2Layout->addWidget(rail2Hint);

    mainLayout->addWidget(rail2Panel);

    // ── 继电器夹爪面板 ─────────────────────────────────────────────────
    // proc_gateway/airport.gripper drives a GPIO relay (sysfs path under
    // /sys/devices/platform/gpio-innohi/...) that mechanically opens or
    // closes the airport jaw. No motor speed / position — pure on/off.
    auto *gripperPanel = makePanel();
    auto *gripperLayout = new QVBoxLayout(gripperPanel);
    gripperLayout->setSpacing(6);
    gripperLayout->addWidget(makeTitle("机场夹爪 (继电器)"));

    auto *gripperBtnRow = new QHBoxLayout;
    gripperBtnRow->setSpacing(8);

    gripper_open_btn_ = new QPushButton("张开", this);
    gripper_open_btn_->setFixedHeight(34);
    gripper_open_btn_->setStyleSheet(
        "QPushButton { background:#3a8; color:white; font-weight:bold;"
        "              padding:4px 14px; border-radius:4px; }"
        "QPushButton:hover { background:#4ba; }"
        "QPushButton:disabled { background:#446; color:#aab; }");

    gripper_close_btn_ = new QPushButton("夹紧", this);
    gripper_close_btn_->setFixedHeight(34);
    gripper_close_btn_->setStyleSheet(
        "QPushButton { background:#c33; color:white; font-weight:bold;"
        "              padding:4px 14px; border-radius:4px; }"
        "QPushButton:hover { background:#e44; }"
        "QPushButton:disabled { background:#553; color:#aab; }");

    gripperBtnRow->addWidget(gripper_open_btn_, 1);
    gripperBtnRow->addWidget(gripper_close_btn_, 1);
    gripperBtnRow->addStretch();
    gripperLayout->addLayout(gripperBtnRow);

    auto *gripperHint = new QLabel(
        "通过 GPIO 继电器控制机场夹爪开合 (airport.gripper RPC)。无速度/位置反馈。",
        this);
    gripperHint->setWordWrap(true);
    gripperHint->setStyleSheet("color: #888aaa;");
    gripperLayout->addWidget(gripperHint);

    mainLayout->addWidget(gripperPanel);

    auto *btnRow = new QHBoxLayout;
    stop_all_btn_ = new QPushButton("全部急停", this);
    stop_all_btn_->setFixedHeight(30);
    btnRow->addWidget(stop_all_btn_);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);

    connect(lock_slider_, &QSlider::valueChanged, this, &AirportWidget::onLockSliderChanged);
    connect(lock_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &AirportWidget::onLockSpinChanged);
    connect(rail2_slider_, &QSlider::valueChanged, this, &AirportWidget::onRail2SliderChanged);
    connect(rail2_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &AirportWidget::onRail2SpinChanged);
    connect(lock_btn_, &QPushButton::clicked, this, &AirportWidget::onLock);
    connect(release_btn_, &QPushButton::clicked, this, &AirportWidget::onRelease);
    connect(rail2_fwd_btn_, &QPushButton::clicked, this, [this]() { onRail2Move(true); });
    connect(rail2_back_btn_, &QPushButton::clicked, this, [this]() { onRail2Move(false); });
    connect(stop_all_btn_, &QPushButton::clicked, this, &AirportWidget::onStopAll);
    connect(gripper_open_btn_,  &QPushButton::clicked, this, [this]() { onGripper(true);  });
    connect(gripper_close_btn_, &QPushButton::clicked, this, [this]() { onGripper(false); });
}

void AirportWidget::syncSliderAndSpin(QSlider *slider, QSpinBox *spinbox, int value)
{
    bool sliderBlocked = slider->blockSignals(true);
    bool spinBlocked = spinbox->blockSignals(true);
    slider->setValue(value);
    spinbox->setValue(value);
    slider->blockSignals(sliderBlocked);
    spinbox->blockSignals(spinBlocked);
}

void AirportWidget::onLockSliderChanged(int value)
{
    syncSliderAndSpin(lock_slider_, lock_spin_, value);
}

void AirportWidget::onLockSpinChanged(int value)
{
    syncSliderAndSpin(lock_slider_, lock_spin_, value);
}

void AirportWidget::onRail2SliderChanged(int value)
{
    syncSliderAndSpin(rail2_slider_, rail2_spin_, value);
}

void AirportWidget::onRail2SpinChanged(int value)
{
    syncSliderAndSpin(rail2_slider_, rail2_spin_, value);
}

void AirportWidget::onLock()
{
    if (!rpc_ || !rpc_->isConnected()) return;

    QJsonObject params;
    params[Protocol::Fields::SPEED_RPM] = lock_spin_->value();
    rpc_->call(Protocol::Methods::AIRPORT_LOCK, params);
}

void AirportWidget::onRelease()
{
    if (!rpc_ || !rpc_->isConnected()) return;

    QJsonObject params;
    params[Protocol::Fields::SPEED_RPM] = lock_spin_->value();
    rpc_->call(Protocol::Methods::AIRPORT_RELEASE, params);
}

void AirportWidget::onRail2Move(bool forward)
{
    if (!rpc_ || !rpc_->isConnected()) return;

    QJsonObject params;
    params[Protocol::Fields::RAIL] = 1;
    params[Protocol::Fields::SPEED_RPM] = rail2_spin_->value() * (forward ? 1 : -1);
    rpc_->call(Protocol::Methods::AIRPORT_SET_SPEED, params);
}

void AirportWidget::onStopAll()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    rpc_->call(Protocol::Methods::AIRPORT_STOP_ALL, QJsonObject{});
}

void AirportWidget::onGripper(bool open)
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject params;
    params[Protocol::Fields::OPEN] = open;
    rpc_->call(Protocol::Methods::AIRPORT_GRIPPER, params);
}
