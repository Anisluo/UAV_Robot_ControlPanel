#include "DroneWidget.h"
#include "MeshMapWidget.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QHostAddress>
#include <QPushButton>
#include <QTime>
#include <QTimer>
#include <QUdpSocket>
#include <QVBoxLayout>

namespace {

constexpr quint16 kDroneRpcPort = 5555;
constexpr int kDroneNodes[] = {102, 103, 104, 105, 106};

QString dashValue(const QString &label, const QString &value)
{
    return QString("%1: %2").arg(label, value);
}

} // namespace

DroneWidget::DroneWidget(QWidget *parent)
    : QGroupBox("大疆无人机控制", parent)
    , refresh_timer_(new QTimer(this))
    , udp_socket_(new QUdpSocket(this))
{
    buildUi();

    connect(refresh_timer_, &QTimer::timeout,
            this, &DroneWidget::refreshDroneStates);
    connect(btn_refresh_, &QPushButton::clicked,
            this, &DroneWidget::refreshDroneStates);
    connect(udp_socket_, &QUdpSocket::readyRead,
            this, &DroneWidget::onUdpReadyRead);

    refresh_timer_->start(5000);
    refreshDroneStates();
}

void DroneWidget::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);
    layout->setContentsMargins(8, 18, 8, 8);

    auto *targetRow = new QHBoxLayout;
    auto *targetLabel = new QLabel("控制节点:", this);
    targetLabel->setStyleSheet("color: #00c8d7; font-family: Consolas;");
    targetLabel->setFixedWidth(64);

    target_host_edit_ = new QLineEdit("192.168.1.101", this);
    target_host_edit_->setPlaceholderText("192.168.1.10x");

    auto *portHint = new QLabel(QString("PSDK UDP %1").arg(kDroneRpcPort), this);
    portHint->setStyleSheet("font-family: Consolas; color: #7f8aa3;");

    btn_refresh_ = new QPushButton("立即刷新", this);
    btn_refresh_->setFixedHeight(28);
    btn_refresh_->setFixedWidth(88);

    targetRow->addWidget(targetLabel);
    targetRow->addWidget(target_host_edit_, 1);
    targetRow->addWidget(portHint);
    targetRow->addWidget(btn_refresh_);
    layout->addLayout(targetRow);

    refresh_label_ = new QLabel("监控节点: .102 ~ .106，5 秒自动刷新", this);
    refresh_label_->setStyleSheet("font-family: Consolas; color: #7f8aa3;");
    layout->addWidget(refresh_label_);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);

    int row = 0;
    int col = 0;
    for (int octet : kDroneNodes) {
        createNodeCard(grid, row, octet);
        ++col;
        if (col == 2) {
            col = 0;
            ++row;
        }
    }

    layout->addLayout(grid);

    status_label_ = new QLabel("等待 activate 节点...", this);
    status_label_->setStyleSheet("font-family: Consolas; color: #888aaa;");
    layout->addWidget(status_label_);
}

void DroneWidget::createNodeCard(QGridLayout *grid, int row, int octet)
{
    const int column = (octet - 102) % 2;

    auto *card = new QFrame(this);
    card->setFrameShape(QFrame::StyledPanel);
    card->setStyleSheet(
        "QFrame {"
        " background: #232634;"
        " border: 1px solid #3a3f52;"
        " border-radius: 8px;"
        "}"
    );

    auto *layout = new QVBoxLayout(card);
    layout->setSpacing(4);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *title = new QLabel(QString("节点 .%1").arg(octet), card);
    title->setStyleSheet("font-weight: 700; color: #dde1f0;");

    auto *state = new QLabel("状态: inactive", card);
    auto *host = new QLabel(dashValue("地址", nodeIp(octet)), card);
    auto *battery = new QLabel(dashValue("电池", "--"), card);
    auto *altitude = new QLabel(dashValue("高度", "--"), card);
    auto *heading = new QLabel(dashValue("航向", "--"), card);
    auto *gps = new QLabel(dashValue("GPS", "--"), card);
    auto *updated = new QLabel(dashValue("更新", "--"), card);

    const QList<QLabel*> labels = {state, host, battery, altitude, heading, gps, updated};
    for (QLabel *label : labels) {
        label->setStyleSheet("font-family: Consolas; color: #9aa3b8;");
        label->setWordWrap(true);
        layout->addWidget(label);
    }

    layout->insertWidget(0, title);

    node_cards_.insert(octet, card);
    state_labels_.insert(octet, state);
    host_labels_.insert(octet, host);
    battery_labels_.insert(octet, battery);
    altitude_labels_.insert(octet, altitude);
    heading_labels_.insert(octet, heading);
    gps_labels_.insert(octet, gps);
    updated_labels_.insert(octet, updated);
    active_nodes_.insert(octet, false);

    setNodeActive(octet, false);
    grid->addWidget(card, row, column);
}

void DroneWidget::setStatus(const QString &text, const QString &color)
{
    status_label_->setText(text);
    status_label_->setStyleSheet(
        QString("font-family: Consolas; color: %1;").arg(color));
}

