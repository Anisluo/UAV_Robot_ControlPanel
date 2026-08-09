#include "AirportWidget.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

// Longest a homing run can plausibly take on the backend
// (UAV_AIRPORT_HOME_WAIT_MS caps it at 120 s there) plus slack. Past this
// the GUI stops believing the "homing" flag and hands the button back.
static constexpr qint64 kHomeWatchdogMs = 150000;

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

    lock_btn_ = new QPushButton("锁定 (+)", this);
    lock_btn_->setFixedHeight(28);
    release_btn_ = new QPushButton("释放 (−)", this);
    release_btn_->setFixedHeight(28);

    pairGrid->addWidget(pairLabel, 0, 0);
    pairGrid->addWidget(lock_slider_, 0, 1);
    pairGrid->addWidget(lock_spin_, 0, 2);
    pairGrid->addWidget(lock_btn_, 0, 3);
    pairGrid->addWidget(release_btn_, 0, 4);
    pairGrid->setColumnStretch(1, 1);
    pairLayout->addLayout(pairGrid);

    auto *pairHint = new QLabel("锁定(+) / 释放(−) 驱动导轨1+3 联动。<b>负方向统一是归零方向</b>。后端轮询驱动器堵转状态，检测到堵转保护后自动停止对应导轨。", this);
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

    rail2_fwd_btn_ = new QPushButton("正向 (+)", this);
    rail2_fwd_btn_->setFixedHeight(28);
    rail2_back_btn_ = new QPushButton("负向 (−)", this);
    rail2_back_btn_->setFixedHeight(28);

    rail2Grid->addWidget(rail2Label, 0, 0);
    rail2Grid->addWidget(rail2_slider_, 0, 1);
    rail2Grid->addWidget(rail2_spin_, 0, 2);
    rail2Grid->addWidget(rail2_fwd_btn_, 0, 3);
    rail2Grid->addWidget(rail2_back_btn_, 0, 4);
    rail2Grid->setColumnStretch(1, 1);
    rail2Layout->addLayout(rail2Grid);

    // 导轨2 归零 — deliberately its own button, not ganged with the 1/3
    // pair: different lead screw, its own hard stop, its own zero.
    auto *rail2HomeRow = new QHBoxLayout;
    rail2HomeRow->setSpacing(8);
    home_rail2_btn_ = new QPushButton("导轨2 归零", this);
    home_rail2_btn_->setFixedHeight(28);
    home_rail2_btn_->setToolTip(
        QStringLiteral("驱动导轨2 跑到自己的硬限位, 把该处编码器计数记为零点。\n"
                       "与导轨1/3 的归零互不影响。方向由 UAV_AIRPORT_RAIL2_HOME_DIR 决定。"));
    home_rail2_btn_->setStyleSheet(
        "QPushButton { background:#2f6f9f; color:white; font-weight:bold;"
        "              padding:4px 10px; border-radius:4px; }"
        "QPushButton:hover { background:#3a83b8; }"
        "QPushButton:disabled { background:#3a3d52; color:#7c7f96; }");
    home_rail2_state_ = new QLabel("未归零", this);
    home_rail2_state_->setStyleSheet("color:#e0a030; font-family: Consolas; font-weight:bold;");
    rail2HomeRow->addWidget(home_rail2_btn_);
    rail2HomeRow->addWidget(home_rail2_state_);
    rail2HomeRow->addStretch();
    rail2Layout->addLayout(rail2HomeRow);

    // 绝对位置 — 填 mm, 点按钮走到距零点该处。The gateway reads the encoder,
    // works out the delta and drives it closed-loop; we never compute the
    // delta here, so nothing can go stale between reading and commanding.
    auto *rail2GotoRow = new QHBoxLayout;
    rail2GotoRow->setSpacing(6);
    auto *gotoLabel = new QLabel("绝对位置", this);
    gotoLabel->setFixedWidth(65);
    rail2_pos_spin_ = new QDoubleSpinBox(this);
    rail2_pos_spin_->setRange(0.0, 150.0);      // 导轨2 总行程 150mm, 零点=释放端硬限位
    rail2_pos_spin_->setDecimals(1);
    rail2_pos_spin_->setSingleStep(1.0);
    rail2_pos_spin_->setValue(50.0);
    rail2_pos_spin_->setSuffix(" mm");
    rail2_pos_spin_->setFixedWidth(100);
    rail2_pos_spin_->setToolTip(
        QStringLiteral("距零点的绝对距离。零点 = 归零时撞到的那个硬限位。\n"
                       "0 就是零点本身; 不能填负数 (那是往硬限位里顶)。"));
    rail2_goto_btn_ = new QPushButton("走到该位置", this);
    rail2_goto_btn_->setFixedHeight(28);
    rail2_goto_btn_->setToolTip(
        QStringLiteral("闭环走到距零点指定 mm 处 (airport.move_to_mm)。\n"
                       "速度用上面那个 rpm。必须先归零, 否则网关会拒绝 ——\n"
                       "驱动器的多圈计数掉电不保持, 没归零的\"50mm\"只是个任意偏移。"));
    rail2_goto_btn_->setStyleSheet(
        "QPushButton { background:#2f8f6f; color:white; font-weight:bold;"
        "              padding:4px 10px; border-radius:4px; }"
        "QPushButton:hover { background:#38a882; }"
        "QPushButton:disabled { background:#3a3d52; color:#7c7f96; }");
    // Its own status label. Sharing the homing label meant an absolute move
    // overwrote "归零中…", and the homing logic read that text back to decide
    // what to display — so one feature's message silently changed another
    // feature's state.
    rail2_goto_state_ = new QLabel("—", this);
    rail2_goto_state_->setStyleSheet("color:#888aaa; font-family: Consolas;");
    rail2GotoRow->addWidget(gotoLabel);
    rail2GotoRow->addWidget(rail2_pos_spin_);
    rail2GotoRow->addWidget(rail2_goto_btn_);
    rail2GotoRow->addWidget(rail2_goto_state_);
    rail2GotoRow->addStretch();
    rail2Layout->addLayout(rail2GotoRow);

    // 加速度档位 — the single biggest lever on how fast rail 2 actually
    // moves, and it was invisible until now. The shipped default (10) is
    // near the slow end: measured on this rail, a commanded 900 rpm only
    // reached 127 rpm because 2.2 s was not enough to finish the ramp.
    // Runtime-adjustable so tuning it does not mean editing the env file
    // and restarting the gateway (which wipes every rail's homed zero).
    auto *rail2AccRow = new QHBoxLayout;
    rail2AccRow->setSpacing(6);
    auto *accLabel = new QLabel("加速度", this);
    accLabel->setFixedWidth(65);
    rail2_accel_spin_ = new QSpinBox(this);
    rail2_accel_spin_->setRange(0, 255);
    rail2_accel_spin_->setValue(255);
    rail2_accel_spin_->setFixedWidth(100);
    rail2_accel_spin_->setToolTip(
        QStringLiteral("ZDT 速度命令的加速度档位。\n"
                       "0   = 直接启动, 没有加速曲线\n"
                       "255 = 最快的斜坡 (实测 0.21s 到命令转速的 90%)\n"
                       "10  = 出厂默认, 很慢 (实测命令 900rpm 只跑出 127rpm)\n\n"
                       "只影响导轨2, 导轨1/3 不受影响。"));
    rail2_accel_btn_ = new QPushButton("应用", this);
    rail2_accel_btn_->setFixedHeight(28);
    rail2_accel_btn_->setToolTip(
        QStringLiteral("下一次运动生效。不写盘 —— 网关重启后回到 "
                       "UAV_AIRPORT_RAIL2_SPEED_ACC 的值。\n"
                       "调出满意的数字后告诉我, 再固化到配置里。"));
    rail2_accel_state_ = new QLabel("当前 —", this);
    rail2_accel_state_->setStyleSheet("color:#888aaa; font-family: Consolas;");
    rail2AccRow->addWidget(accLabel);
    rail2AccRow->addWidget(rail2_accel_spin_);
    rail2AccRow->addWidget(rail2_accel_btn_);
    rail2AccRow->addWidget(rail2_accel_state_);
    rail2AccRow->addStretch();
    rail2Layout->addLayout(rail2AccRow);

    auto *rail2Hint = new QLabel("导轨2 单独速度控制。正向(+) 远离零点，负向(−) 朝零点 —— 与 airport.move_mm 的 dist_mm 符号、以及 pos_mm 的正负完全一致。", this);
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

    // ── 归零面板 ────────────────────────────────────────────────────────
    // The drivers' multi-turn encoder count is NOT retentive — it restarts
    // from 0 whenever the rail drivers are power-cycled, and their
    // power-on auto-homing is disabled. So any absolute position is
    // meaningless until the rails have been driven to a known physical
    // reference. This runs them to the release-side hard stop and latches
    // the count there as zero.
    auto *homePanel = makePanel();
    auto *homeLayout = new QVBoxLayout(homePanel);
    homeLayout->setSpacing(6);

    auto *homeHead = new QHBoxLayout;
    homeHead->addWidget(makeTitle("归零 (导轨1 + 导轨3)"));
    homeHead->addStretch();
    home_state_ = new QLabel("未归零", this);
    home_state_->setStyleSheet("color:#e0a030; font-family: Consolas; font-weight:bold;");
    homeHead->addWidget(home_state_);
    homeLayout->addLayout(homeHead);

    auto *homeRow = new QHBoxLayout;
    home_btn_ = new QPushButton("归零 (跑到释放端硬限位)", this);
    home_btn_->setFixedHeight(32);
    home_btn_->setToolTip(
        QStringLiteral("驱动导轨1和导轨3 往释放方向跑到硬限位, 把该处编码器计数记为零点。\n"
                       "导轨驱动器重新上电后必须做一次 —— 它们的多圈计数会从 0 重新开始, "
                       "且固件的上电自动回零没有开启。\n"
                       "整段行程可能要十几秒。"));
    home_btn_->setStyleSheet(
        "QPushButton { background:#2f6f9f; color:white; font-weight:bold;"
        "              padding:4px 12px; border-radius:4px; }"
        "QPushButton:hover { background:#3a83b8; }"
        "QPushButton:disabled { background:#3a3d52; color:#7c7f96; }");
    homeRow->addWidget(home_btn_, 1);
    homeLayout->addLayout(homeRow);

    auto *homeHint = new QLabel(
        "归零后, 导轨位置以 mm 显示 (相对释放端硬限位)。未归零时显示 --。", this);
    homeHint->setWordWrap(true);
    homeHint->setStyleSheet("color: #888aaa;");
    homeLayout->addWidget(homeHint);

    mainLayout->addWidget(homePanel);

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
    connect(home_btn_,       &QPushButton::clicked, this, &AirportWidget::onHomeRails);
    connect(home_rail2_btn_, &QPushButton::clicked, this, &AirportWidget::onHomeRail2);
    connect(rail2_goto_btn_, &QPushButton::clicked, this, &AirportWidget::onRail2GoTo);
    connect(rail2_accel_btn_, &QPushButton::clicked, this, &AirportWidget::onRail2Accel);

    // Poll only while a homing run is in flight — the rest of the time this
    // widget stays quiet rather than adding another periodic RPC.
    home_timer_ = new QTimer(this);
    home_timer_->setInterval(500);
    connect(home_timer_, &QTimer::timeout, this, &AirportWidget::pollHomeStatus);

    // Same idea for an absolute move: poll only while one is running. Its
    // own timer, because pollHomeStatus stops itself on homing==false and a
    // move_to_mm run has homing==false the whole time.
    rail2_move_timer_ = new QTimer(this);
    rail2_move_timer_->setInterval(300);
    connect(rail2_move_timer_, &QTimer::timeout, this, &AirportWidget::pollRail2Move);
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

    // STOP → 300 ms → LOCK. Same stall-latch pre-empt as onRail2Move.
    rpc_->call(Protocol::Methods::AIRPORT_STOP_ALL, QJsonObject{});
    const int rpm = lock_spin_->value();
    QTimer::singleShot(300, this, [this, rpm]() {
        if (!rpc_ || !rpc_->isConnected()) return;
        QJsonObject params;
        params[Protocol::Fields::SPEED_RPM] = rpm;
        rpc_->call(Protocol::Methods::AIRPORT_LOCK, params);
    });
}

