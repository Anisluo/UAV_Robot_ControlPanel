#include "AirportWidget.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

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

    // Poll only while a homing run is in flight — the rest of the time this
    // widget stays quiet rather than adding another periodic RPC.
    home_timer_ = new QTimer(this);
    home_timer_->setInterval(500);
    connect(home_timer_, &QTimer::timeout, this, &AirportWidget::pollHomeStatus);
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
    home_state_->setText("归零中…");
    home_state_->setStyleSheet("color:#e0a030; font-family: Consolas; font-weight:bold;");

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
                home_btn_->setEnabled(true);
                return;
            }
            if (home_timer_) home_timer_->start();
        });
}

void AirportWidget::onHomeRail2()
{
    if (!rpc_ || !rpc_->isConnected()) return;

    home_rail2_btn_->setEnabled(false);
    home_rail2_state_->setText("归零中…");
    home_rail2_state_->setStyleSheet("color:#e0a030; font-family: Consolas; font-weight:bold;");

    QJsonObject params;
    params[Protocol::Fields::SPEED_RPM] = 150;   // slow — it drives into a hard stop
    rpc_->call(Protocol::Methods::AIRPORT_HOME_RAIL2, params,
        [this](QJsonObject reply) {
            if (!reply.value("ok").toBool(false)) {
                home_rail2_state_->setText("启动失败");
                home_rail2_state_->setStyleSheet(
                    "color:#e05050; font-family: Consolas; font-weight:bold;");
                home_rail2_btn_->setEnabled(true);
                return;
            }
            if (home_timer_) home_timer_->start();
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

            // ── 导轨1 + 导轨3 ──
            const QJsonObject r0 = railObj(0), r2 = railObj(2);
            if (!r0.value("homing").toBool(false) && !r2.value("homing").toBool(false)) {
                home_btn_->setEnabled(true);
                if (r0.value("homed").toBool(false) && r2.value("homed").toBool(false)) {
                    paint(home_state_,
                          QString("已归零  导轨1%1  导轨3%2")
                              .arg(posText(r0)).arg(posText(r2)),
                          "#3ac06a");
                } else if (home_state_->text() == "归零中…") {
                    paint(home_state_, "归零未完成 (超时/被中断)", "#e05050");
                }
            }

            // ── 导轨2 ──
            const QJsonObject r1 = railObj(1);
            if (!r1.value("homing").toBool(false)) {
                home_rail2_btn_->setEnabled(true);
                if (r1.value("homed").toBool(false)) {
                    paint(home_rail2_state_, "已归零" + posText(r1), "#3ac06a");
                } else if (home_rail2_state_->text() == "归零中…") {
                    paint(home_rail2_state_, "归零未完成", "#e05050");
                }
            }

            if (!reply.value("homing").toBool(false)) {
                home_timer_->stop();
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
