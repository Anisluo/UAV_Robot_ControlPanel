#include "DoorWidget.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kPollIntervalMs = 300;

// Dot colours. Grey = inactive, and each active meaning gets its own hue
// so a glance at the row tells you *which* end was reached.
const char *kDotOff    = "#353650";
const char *kDotGreen  = "#3ac06a";   // limit reached / at end position
const char *kDotAmber  = "#e0a030";   // relay energised
const char *kDotRed    = "#e05050";   // fault

QString dotStyle(const QString &color)
{
    return QString("background-color: %1; border: 1px solid #555770;"
                   " border-radius: 7px;").arg(color);
}

// Backend state token → 中文 label + colour.
struct StateSkin {
    QString text;
    QString color;
};

StateSkin hatchSkin(const QString &state)
{
    if (state == "opened")  return {"已开到位",  "#3ac06a"};
    if (state == "closed")  return {"已关到位",  "#3ac06a"};
    if (state == "opening") return {"开舱门中…", "#e0a030"};
    if (state == "closing") return {"关舱门中…", "#e0a030"};
    if (state == "between") return {"中间位置",  "#c8cbe0"};
    if (state == "fault")   return {"限位异常",  "#e05050"};
    return {"未知", "#888aaa"};
}

StateSkin padSkin(const QString &state)
{
    if (state == "top")      return {"已到顶",   "#3ac06a"};
    if (state == "bottom")   return {"已到底",   "#3ac06a"};
    if (state == "rising")   return {"上升中…",  "#e0a030"};
    if (state == "lowering") return {"下降中…",  "#e0a030"};
    if (state == "between")  return {"中间位置", "#c8cbe0"};
    if (state == "fault")    return {"限位异常", "#e05050"};
    return {"未知", "#888aaa"};
}

// reason token → short 中文 hint appended after the state.
QString reasonHint(const QString &reason)
{
    if (reason == "reached")         return "";              // already obvious
    if (reason == "timeout")         return "  [超时已断电]";
    if (reason == "stopped")         return "  [手动停止]";
    if (reason == "already")         return "  [已在位]";
    if (reason == "manual_override") return "  [被手动改写]";
    if (reason == "link down" || reason == "link lost") return "  [通讯中断]";
    return "";
}

const char *kBtnGreen =
    "QPushButton { background:#2f8f5b; color:white; font-weight:bold;"
    "              padding:4px 10px; border-radius:4px; }"
    "QPushButton:hover { background:#3aa76c; }"
    "QPushButton:disabled { background:#3a3d52; color:#7c7f96; }";

const char *kBtnBlue =
    "QPushButton { background:#2f6f9f; color:white; font-weight:bold;"
    "              padding:4px 10px; border-radius:4px; }"
    "QPushButton:hover { background:#3a83b8; }"
    "QPushButton:disabled { background:#3a3d52; color:#7c7f96; }";

const char *kBtnGrey =
    "QPushButton { background:#5a5d75; color:white; font-weight:bold;"
    "              padding:4px 10px; border-radius:4px; }"
    "QPushButton:hover { background:#6c6f88; }"
    "QPushButton:disabled { background:#3a3d52; color:#7c7f96; }";

const char *kBtnRed =
    "QPushButton { background:#b03434; color:white; font-weight:bold;"
    "              padding:5px 12px; border-radius:4px; }"
    "QPushButton:hover { background:#cc4040; }"
    "QPushButton:disabled { background:#4a3535; color:#8c7f7f; }";

}  // namespace

DoorWidget::DoorWidget(RpcClient *rpc, QWidget *parent)
    : QGroupBox("舱门 / 停机坪 [RS485 继电器]", parent)
    , rpc_(rpc)
{
    buildUi();

    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(kPollIntervalMs);
    connect(poll_timer_, &QTimer::timeout, this, &DoorWidget::poll);
}