void AirportWidget::onRelease()
{
    if (!rpc_ || !rpc_->isConnected()) return;

    rpc_->call(Protocol::Methods::AIRPORT_STOP_ALL, QJsonObject{});
    const int rpm = lock_spin_->value();
    QTimer::singleShot(300, this, [this, rpm]() {
        if (!rpc_ || !rpc_->isConnected()) return;
        QJsonObject params;
        params[Protocol::Fields::SPEED_RPM] = rpm;
        rpc_->call(Protocol::Methods::AIRPORT_RELEASE, params);
    });
}

void AirportWidget::onRail2Move(bool forward)
{
    if (!rpc_ || !rpc_->isConnected()) return;

    // Pre-empt any latched stall state by issuing STOP first, waiting 300 ms
    // for the firmware to settle, then sending SPEED. This mirrors what the
    // operator was doing manually after stalls (前进 → 急停 → 后退) which
    // they confirmed works reliably. Doing it client-side guarantees the
    // motor sees the stop with timing before the next motion command, no
    // matter what the backend's auto-recovery is doing.
    QJsonObject stop_params;
    stop_params[Protocol::Fields::RAIL] = 1;
    rpc_->call(Protocol::Methods::AIRPORT_STOP, stop_params);

    const int rpm = rail2_spin_->value() * (forward ? 1 : -1);
    QTimer::singleShot(300, this, [this, rpm]() {
        if (!rpc_ || !rpc_->isConnected()) return;
        QJsonObject params;
        params[Protocol::Fields::RAIL] = 1;
        params[Protocol::Fields::SPEED_RPM] = rpm;
        rpc_->call(Protocol::Methods::AIRPORT_SET_SPEED, params);
    });
}

