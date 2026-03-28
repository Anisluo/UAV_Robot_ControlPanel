#include "Tab2CommConfig.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QFrame>
#include <QJsonObject>
#include <QString>

Tab2CommConfig::Tab2CommConfig(RpcClient *rpc, QWidget *parent)
    : QWidget(parent)
    , rpc_(rpc)
{
    buildUi();
}

void Tab2CommConfig::buildUi()
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *container = new QWidget(scrollArea);
    auto *layout    = new QVBoxLayout(container);
    layout->setSpacing(12);
    layout->setContentsMargins(12, 12, 12, 12);

    // Note label
    auto *noteLabel = new QLabel(
        "以下设置通过RPC发送至RK3588。"
        "实际硬件连接位于机器人端。", container);
    noteLabel->setWordWrap(true);
    noteLabel->setStyleSheet("color: #ff9800; font-style: italic; padding: 4px;");
    layout->addWidget(noteLabel);

    // ── CAN Bus ──
    {
        auto *grp = new QGroupBox("CAN总线", container);
        auto *form = new QFormLayout(grp);
        form->setSpacing(8);
        form->setContentsMargins(8, 18, 8, 8);

        auto *canHint = new QLabel(
            "can1 对应平台直线电机，can3 对应小车底盘，can4 对应机械臂。"
            "三条总线会同时显示，可分别设置波特率。",
            grp);
        canHint->setWordWrap(true);
        canHint->setStyleSheet("color: #888aaa;");
        form->addRow("", canHint);

        auto addCanRow = [&](const QString &device,
                             const QString &label,
                             int default_bitrate,
                             QSpinBox **spin_out,
                             void (Tab2CommConfig::*slot)()) {
            auto *row = new QWidget(grp);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(8);

            auto *nameLabel = new QLabel(label, row);
            nameLabel->setMinimumWidth(180);
            rowLayout->addWidget(nameLabel);

            auto *bitrateSpin = new QSpinBox(row);
            bitrateSpin->setRange(10000, 1000000);
            bitrateSpin->setSingleStep(50000);
            bitrateSpin->setValue(default_bitrate);
            bitrateSpin->setSuffix(" bps");
            rowLayout->addWidget(bitrateSpin);

            auto *applyBtn = new QPushButton("应用", row);
            applyBtn->setFixedWidth(80);
            rowLayout->addWidget(applyBtn);
            rowLayout->addStretch();

            connect(applyBtn, &QPushButton::clicked, this, slot);
            form->addRow(device + ":", row);
            *spin_out = bitrateSpin;
        };

        addCanRow("can1", "平台直线电机", 500000, &can1_bitrate_spin_, &Tab2CommConfig::onApplyCan1);
        addCanRow("can3", "小车底盘", 1000000, &can3_bitrate_spin_, &Tab2CommConfig::onApplyCan3);
        addCanRow("can4", "机械臂", 500000, &can4_bitrate_spin_, &Tab2CommConfig::onApplyCan4);

        layout->addWidget(grp);
    }

    // ── Serial Port ──
    {
        auto *grp = new QGroupBox("串口", container);
        auto *form = new QFormLayout(grp);
        form->setSpacing(8);
        form->setContentsMargins(8, 18, 8, 8);

        serial_device_edit_ = new QLineEdit("/dev/ttyUSB0", grp);
        form->addRow("设备:", serial_device_edit_);

        serial_baud_combo_ = new QComboBox(grp);
        serial_baud_combo_->addItems({"9600", "19200", "38400", "57600",
                                      "115200", "230400", "460800", "921600"});
        serial_baud_combo_->setCurrentText("115200");
        form->addRow("波特率:", serial_baud_combo_);

        auto *applyBtn = new QPushButton("应用", grp);
        applyBtn->setFixedWidth(80);
        form->addRow("", applyBtn);
        connect(applyBtn, &QPushButton::clicked, this, &Tab2CommConfig::onApplySerial);

        layout->addWidget(grp);
    }

    // ── Relay ──
    {
        auto *grp = new QGroupBox("继电器", container);
        auto *form = new QFormLayout(grp);
        form->setSpacing(8);
        form->setContentsMargins(8, 18, 8, 8);

        relay_gpio_spin_ = new QSpinBox(grp);
        relay_gpio_spin_->setRange(0, 255);
        relay_gpio_spin_->setValue(17);
        form->addRow("GPIO引脚:", relay_gpio_spin_);

        relay_active_combo_ = new QComboBox(grp);
        relay_active_combo_->addItems({"高电平有效", "低电平有效"});
        form->addRow("有效电平:", relay_active_combo_);

        auto *testBtn = new QPushButton("测试", grp);
        testBtn->setFixedWidth(80);
        form->addRow("", testBtn);
        connect(testBtn, &QPushButton::clicked, this, &Tab2CommConfig::onApplyRelay);

        layout->addWidget(grp);
    }

    // ── Ethernet ──
    {
        auto *grp = new QGroupBox("以太网", container);
        auto *form = new QFormLayout(grp);
        form->setSpacing(8);
        form->setContentsMargins(8, 18, 8, 8);

        eth_host_edit_ = new QLineEdit("192.168.1.100", grp);
        form->addRow("主机IP:", eth_host_edit_);

        eth_rpc_port_spin_ = new QSpinBox(grp);
        eth_rpc_port_spin_->setRange(1, 65535);
        eth_rpc_port_spin_->setValue(Protocol::RPC_PORT);
        form->addRow("RPC端口:", eth_rpc_port_spin_);

        eth_video_port_spin_ = new QSpinBox(grp);
        eth_video_port_spin_->setRange(1, 65535);
        eth_video_port_spin_->setValue(Protocol::VIDEO_PORT);
        form->addRow("视频端口:", eth_video_port_spin_);

        auto *applyBtn = new QPushButton("应用", grp);
        applyBtn->setFixedWidth(80);
        form->addRow("", applyBtn);
        connect(applyBtn, &QPushButton::clicked, this, &Tab2CommConfig::onApplyEthernet);

        layout->addWidget(grp);
    }

    layout->addStretch();
    container->setLayout(layout);
    scrollArea->setWidget(container);
    outerLayout->addWidget(scrollArea);
}