DoorWidget::Led DoorWidget::makeLed(const QString &caption, QWidget *parent)
{
    Led led;
    led.dot = new QLabel(parent);
    led.dot->setFixedSize(14, 14);
    led.dot->setStyleSheet(dotStyle(kDotOff));
    led.text = new QLabel(caption, parent);
    led.text->setStyleSheet("color:#a8abc4;");
    return led;
}

void DoorWidget::setLed(const Led &led, bool on, const QString &onColor)
{
    if (led.dot == nullptr) return;
    led.dot->setStyleSheet(dotStyle(on ? onColor : QString(kDotOff)));
}

void DoorWidget::setLinkState(const QString &text, const QString &color)
{
    if (link_dot_ != nullptr) link_dot_->setStyleSheet(dotStyle(color));
    if (link_state_ != nullptr) {
        link_state_->setText(text);
        link_state_->setStyleSheet(
            QString("color:%1; font-family: Consolas;").arg(color));
    }
}

void DoorWidget::buildUi()
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

    // ── 舱门 ────────────────────────────────────────────────────────────
    auto *hatchPanel = makePanel();
    auto *hatchLayout = new QVBoxLayout(hatchPanel);
    hatchLayout->setSpacing(6);

    auto *hatchHead = new QHBoxLayout;
    hatchHead->addWidget(makeTitle("舱门  Y3=使能 / Y4=方向"));
    hatchHead->addStretch();
    hatch_state_ = new QLabel("未知", this);
    hatch_state_->setStyleSheet("color:#888aaa; font-family: Consolas; font-weight:bold;");
    hatchHead->addWidget(hatch_state_);
    hatchLayout->addLayout(hatchHead);

    auto *hatchBtns = new QHBoxLayout;
    hatchBtns->setSpacing(6);
    hatch_open_btn_  = new QPushButton("开舱门", this);
    hatch_close_btn_ = new QPushButton("关舱门", this);
    hatch_stop_btn_  = new QPushButton("停止", this);
    hatch_open_btn_->setFixedHeight(32);
    hatch_close_btn_->setFixedHeight(32);
    hatch_stop_btn_->setFixedHeight(32);
    hatch_open_btn_->setStyleSheet(kBtnGreen);
    hatch_close_btn_->setStyleSheet(kBtnBlue);
    hatch_stop_btn_->setStyleSheet(kBtnGrey);
    hatchBtns->addWidget(hatch_open_btn_, 1);
    hatchBtns->addWidget(hatch_close_btn_, 1);
    hatchBtns->addWidget(hatch_stop_btn_, 1);
    hatchLayout->addLayout(hatchBtns);

    mainLayout->addWidget(hatchPanel);

    // ── 停机坪 ──────────────────────────────────────────────────────────
    auto *padPanel = makePanel();
    auto *padLayout = new QVBoxLayout(padPanel);
    padLayout->setSpacing(6);

    auto *padHead = new QHBoxLayout;
    padHead->addWidget(makeTitle("停机坪升降  Y1=上升 / Y2=下降"));
    padHead->addStretch();
    pad_state_ = new QLabel("未知", this);
    pad_state_->setStyleSheet("color:#888aaa; font-family: Consolas; font-weight:bold;");
    padHead->addWidget(pad_state_);
    padLayout->addLayout(padHead);

    auto *padBtns = new QHBoxLayout;
    padBtns->setSpacing(6);
    pad_up_btn_   = new QPushButton("上升", this);
    pad_down_btn_ = new QPushButton("下降", this);
    pad_stop_btn_ = new QPushButton("停止", this);
    pad_up_btn_->setFixedHeight(32);
    pad_down_btn_->setFixedHeight(32);
    pad_stop_btn_->setFixedHeight(32);
    pad_up_btn_->setStyleSheet(kBtnGreen);
    pad_down_btn_->setStyleSheet(kBtnBlue);
    pad_stop_btn_->setStyleSheet(kBtnGrey);
    padBtns->addWidget(pad_up_btn_, 1);
    padBtns->addWidget(pad_down_btn_, 1);
    padBtns->addWidget(pad_stop_btn_, 1);
    padLayout->addLayout(padBtns);

    mainLayout->addWidget(padPanel);

    // ── 传感器 / 继电器状态 ─────────────────────────────────────────────
    auto *ioPanel = makePanel();
    auto *ioLayout = new QVBoxLayout(ioPanel);
    ioLayout->setSpacing(6);

    auto *ioHead = new QHBoxLayout;
    ioHead->addWidget(makeTitle("传感器 / 继电器"));
    ioHead->addStretch();

    // 串口/模块连接指示灯。灰=未连接主机, 绿=模块在线, 红=串口开着但模块
    // 不应答（板子断电/A-B 松了）, 橙=正在重连。
    link_dot_ = new QLabel(this);
    link_dot_->setFixedSize(14, 14);
    link_dot_->setStyleSheet(dotStyle(kDotOff));
    ioHead->addWidget(link_dot_);

    link_state_ = new QLabel("未连接", this);
    link_state_->setStyleSheet("color:#888aaa; font-family: Consolas;");
    ioHead->addSpacing(4);
    ioHead->addWidget(link_state_);

    // 继电器板断电重上电后按这个：proc_door 关掉串口重开并重新握手。
    // 后端本来每 2s 也会自己重试，这个按钮只是把等待变成立即。
    reconnect_btn_ = new QPushButton("重连", this);
    reconnect_btn_->setFixedHeight(24);
    reconnect_btn_->setToolTip("继电器板断电/换线后，强制重开 RS485 串口并重新握手");
    reconnect_btn_->setStyleSheet(kBtnGrey);
    ioHead->addSpacing(6);
    ioHead->addWidget(reconnect_btn_);
    ioLayout->addLayout(ioHead);

    static const char *kInCaptions[4] = {
        "X1 坪-上限位", "X2 坪-下限位", "X3 门-开到位", "X4 门-关到位"
    };
    static const char *kOutCaptions[4] = {
        "Y1 坪-上升", "Y2 坪-下降", "Y3 门-使能", "Y4 门-正转"
    };

    auto *ioGrid = new QGridLayout;
    ioGrid->setHorizontalSpacing(10);
    ioGrid->setVerticalSpacing(5);
    for (int i = 0; i < 4; ++i) {
        in_leds_[i] = makeLed(QString::fromUtf8(kInCaptions[i]), this);
        ioGrid->addWidget(in_leds_[i].dot,  i % 2, (i / 2) * 4 + 0);
        ioGrid->addWidget(in_leds_[i].text, i % 2, (i / 2) * 4 + 1);

        out_leds_[i] = makeLed(QString::fromUtf8(kOutCaptions[i]), this);
        ioGrid->addWidget(out_leds_[i].dot,  2 + i % 2, (i / 2) * 4 + 0);
        ioGrid->addWidget(out_leds_[i].text, 2 + i % 2, (i / 2) * 4 + 1);
    }
    ioGrid->setColumnStretch(1, 1);
    ioGrid->setColumnStretch(5, 1);
    ioLayout->addLayout(ioGrid);

    mainLayout->addWidget(ioPanel);

    // ── 急停 ────────────────────────────────────────────────────────────
    auto *stopRow = new QHBoxLayout;
    stop_all_btn_ = new QPushButton("舱门+停机坪 全部急停", this);
    stop_all_btn_->setFixedHeight(34);
    stop_all_btn_->setStyleSheet(kBtnRed);
    stopRow->addWidget(stop_all_btn_, 1);
    mainLayout->addLayout(stopRow);

    auto *hint = new QLabel(
        "限位到位或超时后后端自动断电；输出全关为默认安全状态。",
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#888aaa;");
    mainLayout->addWidget(hint);

    connect(hatch_open_btn_,  &QPushButton::clicked, this,
            [this]() { sendCommand(Protocol::Methods::DOOR_OPEN); });
    connect(hatch_close_btn_, &QPushButton::clicked, this,
            [this]() { sendCommand(Protocol::Methods::DOOR_CLOSE); });
    connect(hatch_stop_btn_,  &QPushButton::clicked, this,
            [this]() { sendCommand(Protocol::Methods::DOOR_STOP); });
    connect(pad_up_btn_,   &QPushButton::clicked, this,
            [this]() { sendCommand(Protocol::Methods::HELIPAD_UP); });
    connect(pad_down_btn_, &QPushButton::clicked, this,
            [this]() { sendCommand(Protocol::Methods::HELIPAD_DOWN); });
    connect(pad_stop_btn_, &QPushButton::clicked, this,
            [this]() { sendCommand(Protocol::Methods::HELIPAD_STOP); });
    connect(stop_all_btn_, &QPushButton::clicked, this,
            [this]() { sendCommand(Protocol::Methods::DOOR_STOP_ALL); });
    connect(reconnect_btn_, &QPushButton::clicked, this, &DoorWidget::onReconnect);

    setOffline();
}