void AirportWidget::onStopAll()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    rpc_->call(Protocol::Methods::AIRPORT_STOP_ALL, QJsonObject{});
    if (home_timer_) home_timer_->stop();
    if (home_btn_)   home_btn_->setEnabled(true);
}

void AirportWidget::onHomeRails()
{
    if (!rpc_ || !rpc_->isConnected()) return;

    home_btn_->setEnabled(false);
    home_pair_pending_ = true;
    home_started_ms_ = QDateTime::currentMSecsSinceEpoch();
    home_state_->setText("归零中…");
    home_state_->setStyleSheet("color:#e0a030; font-family: Consolas; font-weight:bold;");

    // Start the watchdog BEFORE the RPC, not inside its reply. The button is
    // disabled the moment it is clicked, so the thing that re-enables it must
    // not depend on a reply that might never arrive — otherwise a single lost
    // reply leaves the operator with a dead button and no way back short of
    // restarting HostGUI.
    if (home_timer_) home_timer_->start();

    QJsonObject params;
    // Deliberately slow: this drives into a hard stop, and the whole point
    // is to land on it gently rather than hammer it.
    params[Protocol::Fields::SPEED_RPM] = 150;
    rpc_->call(Protocol::Methods::AIRPORT_HOME_RAILS, params,
        [this](QJsonObject reply) {
            if (!reply.value("ok").toBool(false)) {
                home_state_->setText("归零启动失败");
                home_state_->setStyleSheet(
                    "color:#e05050; font-family: Consolas; font-weight:bold;");
                home_pair_pending_ = false;
                home_btn_->setEnabled(true);
            }
        });
}