void DroneWidget::setDefaultTargetHost(const QString &host)
{
    if (!host.trimmed().isEmpty()) {
        target_host_edit_->setText(host.trimmed());
    }
}

QString DroneWidget::nodeIp(int octet) const
{
    return QString("192.168.1.%1").arg(octet);
}

void DroneWidget::setNodeActive(int octet, bool active)
{
    active_nodes_[octet] = active;

    QFrame *card = node_cards_.value(octet, nullptr);
    QLabel *state = state_labels_.value(octet, nullptr);
    if (!card || !state) {
        return;
    }

    if (active) {
        card->setStyleSheet(
            "QFrame {"
            " background: #1f2c26;"
            " border: 1px solid #4caf50;"
            " border-radius: 8px;"
            "}"
        );
        state->setText("状态: activate");
        state->setStyleSheet("font-family: Consolas; color: #4caf50; font-weight: 700;");
    } else {
        card->setStyleSheet(
            "QFrame {"
            " background: #232634;"
            " border: 1px solid #3a3f52;"
            " border-radius: 8px;"
            "}"
        );
        state->setText("状态: inactive");
        state->setStyleSheet("font-family: Consolas; color: #7f8aa3;");
        battery_labels_[octet]->setText(dashValue("电池", "--"));
        altitude_labels_[octet]->setText(dashValue("高度", "--"));
        heading_labels_[octet]->setText(dashValue("航向", "--"));
        gps_labels_[octet]->setText(dashValue("GPS", "--"));
        updated_labels_[octet]->setText(dashValue("更新", "--"));
    }
}

void DroneWidget::updateNodeTimestamp(int octet, const QString &text)
{
    QLabel *updated = updated_labels_.value(octet, nullptr);
    if (updated) {
        updated->setText(dashValue("更新", text));
    }
}

void DroneWidget::updateMeshNodes(const QList<MeshNode> &nodes)
{
    for (const MeshNode &node : nodes) {
        if (node.id < 102 || node.id > 106) {
            continue;
        }
        setNodeActive(node.id, node.reachable);
    }

    refreshDroneStates();
}

void DroneWidget::sendRpcRequest(int octet, const QString &method)
{
    const int requestId = next_request_id_++;
    pending_requests_.insert(requestId, PendingRequest{octet, method});

    QJsonObject req;
    req["jsonrpc"] = QStringLiteral("2.0");
    req["id"] = requestId;
    req["method"] = method;
    req["params"] = QJsonObject{};

    const QByteArray payload = QJsonDocument(req).toJson(QJsonDocument::Compact);
    udp_socket_->writeDatagram(payload, QHostAddress(nodeIp(octet)), kDroneRpcPort);
}

void DroneWidget::refreshDroneStates()
{
    int activeCount = 0;
    for (int octet : kDroneNodes) {
        if (!active_nodes_.value(octet, false)) {
            continue;
        }
        ++activeCount;
        sendRpcRequest(octet, "drone.get_telemetry");
        sendRpcRequest(octet, "drone.get_battery_info");
    }

    if (activeCount == 0) {
        setStatus("未发现 activate 无人机节点", "#888aaa");
    } else {
        setStatus(QString("正在刷新 %1 个 activate 节点").arg(activeCount), "#4caf50");
    }
}

void DroneWidget::onUdpReadyRead()
{
    while (udp_socket_->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(udp_socket_->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        udp_socket_->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        Q_UNUSED(sender)
        Q_UNUSED(senderPort)

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(datagram, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        const QJsonObject obj = doc.object();
        const int requestId = obj.value("id").toInt(-1);
        if (!pending_requests_.contains(requestId)) {
            continue;
        }

        const PendingRequest req = pending_requests_.take(requestId);
        if (!active_nodes_.value(req.octet, false)) {
            continue;
        }

        const QJsonObject result = obj.value("result").toObject();
        if (req.method == "drone.get_telemetry") {
            const double altRel = result.value("alt_rel_m").toDouble();
            const double heading = result.value("heading_deg").toDouble();
            const int gpsSats = result.value("gps_sats").toInt();
            const int gpsFix = result.value("gps_fix").toInt();

            altitude_labels_[req.octet]->setText(
                dashValue("高度", QString("%1 m").arg(altRel, 0, 'f', 1)));
            heading_labels_[req.octet]->setText(
                dashValue("航向", QString("%1 deg").arg(heading, 0, 'f', 1)));
            gps_labels_[req.octet]->setText(
                dashValue("GPS", QString("fix=%1 sats=%2").arg(gpsFix).arg(gpsSats)));
            updateNodeTimestamp(req.octet, QTime::currentTime().toString("hh:mm:ss"));
        } else if (req.method == "drone.get_battery_info") {
            const int pct = result.value("remaining_pct").toInt(-1);
            const int mv = result.value("voltage_mv").toInt();
            const int temp = result.value("temperature_dc").toInt();

            battery_labels_[req.octet]->setText(
                dashValue("电池", QString("%1%  %2 mV  %3 dC").arg(pct).arg(mv).arg(temp)));
            updateNodeTimestamp(req.octet, QTime::currentTime().toString("hh:mm:ss"));
        }
    }
}