void DoorWidget::onRpcConnected()
{
    if (poll_timer_) poll_timer_->start();
    poll();
}

void DoorWidget::onRpcDisconnected()
{
    if (poll_timer_) poll_timer_->stop();
    setOffline();
}

void DoorWidget::sendCommand(const QString &method)
{
    if (rpc_ == nullptr || !rpc_->isConnected()) return;
    rpc_->call(method, QJsonObject{}, [this](QJsonObject reply) {
        // The reply already carries the fresh axis state — use it so the UI
        // reacts on the click instead of waiting up to a poll period.
        applyStatus(reply);
    });
}

void DoorWidget::onReconnect()
{
    if (rpc_ == nullptr || !rpc_->isConnected()) return;

    reconnect_btn_->setEnabled(false);
    reconnect_btn_->setText("重连中");
    setLinkState("重连中…", kDotAmber);

    rpc_->call(Protocol::Methods::DOOR_RECONNECT, QJsonObject{},
        [this](QJsonObject reply) {
            // The round trip is only a few ms. Showing the outcome that
            // fast reads as "the button did nothing", especially when the
            // outcome is the same red LED it started from — so hold the
            // 重连中 state briefly, then paint the result.
            QTimer::singleShot(500, this, [this, reply]() {
                if (reconnect_btn_ == nullptr) return;
                reconnect_btn_->setEnabled(true);
                reconnect_btn_->setText("重连");
                // The reply carries {ok, connected, error?}; apply it, then
                // pull a full status so the sensor LEDs and both axis
                // labels refresh in the same beat.
                applyStatus(reply);
                poll();
            });
        });

    // Re-arm the button even if the gateway never answers (proc_door down,
    // socket refused) — otherwise it would stay greyed out forever, and the
    // amber "重连中…" would lie about still being in progress.
    QTimer::singleShot(4000, this, [this]() {
        if (reconnect_btn_ != nullptr && !reconnect_btn_->isEnabled()) {
            reconnect_btn_->setEnabled(true);
            reconnect_btn_->setText("重连");
            setLinkState("重连超时 (proc_door 无应答)", kDotRed);
            poll();
        }
    });
}