void AirportWidget::onHomeRail2()
{
    if (!rpc_ || !rpc_->isConnected()) return;

    home_rail2_btn_->setEnabled(false);
    home_rail2_pending_ = true;
    home_started_ms_ = QDateTime::currentMSecsSinceEpoch();
    home_rail2_state_->setText("归零中…");
    home_rail2_state_->setStyleSheet("color:#e0a030; font-family: Consolas; font-weight:bold;");

    if (home_timer_) home_timer_->start();       // see onHomeRails()

    QJsonObject params;
    params[Protocol::Fields::SPEED_RPM] = 150;   // slow — it drives into a hard stop
    rpc_->call(Protocol::Methods::AIRPORT_HOME_RAIL2, params,
        [this](QJsonObject reply) {
            if (!reply.value("ok").toBool(false)) {
                home_rail2_state_->setText("启动失败");
                home_rail2_state_->setStyleSheet(
                    "color:#e05050; font-family: Consolas; font-weight:bold;");
                home_rail2_pending_ = false;
                home_rail2_btn_->setEnabled(true);
            }
        });
}

// 导轨2 走到绝对位置。The target is resolved by the gateway
// (airport.move_to_mm): it reads the encoder, subtracts the latched zero
// and drives the difference closed-loop. Nothing here computes a delta, so
// there is no window where a stale position turns into a wrong move.
//
// The gateway refuses outright when the rail has never been homed. That is
// not a nicety — the drivers' multi-turn count restarts at 0 on power-up
// with auto-homing disabled, so an un-homed "50 mm" is 50 mm from wherever
// the rail happened to sit when the driver last booted.
void AirportWidget::onRail2GoTo()
{
    if (!rpc_ || !rpc_->isConnected()) return;

    const double target = rail2_pos_spin_->value();
    // Reuse the panel's rpm control; 0 on the slider would mean "don't move",
    // so fall back to the same gentle default the homing runs use.
    const int rpm = rail2_spin_->value() > 0 ? rail2_spin_->value() : 300;

    rail2_goto_btn_->setEnabled(false);
    rail2_goto_state_->setText(QString("走位中… → %1mm").arg(target, 0, 'f', 1));
    rail2_goto_state_->setStyleSheet("color:#e0a030; font-family: Consolas; font-weight:bold;");

    QJsonObject params;
    params[Protocol::Fields::RAIL]      = 1;
    params[Protocol::Fields::POS_MM]    = target;
    params[Protocol::Fields::SPEED_RPM] = rpm;
    rpc_->call(Protocol::Methods::AIRPORT_MOVE_TO_MM, params,
        [this, target](QJsonObject reply) {
            if (!reply.value("ok").toBool(false)) {
                // Distinguish the two refusals — "未归零" is fixed by one
                // click, "超软限位" means the number itself is wrong.
                const bool homed = reply.value("homed").toBool(false);
                rail2_goto_state_->setText(
                    homed ? QString("拒绝: %1mm 超出软限位").arg(target, 0, 'f', 1)
                          : QStringLiteral("拒绝: 未归零, 请先点「导轨2 归零」"));
                rail2_goto_state_->setStyleSheet(
                    "color:#e05050; font-family: Consolas; font-weight:bold;");
                rail2_goto_btn_->setEnabled(true);
                return;
            }
            if (rail2_move_timer_) rail2_move_timer_->start();
        });
}