void Tab2CommConfig::setConnectionParams(const QString &host, quint16 rpcPort, quint16 videoPort)
{
    eth_host_edit_->setText(host);
    eth_rpc_port_spin_->setValue(rpcPort);
    eth_video_port_spin_->setValue(videoPort);
}

void Tab2CommConfig::applyCanConfig(const QString &device, QSpinBox *bitrate_spin)
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject params;
    params[Protocol::Fields::DEVICE]  = device;
    params[Protocol::Fields::BITRATE] = bitrate_spin->value();
    rpc_->call(Protocol::Methods::CONFIG_SET_CAN, params);
}

void Tab2CommConfig::onApplyCan1()
{
    applyCanConfig("can1", can1_bitrate_spin_);
}

void Tab2CommConfig::onApplyCan3()
{
    applyCanConfig("can3", can3_bitrate_spin_);
}

void Tab2CommConfig::onApplyCan4()
{
    applyCanConfig("can4", can4_bitrate_spin_);
}

void Tab2CommConfig::onApplySerial()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject params;
    params[Protocol::Fields::DEVICE]    = serial_device_edit_->text();
    params[Protocol::Fields::BAUD_RATE] = serial_baud_combo_->currentText().toInt();
    rpc_->call(Protocol::Methods::CONFIG_SET_SERIAL, params);
}

void Tab2CommConfig::onApplyRelay()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject params;
    params[Protocol::Fields::GPIO_PIN]  = relay_gpio_spin_->value();
    params[Protocol::Fields::ACTIVE_LOW] = (relay_active_combo_->currentIndex() == 1);
    rpc_->call(Protocol::Methods::CONFIG_SET_RELAY, params);
}

void Tab2CommConfig::onApplyEthernet()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject params;
    params[Protocol::Fields::HOST]        = eth_host_edit_->text();
    params[Protocol::Fields::RPC_PORT_F]  = eth_rpc_port_spin_->value();
    params[Protocol::Fields::VIDEO_PORT_F] = eth_video_port_spin_->value();
    rpc_->call(Protocol::Methods::CONFIG_SET_ETHERNET, params);
}