void DoorWidget::poll()
{
    if (rpc_ == nullptr || !rpc_->isConnected()) return;
    rpc_->call(Protocol::Methods::DOOR_GET_STATUS, QJsonObject{},
               [this](QJsonObject reply) { applyStatus(reply); });
}

void DoorWidget::applyStatus(const QJsonObject &result)
{
    // Command replies carry only the axis they touched; the status poll
    // carries everything. Update whatever is present and leave the rest.
    if (result.contains("hatch")) {
        const QJsonObject h = result.value("hatch").toObject();
        const StateSkin skin = hatchSkin(h.value("state").toString());
        const QString hint = reasonHint(h.value("reason").toString());
        hatch_state_->setText(skin.text + hint);
        hatch_state_->setStyleSheet(
            QString("color:%1; font-family: Consolas; font-weight:bold;").arg(skin.color));
    }
    if (result.contains("helipad")) {
        const QJsonObject p = result.value("helipad").toObject();
        const StateSkin skin = padSkin(p.value("state").toString());
        const QString hint = reasonHint(p.value("reason").toString());
        pad_state_->setText(skin.text + hint);
        pad_state_->setStyleSheet(
            QString("color:%1; font-family: Consolas; font-weight:bold;").arg(skin.color));
    }

    // proc_door itself is unreachable: the gateway answers on its behalf
    // with {"ok":false,"error":"proc_door unavailable"} and no "connected"
    // key at all. Without this branch the LED would keep showing whatever
    // it last saw — green — while nothing is actually being controlled.
    if (!result.contains("connected") &&
        result.value("error").toString().contains("proc_door")) {
        setLinkState("proc_door 无应答", kDotRed);
        if (link_state_) {
            link_state_->setToolTip("网关连不上 proc_door：检查 uav-proc-door.service");
        }
        return;
    }

    if (result.contains("connected")) {
        const bool online = result.value("connected").toBool(false);
        if (online) {
            setLinkState("模块在线", kDotGreen);
            if (link_state_) link_state_->setToolTip(QString());
        } else {
            // proc_door answered, but the RS485 side is dead. Two very
            // different causes hide behind that, and they need different
            // actions from the operator — so say which one it is instead
            // of a generic "offline" that makes 重连 look broken.
            const QString err = result.value("error").toString();
            QString label;
            if (err.contains("No such file") || err.contains("ENOENT")) {
                // The CH340 is powered from the relay board, so cutting the
                // board's power removes /dev/ttyUSB* entirely. No amount of
                // reconnecting helps until it is powered back on.
                label = "串口不存在 (板子断电?)";
            } else if (err.contains("no handshake")) {
                label = "模块不应答 (查 A/B 接线)";
            } else if (!err.isEmpty()) {
                label = "串口打不开";
            } else {
                label = "模块无响应 → 点重连";
            }
            setLinkState(label, kDotRed);
            if (link_state_ && !err.isEmpty()) link_state_->setToolTip(err);
        }
    }

    // X1..X4 — the sensor row the operator actually watches. A limit that
    // reads active on both ends of one axis is a wiring/sensor fault, so
    // paint those red rather than green.
    if (result.contains("inputs")) {
        const QJsonArray in = result.value("inputs").toArray();
        const bool pad_fault   = in.size() > 1 && in.at(0).toInt() && in.at(1).toInt();
        const bool hatch_fault = in.size() > 3 && in.at(2).toInt() && in.at(3).toInt();
        for (int i = 0; i < 4; ++i) {
            const bool on = (i < in.size()) && in.at(i).toInt() != 0;
            const bool fault = (i < 2) ? pad_fault : hatch_fault;
            setLed(in_leds_[i], on, fault ? kDotRed : kDotGreen);
        }
    }
    if (result.contains("outputs")) {
        const QJsonArray out = result.value("outputs").toArray();
        for (int i = 0; i < 4; ++i) {
            const bool on = (i < out.size()) && out.at(i).toInt() != 0;
            setLed(out_leds_[i], on, kDotAmber);
        }
    }
}

void DoorWidget::setOffline()
{
    if (hatch_state_) {
        hatch_state_->setText("未连接");
        hatch_state_->setStyleSheet("color:#888aaa; font-family: Consolas; font-weight:bold;");
    }
    if (pad_state_) {
        pad_state_->setText("未连接");
        pad_state_->setStyleSheet("color:#888aaa; font-family: Consolas; font-weight:bold;");
    }
    setLinkState("未连接主机", kDotOff);
    if (reconnect_btn_) {
        reconnect_btn_->setEnabled(true);
        reconnect_btn_->setText("重连");
    }
    for (int i = 0; i < 4; ++i) {
        setLed(in_leds_[i],  false, kDotGreen);
        setLed(out_leds_[i], false, kDotAmber);
    }
}