// 导轨2 加速度档位。Applies to the next move, on rail 2 only — the gateway
// resolves UAV_AIRPORT_RAIL2_SPEED_ACC before the shared default, so rails
// 1/3 keep whatever they had.
//
// The reply carries the gateway's effective value back, so the label shows
// what is actually in force rather than what we asked for.
void AirportWidget::onRail2Accel()
{
    if (!rpc_ || !rpc_->isConnected()) return;

    const int acc = rail2_accel_spin_->value();
    rail2_accel_btn_->setEnabled(false);

    QJsonObject params;
    params[Protocol::Fields::RAIL]  = 1;
    params[Protocol::Fields::ACCEL] = acc;
    rpc_->call(Protocol::Methods::AIRPORT_SET_ACCEL, params,
        [this, acc](QJsonObject reply) {
            rail2_accel_btn_->setEnabled(true);
            if (!reply.value("ok").toBool(false)) {
                rail2_accel_state_->setText(QString("设置 %1 失败").arg(acc));
                rail2_accel_state_->setStyleSheet(
                    "color:#e05050; font-family: Consolas; font-weight:bold;");
                return;
            }
            const int eff = reply.value(Protocol::Fields::ACCEL).toInt(acc);
            rail2_accel_state_->setText(QString("当前 %1").arg(eff));
            rail2_accel_state_->setStyleSheet(
                "color:#3ac06a; font-family: Consolas; font-weight:bold;");
        });
}

// Runs only while an absolute move is in flight. Advances the label with
// the live position and hands the button back the moment the rail leaves
// MOVING — whether it arrived (IDLE) or hit something (STALLED).
void AirportWidget::pollRail2Move()
{
    if (!rpc_ || !rpc_->isConnected()) {
        rail2_move_timer_->stop();
        rail2_goto_btn_->setEnabled(true);
        return;
    }
    rpc_->call(Protocol::Methods::AIRPORT_GET_STATUS, QJsonObject{},
        [this](QJsonObject reply) {
            QJsonObject r1;
            for (const QJsonValue &v : reply.value("rails").toArray()) {
                const QJsonObject o = v.toObject();
                if (o.value("index").toInt(-1) == 1) { r1 = o; break; }
            }
            const int state = r1.value("state").toInt(0);
            const QJsonValue posv = r1.value(Protocol::Fields::POS_MM);
            const QString pos = posv.isNull()
                ? QString() : QString("=%1mm").arg(posv.toDouble(), 0, 'f', 1);

            if (state == 1) {                       // still MOVING
                rail2_goto_state_->setText("走位中…" + pos);
                return;
            }
            rail2_move_timer_->stop();
            rail2_goto_btn_->setEnabled(true);
            if (state == 2) {
                rail2_goto_state_->setText("堵转停" + pos);
                rail2_goto_state_->setStyleSheet(
                    "color:#e05050; font-family: Consolas; font-weight:bold;");
            } else {
                rail2_goto_state_->setText("到位" + pos);
                rail2_goto_state_->setStyleSheet(
                    "color:#3ac06a; font-family: Consolas; font-weight:bold;");
            }
        });
}

