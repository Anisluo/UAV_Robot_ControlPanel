#include "DroneWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QFile>
#include <QTcpSocket>

DroneWidget::DroneWidget(QWidget *parent)
    : QGroupBox("大疆无人机控制", parent)
    , socket_(new QTcpSocket(this))
{
    buildUi();

    connect(socket_, &QTcpSocket::connected,
            this, &DroneWidget::onConnected);
    connect(socket_, &QTcpSocket::bytesWritten,
            this, &DroneWidget::onBytesWritten);
    connect(socket_, &QTcpSocket::disconnected,
            this, &DroneWidget::onDisconnected);
    connect(socket_, &QAbstractSocket::errorOccurred,
            this, &DroneWidget::onError);
}

void DroneWidget::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);
    layout->setContentsMargins(8, 18, 8, 8);

    // ── Row 1: file selector ──────────────────────────────────────────────────
    auto *fileRow = new QHBoxLayout;
    auto *fileLabel = new QLabel("信令文件:", this);
    fileLabel->setStyleSheet("color: #00c8d7; font-family: Consolas;");
    fileLabel->setFixedWidth(64);

    file_path_edit_ = new QLineEdit(this);
    file_path_edit_->setPlaceholderText("未选择文件...");
    file_path_edit_->setReadOnly(true);

    btn_load_ = new QPushButton("加载文件", this);
    btn_load_->setFixedWidth(80);
    btn_load_->setFixedHeight(28);

    fileRow->addWidget(fileLabel);
    fileRow->addWidget(file_path_edit_, 1);
    fileRow->addWidget(btn_load_);
    layout->addLayout(fileRow);

    // ── Row 2: target IP + port ───────────────────────────────────────────────
    auto *targetRow = new QHBoxLayout;
    auto *ipLabel = new QLabel("目标节点:", this);
    ipLabel->setStyleSheet("color: #00c8d7; font-family: Consolas;");
    ipLabel->setFixedWidth(64);

    ip_edit_ = new QLineEdit("192.168.10.1", this);
    ip_edit_->setFixedWidth(130);
    ip_edit_->setPlaceholderText("IP 地址");

    auto *portLabel = new QLabel("端口:", this);
    portLabel->setStyleSheet("color: #00c8d7; font-family: Consolas;");

    port_spin_ = new QSpinBox(this);
    port_spin_->setRange(1, 65535);
    port_spin_->setValue(14550);    // MAVLink / DJI ground station default
    port_spin_->setFixedWidth(75);

    targetRow->addWidget(ipLabel);
    targetRow->addWidget(ip_edit_);
    targetRow->addSpacing(8);
    targetRow->addWidget(portLabel);
    targetRow->addWidget(port_spin_);
    targetRow->addStretch();
    layout->addLayout(targetRow);

    // ── Row 3: status + send ──────────────────────────────────────────────────
    auto *sendRow = new QHBoxLayout;

    status_label_ = new QLabel("就绪", this);
    status_label_->setStyleSheet("font-family: Consolas; color: #888aaa;");

    btn_send_ = new QPushButton("发 送", this);
    btn_send_->setFixedWidth(80);
    btn_send_->setFixedHeight(28);
    btn_send_->setEnabled(false);

    sendRow->addWidget(status_label_, 1);
    sendRow->addWidget(btn_send_);
    layout->addLayout(sendRow);

    connect(btn_load_, &QPushButton::clicked, this, &DroneWidget::onLoadFile);
    connect(btn_send_, &QPushButton::clicked, this, &DroneWidget::onSend);
}

void DroneWidget::setStatus(const QString &text, const QString &color)
{
    status_label_->setText(text);
    status_label_->setStyleSheet(
        QString("font-family: Consolas; color: %1;").arg(color));
}

// ─── Slots ────────────────────────────────────────────────────────────────────

void DroneWidget::onLoadFile()
{
    QString path = QFileDialog::getOpenFileName(
        this, "选择控制信令文件", QString(),
        "所有文件 (*);;二进制文件 (*.bin);;文本文件 (*.txt *.json)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus("错误: 无法打开文件", "#f44336");
        return;
    }
    file_data_ = f.readAll();
    f.close();

    file_path_edit_->setText(path);
    setStatus(QString("已加载  %1 字节").arg(file_data_.size()), "#4caf50");
    btn_send_->setEnabled(!file_data_.isEmpty());
}

void DroneWidget::onSend()
{
    if (file_data_.isEmpty()) {
        setStatus("错误: 请先加载信令文件", "#f44336");
        return;
    }
    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->abort();
    }

    QString ip   = ip_edit_->text().trimmed();
    quint16 port = static_cast<quint16>(port_spin_->value());

    bytes_written_ = 0;
    btn_send_->setEnabled(false);
    setStatus(QString("正在连接 %1:%2 …").arg(ip).arg(port), "#ff9800");
    socket_->connectToHost(ip, port);
}

void DroneWidget::onConnected()
{
    setStatus(QString("连接成功，正在发送 %1 字节…").arg(file_data_.size()), "#ff9800");
    socket_->write(file_data_);
}

void DroneWidget::onBytesWritten(qint64 bytes)
{
    bytes_written_ += bytes;
    if (bytes_written_ >= file_data_.size()) {
        // All bytes handed to the kernel — disconnect cleanly
        socket_->disconnectFromHost();
    }
}

void DroneWidget::onDisconnected()
{
    if (bytes_written_ >= file_data_.size() && !file_data_.isEmpty()) {
        setStatus(QString("发送完成  %1 字节").arg(bytes_written_), "#4caf50");
    } else {
        setStatus("连接已断开", "#888aaa");
    }
    btn_send_->setEnabled(!file_data_.isEmpty());
}

void DroneWidget::onError(QAbstractSocket::SocketError)
{
    setStatus("错误: " + socket_->errorString(), "#f44336");
    btn_send_->setEnabled(!file_data_.isEmpty());
}