// Shared poll for both homing buttons: the backend reports per-rail
// homing/homed, so one timer can drive both panels. It stops once no rail
// is homing any more.
void AirportWidget::pollHomeStatus()
{
    if (!rpc_ || !rpc_->isConnected()) {
        home_timer_->stop();
        home_btn_->setEnabled(true);
        home_rail2_btn_->setEnabled(true);
        return;
    }
    rpc_->call(Protocol::Methods::AIRPORT_GET_STATUS, QJsonObject{},
        [this](QJsonObject reply) {
            const QJsonArray rails = reply.value("rails").toArray();

            auto railObj = [&rails](int index) {
                for (const QJsonValue &v : rails) {
                    const QJsonObject o = v.toObject();
                    if (o.value("index").toInt(-1) == index) return o;
                }
                return QJsonObject{};
            };
            auto posText = [](const QJsonObject &o) {
                const QJsonValue p = o.value(Protocol::Fields::POS_MM);
                return p.isNull() ? QString()
                                  : QString("=%1mm").arg(p.toDouble(), 0, 'f', 1);
            };
            auto paint = [](QLabel *lbl, const QString &text, const char *color) {
                lbl->setText(text);
                lbl->setStyleSheet(
                    QString("color:%1; font-family: Consolas; font-weight:bold;").arg(color));
            };

            // A homing run that has been going far longer than the backend
            // could possibly take is over, whatever the flags say. Without
            // this, one flag stuck true on the backend disables the button
            // for the rest of the session.
            const qint64 elapsed = home_started_ms_ > 0
                ? (QDateTime::currentMSecsSinceEpoch() - home_started_ms_) : 0;
            const bool watchdog = (elapsed > kHomeWatchdogMs);

            // ── 导轨1 + 导轨3 ──
            const QJsonObject r0 = railObj(0), r2 = railObj(2);
            const bool pair_homing =
                r0.value("homing").toBool(false) || r2.value("homing").toBool(false);
            if (!pair_homing || watchdog) {
                home_btn_->setEnabled(true);
                if (r0.value("homed").toBool(false) && r2.value("homed").toBool(false)) {
                    paint(home_state_,
                          QString("已归零  导轨1%1  导轨3%2")
                              .arg(posText(r0)).arg(posText(r2)),
                          "#3ac06a");
                } else if (home_pair_pending_) {
                    paint(home_state_, "归零未完成 (超时/被中断)", "#e05050");
                }
                home_pair_pending_ = false;
            }

            // ── 导轨2 ──
            const QJsonObject r1 = railObj(1);
            // Show the gear actually in force, so the field is not just an
            // input box with no feedback about what the rail is really using.
            if (r1.contains(Protocol::Fields::ACCEL)) {
                rail2_accel_state_->setText(
                    QString("当前 %1").arg(r1.value(Protocol::Fields::ACCEL).toInt()));
            }
            const bool r1_homing = r1.value("homing").toBool(false);
            if (!r1_homing || watchdog) {
                home_rail2_btn_->setEnabled(true);
                if (r1.value("homed").toBool(false)) {
                    paint(home_rail2_state_, "已归零" + posText(r1), "#3ac06a");
                } else if (home_rail2_pending_) {
                    paint(home_rail2_state_, "归零未完成 (超时/被中断)", "#e05050");
                }
                home_rail2_pending_ = false;
            }

            // Only stand down once nothing is homing AND neither button is
            // still waiting on one. Stopping on the backend flag alone was
            // the trap: the flag can read false for a tick before the
            // backend has actually started, which killed the poll and left
            // whichever button was disabled disabled for good.
            if (!reply.value("homing").toBool(false) &&
                !home_pair_pending_ && !home_rail2_pending_) {
                home_timer_->stop();
                home_started_ms_ = 0;
            }
        });
}

void AirportWidget::onGripper(bool open)
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject params;
    params[Protocol::Fields::OPEN] = open;
    rpc_->call(Protocol::Methods::AIRPORT_GRIPPER, params);
}
