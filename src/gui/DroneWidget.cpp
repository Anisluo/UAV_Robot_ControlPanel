#include "DroneWidget.h"
#include "MeshMapWidget.h"

#include <cmath>
#include <initializer_list>

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QHostAddress>
#include <QPushButton>
#include <QHash>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
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
    , kmz_socket_(new QTcpSocket(this))
{
    buildUi();

    connect(refresh_timer_, &QTimer::timeout,
            this, &DroneWidget::refreshDroneStates);
    connect(btn_refresh_, &QPushButton::clicked,
            this, &DroneWidget::refreshDroneStates);
    connect(udp_socket_, &QUdpSocket::readyRead,
            this, &DroneWidget::onUdpReadyRead);

    connect(kmz_socket_, &QTcpSocket::connected,
            this, &DroneWidget::onKmzConnected);
    connect(kmz_socket_, &QTcpSocket::bytesWritten,
            this, &DroneWidget::onKmzBytesWritten);
    connect(kmz_socket_, &QTcpSocket::disconnected,
            this, &DroneWidget::onKmzDisconnected);
    connect(kmz_socket_, &QAbstractSocket::errorOccurred,
            this, &DroneWidget::onKmzError);

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

    target_host_edit_ = new QLineEdit("192.168.200.101", this);
    target_host_edit_->setPlaceholderText("192.168.200.10x");

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

    // ── KMZ 路径规划下发 ──────────────────────────────────────────────────
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #3a3f52;");
    layout->addWidget(sep);

    auto *kmzTitle = new QLabel("KMZ 路径规划下发", this);
    kmzTitle->setStyleSheet("color: #00c8d7; font-family: Consolas; font-weight: 700;");
    layout->addWidget(kmzTitle);

    // File selector row
    auto *kmzFileRow = new QHBoxLayout;
    auto *kmzFileLabel = new QLabel("规划文件:", this);
    kmzFileLabel->setStyleSheet("color: #00c8d7; font-family: Consolas;");
    kmzFileLabel->setFixedWidth(64);

    kmz_path_edit_ = new QLineEdit(this);
    kmz_path_edit_->setPlaceholderText("未选择 KMZ 文件...");
    kmz_path_edit_->setReadOnly(true);

    btn_kmz_load_ = new QPushButton("选择文件", this);
    btn_kmz_load_->setFixedWidth(80);
    btn_kmz_load_->setFixedHeight(28);

    kmzFileRow->addWidget(kmzFileLabel);
    kmzFileRow->addWidget(kmz_path_edit_, 1);
    kmzFileRow->addWidget(btn_kmz_load_);
    layout->addLayout(kmzFileRow);

    // Target IP + port row
    auto *kmzTargetRow = new QHBoxLayout;
    auto *kmzIpLabel = new QLabel("目标节点:", this);
    kmzIpLabel->setStyleSheet("color: #00c8d7; font-family: Consolas;");
    kmzIpLabel->setFixedWidth(64);

    kmz_ip_combo_ = new QComboBox(this);
    for (int octet : kDroneNodes) {
        kmz_ip_combo_->addItem(QStringLiteral("192.168.200.%1").arg(octet));
    }
    kmz_ip_combo_->setFixedWidth(130);

    auto *kmzPortLabel = new QLabel("端口:", this);
    kmzPortLabel->setStyleSheet("color: #00c8d7; font-family: Consolas;");

    kmz_port_spin_ = new QSpinBox(this);
    kmz_port_spin_->setRange(1, 65535);
    kmz_port_spin_->setValue(14550);
    kmz_port_spin_->setFixedWidth(75);

    btn_kmz_send_ = new QPushButton("下 发", this);
    btn_kmz_send_->setFixedWidth(80);
    btn_kmz_send_->setFixedHeight(28);
    btn_kmz_send_->setEnabled(false);

    kmzTargetRow->addWidget(kmzIpLabel);
    kmzTargetRow->addWidget(kmz_ip_combo_);
    kmzTargetRow->addSpacing(8);
    kmzTargetRow->addWidget(kmzPortLabel);
    kmzTargetRow->addWidget(kmz_port_spin_);
    kmzTargetRow->addStretch();
    kmzTargetRow->addWidget(btn_kmz_send_);
    layout->addLayout(kmzTargetRow);

    kmz_status_label_ = new QLabel("就绪", this);
    kmz_status_label_->setStyleSheet("font-family: Consolas; color: #888aaa;");
    layout->addWidget(kmz_status_label_);

    connect(btn_kmz_load_, &QPushButton::clicked, this, &DroneWidget::onKmzLoadFile);
    connect(btn_kmz_send_, &QPushButton::clicked, this, &DroneWidget::onKmzSend);

    // ── 远端控制 (line-delimited JSON server) ──────────────────────────────
    auto *remoteHr = new QFrame(this);
    remoteHr->setFrameShape(QFrame::HLine);
    remoteHr->setStyleSheet("color: #353650;");
    layout->addWidget(remoteHr);

    auto *remoteTitle = new QLabel(QStringLiteral("远端控制 (JSON 侦听)"), this);
    remoteTitle->setStyleSheet("color: #00c8d7; font-family: Consolas; font-weight: bold;");
    layout->addWidget(remoteTitle);

    QSettings persisted;
    const QString def_kmz_dir = persisted
        .value("DroneWidget/kmz_dir",
               QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/UAV_KMZ")
        .toString();
    const int def_port = persisted.value("DroneWidget/remote_port", 7100).toInt();

    auto *dirRow = new QHBoxLayout;
    auto *dirLabel = new QLabel(QStringLiteral("KMZ 目录:"), this);
    dirLabel->setStyleSheet("color: #aab6cc; font-family: Consolas;");
    dirLabel->setFixedWidth(70);
    kmz_dir_edit_ = new QLineEdit(def_kmz_dir, this);
    btn_kmz_dir_browse_ = new QPushButton(QStringLiteral("浏览…"), this);
    btn_kmz_dir_browse_->setFixedWidth(70);
    dirRow->addWidget(dirLabel);
    dirRow->addWidget(kmz_dir_edit_, 1);
    dirRow->addWidget(btn_kmz_dir_browse_);
    layout->addLayout(dirRow);

    auto *listenRow = new QHBoxLayout;
    auto *portLabel = new QLabel(QStringLiteral("侦听端口:"), this);
    portLabel->setStyleSheet("color: #aab6cc; font-family: Consolas;");
    portLabel->setFixedWidth(70);
    remote_port_spin_ = new QSpinBox(this);
    remote_port_spin_->setRange(1024, 65535);
    remote_port_spin_->setValue(def_port);
    remote_port_spin_->setFixedWidth(90);

    btn_remote_toggle_ = new QPushButton(QStringLiteral("开启侦听"), this);
    btn_remote_toggle_->setCheckable(true);
    btn_remote_toggle_->setFixedHeight(28);
    btn_remote_toggle_->setStyleSheet(
        "QPushButton { background:#445; color:#ddd; border-radius:4px;"
        "              padding:2px 14px; font-weight:bold; }"
        "QPushButton:checked { background:#3a8; color:white; }"
        "QPushButton:hover { background:#556; }"
        "QPushButton:checked:hover { background:#4ba; }");

    listenRow->addWidget(portLabel);
    listenRow->addWidget(remote_port_spin_);
    listenRow->addStretch();
    listenRow->addWidget(btn_remote_toggle_);
    layout->addLayout(listenRow);

    remote_status_label_ = new QLabel(QStringLiteral("未侦听"), this);
    remote_status_label_->setStyleSheet("font-family: Consolas; color: #888aaa;");
    layout->addWidget(remote_status_label_);

    auto *remoteHelp = new QLabel(
        QStringLiteral("<i>协议: 每行一个 JSON. 例:<br>"
                       "  {\"id\":1,\"cmd\":\"deploy_kmz\",\"name\":\"plan1\",\"target\":\"192.168.200.102\",\"port\":14550}<br>"
                       "  {\"id\":2,\"cmd\":\"list_kmz\"}    /    {\"id\":3,\"cmd\":\"ping\"}<br>"
                       "回复同 id, 含 ok=true/false. 文件名可省略 .kmz 扩展.</i>"),
        this);
    remoteHelp->setStyleSheet("color: #6b7388;");
    remoteHelp->setWordWrap(true);
    layout->addWidget(remoteHelp);

    connect(btn_kmz_dir_browse_, &QPushButton::clicked, this, &DroneWidget::onKmzDirBrowse);
    connect(btn_remote_toggle_,  &QPushButton::toggled, this, &DroneWidget::onRemoteToggle);
    connect(kmz_dir_edit_, &QLineEdit::editingFinished, this, [this]() {
        QSettings s; s.setValue("DroneWidget/kmz_dir", kmz_dir_edit_->text());
    });
    connect(remote_port_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) {
        QSettings s; s.setValue("DroneWidget/remote_port", v);
    });
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
    return QString("192.168.200.%1").arg(octet);
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
    // While the mesh-widget 仿真 is active, ignore real ping data — the
    // simulated cards are owned by setSimulationTelemetry() and would get
    // clobbered back to "inactive" on the next ping cycle.
    if (sim_active_) {
        return;
    }

    for (const MeshNode &node : nodes) {
        if (node.id < 102 || node.id > 106) {
            continue;
        }
        setNodeActive(node.id, node.reachable);
    }

    refreshDroneStates();
}

void DroneWidget::setSimulationTelemetry(bool active, const QList<MeshNode> &nodes)
{
    sim_active_ = active;

    if (!active) {
        // Drop sim state — mark every card inactive and let the next
        // mesh-ping cycle (or manual refresh) repopulate with real data.
        for (int octet : kDroneNodes) {
            setNodeActive(octet, false);
        }
        setStatus("扫描已停止", "#7f8aa3");
        return;
    }

    // Per-node mock telemetry. Lat/lng cluster around a single point with
    // small decimal-place jitter (5th–6th decimal); altitude pinned at 0.0 m
    // per the demo brief. Battery/heading just exist to look credible.
    struct SimTelemetry {
        int    octet;
        int    batteryPct;
        int    batteryMv;
        int    batteryDc;
        double altM;
        double headingDeg;
        double lat;
        double lng;
        int    gpsSats;
    };
    static const SimTelemetry kSim[] = {
        { 102, 92, 12450, 28, 0.0,  87.0, 22.543021, 113.934152, 14 },
        { 103, 87, 12380, 30, 0.0, 142.0, 22.543174, 113.934283, 13 },
        { 104, 95, 12490, 26, 0.0,  15.0, 22.542958, 113.934017, 15 },
        { 105, 78, 12210, 32, 0.0, 273.0, 22.543112, 113.934378, 12 },
    };

    QHash<int, const SimTelemetry*> simByOctet;
    for (const auto &t : kSim) simByOctet.insert(t.octet, &t);

    // Build the "active" set from the mesh widget's lit nodes so the two
    // views stay in lockstep. Anything outside 102..105 (e.g. HOST .101)
    // is ignored — DroneWidget has no card for it.
    QSet<int> litOctets;
    for (const MeshNode &n : nodes) {
        if (n.reachable && n.id >= 102 && n.id <= 105) {
            litOctets.insert(n.id);
        }
    }

    int lit = 0;
    const QString stamp = QTime::currentTime().toString("hh:mm:ss");
    for (int octet : kDroneNodes) {
        const bool active = litOctets.contains(octet);
        setNodeActive(octet, active);
        if (!active) continue;

        const SimTelemetry *t = simByOctet.value(octet, nullptr);
        if (!t) continue;

        altitude_labels_[octet]->setText(
            dashValue("高度", QString("%1 m").arg(t->altM, 0, 'f', 1)));
        heading_labels_[octet]->setText(
            dashValue("航向", QString("%1 deg").arg(t->headingDeg, 0, 'f', 1)));
        gps_labels_[octet]->setText(
            dashValue("GPS", QString("%1°N, %2°E  sats=%3")
                                .arg(t->lat, 0, 'f', 6)
                                .arg(t->lng, 0, 'f', 6)
                                .arg(t->gpsSats)));
        battery_labels_[octet]->setText(
            dashValue("电池", QString("%1%  %2 mV  %3 dC")
                                .arg(t->batteryPct).arg(t->batteryMv).arg(t->batteryDc)));
        updateNodeTimestamp(octet, stamp);
        emit dronePositionUpdated(octet, t->lat, t->lng);
        ++lit;
    }

    setStatus(QString("已发现 %1 个节点").arg(lit), "#4caf50");
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
    // Sim mode holds the labels — skip the real UDP poll entirely.
    if (sim_active_) {
        return;
    }

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

            // Latitude/longitude field names vary between PSDK builds — accept
            // the common spellings, and de-scale the 1e7 fixed-point form.
            auto pick = [&result](std::initializer_list<const char*> keys) -> double {
                for (const char *k : keys)
                    if (result.contains(QLatin1String(k)))
                        return result.value(QLatin1String(k)).toDouble();
                return 0.0;
            };
            double lat = pick({"lat", "latitude", "lat_deg", "gps_lat"});
            double lng = pick({"lng", "lon", "longitude", "lng_deg", "lon_deg", "gps_lon"});
            if (std::fabs(lat) > 90.0)  lat /= 1e7;
            if (std::fabs(lng) > 180.0) lng /= 1e7;

            altitude_labels_[req.octet]->setText(
                dashValue("高度", QString("%1 m").arg(altRel, 0, 'f', 1)));
            heading_labels_[req.octet]->setText(
                dashValue("航向", QString("%1 deg").arg(heading, 0, 'f', 1)));
            if (lat != 0.0 || lng != 0.0) {
                gps_labels_[req.octet]->setText(
                    dashValue("GPS", QString("%1,%2 fix=%3 sats=%4")
                                        .arg(lat, 0, 'f', 6).arg(lng, 0, 'f', 6)
                                        .arg(gpsFix).arg(gpsSats)));
                emit dronePositionUpdated(req.octet, lat, lng);
            } else {
                gps_labels_[req.octet]->setText(
                    dashValue("GPS", QString("fix=%1 sats=%2").arg(gpsFix).arg(gpsSats)));
            }
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

// ─── KMZ 路径规划下发 ─────────────────────────────────────────────────────────

void DroneWidget::onKmzLoadFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "选择 KMZ 路径规划文件", QString(),
        "KMZ 文件 (*.kmz);;所有文件 (*)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        kmz_status_label_->setText("错误: 无法打开文件");
        kmz_status_label_->setStyleSheet("font-family: Consolas; color: #f44336;");
        return;
    }
    kmz_data_ = f.readAll();
    f.close();

    kmz_path_edit_->setText(path);
    kmz_status_label_->setText(QString("已加载  %1 字节").arg(kmz_data_.size()));
    kmz_status_label_->setStyleSheet("font-family: Consolas; color: #4caf50;");
    btn_kmz_send_->setEnabled(!kmz_data_.isEmpty());
}

void DroneWidget::onKmzSend()
{
    if (kmz_data_.isEmpty()) {
        kmz_status_label_->setText("错误: 请先选择 KMZ 文件");
        kmz_status_label_->setStyleSheet("font-family: Consolas; color: #f44336;");
        return;
    }
    if (kmz_socket_->state() != QAbstractSocket::UnconnectedState) {
        kmz_socket_->abort();
    }

    const QString ip   = kmz_ip_combo_->currentText().trimmed();
    const quint16 port = static_cast<quint16>(kmz_port_spin_->value());

    kmz_bytes_written_ = 0;
    btn_kmz_send_->setEnabled(false);
    kmz_status_label_->setText(QString("正在连接 %1:%2 …").arg(ip).arg(port));
    kmz_status_label_->setStyleSheet("font-family: Consolas; color: #ff9800;");
    kmz_socket_->connectToHost(ip, port);
}

void DroneWidget::onKmzConnected()
{
    kmz_status_label_->setText(
        QString("连接成功，正在发送 %1 字节…").arg(kmz_data_.size()));
    kmz_status_label_->setStyleSheet("font-family: Consolas; color: #ff9800;");
    kmz_socket_->write(kmz_data_);
}

void DroneWidget::onKmzBytesWritten(qint64 bytes)
{
    kmz_bytes_written_ += bytes;
    if (kmz_bytes_written_ >= kmz_data_.size()) {
        kmz_socket_->disconnectFromHost();
    }
}

void DroneWidget::onKmzDisconnected()
{
    const bool ok = kmz_bytes_written_ >= kmz_data_.size() && !kmz_data_.isEmpty();
    if (ok) {
        kmz_status_label_->setText(
            QString("下发完成  %1 字节").arg(kmz_bytes_written_));
        kmz_status_label_->setStyleSheet("font-family: Consolas; color: #4caf50;");
    } else {
        kmz_status_label_->setText("连接已断开");
        kmz_status_label_->setStyleSheet("font-family: Consolas; color: #888aaa;");
    }
    btn_kmz_send_->setEnabled(!kmz_data_.isEmpty());

    // If this deployment was kicked off by a remote command, send the
    // final result back to that client and clear pending state. Any
    // failure path that goes through onKmzError handles its own reply.
    if (pending_remote_client_) {
        QJsonObject reply;
        reply["id"]    = pending_remote_req_id_;
        reply["cmd"]   = "deploy_kmz";
        reply["ok"]    = ok;
        reply["stage"] = "done";
        reply["name"]  = pending_remote_name_;
        reply["bytes"] = qint64(kmz_bytes_written_);
        reply["target"] = kmz_ip_combo_->currentText();
        reply["port"]   = kmz_port_spin_->value();
        if (!ok) reply["error"] = "transfer incomplete";
        sendRemoteReply(pending_remote_client_, reply);
        pending_remote_client_ = nullptr;
        pending_remote_req_id_ = 0;
        pending_remote_name_.clear();
    }
}

void DroneWidget::onKmzError(QAbstractSocket::SocketError)
{
    kmz_status_label_->setText("错误: " + kmz_socket_->errorString());
    kmz_status_label_->setStyleSheet("font-family: Consolas; color: #f44336;");
    btn_kmz_send_->setEnabled(!kmz_data_.isEmpty());

    // If this failure was a remote-triggered deployment, report back.
    if (pending_remote_client_) {
        QJsonObject reply;
        reply["id"]    = pending_remote_req_id_;
        reply["ok"]    = false;
        reply["stage"] = "send_error";
        reply["error"] = kmz_socket_->errorString();
        if (!pending_remote_name_.isEmpty()) reply["name"] = pending_remote_name_;
        sendRemoteReply(pending_remote_client_, reply);
        pending_remote_client_ = nullptr;
        pending_remote_req_id_ = 0;
        pending_remote_name_.clear();
    }
}

// ─── 远端 JSON 控制 ───────────────────────────────────────────────────────────

void DroneWidget::onKmzDirBrowse()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择 KMZ 库目录"),
        kmz_dir_edit_->text());
    if (dir.isEmpty()) return;
    kmz_dir_edit_->setText(dir);
    QSettings s; s.setValue("DroneWidget/kmz_dir", dir);
}

void DroneWidget::onRemoteToggle(bool checked)
{
    if (!checked) {
        if (remote_server_) {
            remote_server_->close();
            remote_server_->deleteLater();
            remote_server_ = nullptr;
        }
        btn_remote_toggle_->setText(QStringLiteral("开启侦听"));
        remote_status_label_->setText(QStringLiteral("未侦听"));
        remote_status_label_->setStyleSheet("font-family: Consolas; color: #888aaa;");
        return;
    }

    const quint16 port = static_cast<quint16>(remote_port_spin_->value());
    remote_server_ = new QTcpServer(this);
    if (!remote_server_->listen(QHostAddress::Any, port)) {
        remote_status_label_->setText(
            QStringLiteral("侦听失败: %1").arg(remote_server_->errorString()));
        remote_status_label_->setStyleSheet("font-family: Consolas; color: #f44336;");
        remote_server_->deleteLater();
        remote_server_ = nullptr;
        btn_remote_toggle_->setChecked(false);
        return;
    }
    connect(remote_server_, &QTcpServer::newConnection,
            this, &DroneWidget::onRemoteNewConnection);
    btn_remote_toggle_->setText(QStringLiteral("停止侦听"));
    remote_status_label_->setText(
        QStringLiteral("侦听中 0.0.0.0:%1").arg(port));
    remote_status_label_->setStyleSheet("font-family: Consolas; color: #4caf50;");
}

void DroneWidget::onRemoteNewConnection()
{
    if (!remote_server_) return;
    while (remote_server_->hasPendingConnections()) {
        QTcpSocket *client = remote_server_->nextPendingConnection();
        // Buffer line-delimited JSON per client; the buffer lives on the
        // socket via dynamic property so we don't need a per-socket hash.
        client->setProperty("rx_buf", QByteArray());
        connect(client, &QTcpSocket::readyRead,
                this, &DroneWidget::onRemoteClientReadyRead);
        connect(client, &QTcpSocket::disconnected,
                this, &DroneWidget::onRemoteClientDisconnected);
        remote_status_label_->setText(
            QStringLiteral("已连接 %1:%2")
                .arg(client->peerAddress().toString()).arg(client->peerPort()));
    }
}

void DroneWidget::onRemoteClientReadyRead()
{
    auto *client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;
    QByteArray buf = client->property("rx_buf").toByteArray();
    buf.append(client->readAll());
    int nl;
    while ((nl = buf.indexOf('\n')) >= 0) {
        QByteArray line = buf.left(nl).trimmed();
        buf.remove(0, nl + 1);
        if (line.isEmpty()) continue;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            QJsonObject reply;
            reply["ok"]    = false;
            reply["error"] = QStringLiteral("parse error: %1").arg(err.errorString());
            sendRemoteReply(client, reply);
            continue;
        }
        handleRemoteCommand(client, doc.object());
    }
    client->setProperty("rx_buf", buf);
}

void DroneWidget::onRemoteClientDisconnected()
{
    auto *client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;
    if (pending_remote_client_ == client) {
        pending_remote_client_ = nullptr;
        pending_remote_req_id_ = 0;
        pending_remote_name_.clear();
    }
    client->deleteLater();
}

void DroneWidget::sendRemoteReply(QTcpSocket *client, const QJsonObject &reply)
{
    if (!client || client->state() != QAbstractSocket::ConnectedState) return;
    const QByteArray bytes = QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n';
    client->write(bytes);
    client->flush();
}

QString DroneWidget::resolveKmzPath(const QString &name) const
{
    const QString dir = kmz_dir_edit_->text().trimmed();
    if (dir.isEmpty() || name.isEmpty()) return {};
    // Sanitize: reject path-traversal in name.
    if (name.contains("..") || name.contains('/') || name.contains('\\')) return {};
    QDir d(dir);
    // Try name as-is first, then with .kmz extension.
    if (d.exists(name)) return d.absoluteFilePath(name);
    const QString with_ext = name + QStringLiteral(".kmz");
    if (d.exists(with_ext)) return d.absoluteFilePath(with_ext);
    return {};
}

bool DroneWidget::beginRemoteDeploy(const QString &full_path,
                                     const QString &target_ip,
                                     quint16 target_port,
                                     QString *err_out)
{
    if (pending_remote_client_) {
        if (err_out) *err_out = QStringLiteral("busy: a remote deployment is already in progress");
        return false;
    }
    if (kmz_socket_ && kmz_socket_->state() != QAbstractSocket::UnconnectedState) {
        if (err_out) *err_out = QStringLiteral("busy: a manual deployment is already in progress");
        return false;
    }
    QFile f(full_path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err_out) *err_out = QStringLiteral("open failed: %1").arg(f.errorString());
        return false;
    }
    kmz_data_ = f.readAll();
    f.close();
    if (kmz_data_.isEmpty()) {
        if (err_out) *err_out = QStringLiteral("file is empty");
        return false;
    }
    kmz_path_edit_->setText(full_path);

    // Honor explicit target IP if given; otherwise stick with whatever the
    // dropdown currently shows.
    if (!target_ip.isEmpty()) {
        int idx = kmz_ip_combo_->findText(target_ip);
        if (idx >= 0) {
            kmz_ip_combo_->setCurrentIndex(idx);
        } else {
            // Add it (in case the remote names a node not in our preset).
            kmz_ip_combo_->insertItem(0, target_ip);
            kmz_ip_combo_->setCurrentIndex(0);
        }
    }
    if (target_port > 0) kmz_port_spin_->setValue(target_port);

    kmz_bytes_written_ = 0;
    btn_kmz_send_->setEnabled(false);
    kmz_status_label_->setText(
        QStringLiteral("[远端] 下发 %1 (%2 字节) → %3:%4")
            .arg(QFileInfo(full_path).fileName())
            .arg(kmz_data_.size())
            .arg(kmz_ip_combo_->currentText())
            .arg(kmz_port_spin_->value()));
    kmz_status_label_->setStyleSheet("font-family: Consolas; color: #ff9800;");
    if (kmz_socket_->state() != QAbstractSocket::UnconnectedState) {
        kmz_socket_->abort();
    }
    kmz_socket_->connectToHost(kmz_ip_combo_->currentText().trimmed(),
                               static_cast<quint16>(kmz_port_spin_->value()));
    return true;
}

void DroneWidget::handleRemoteCommand(QTcpSocket *client, const QJsonObject &req)
{
    const int req_id    = req.value("id").toInt(0);
    const QString cmd   = req.value("cmd").toString();
    QJsonObject reply;
    reply["id"]  = req_id;
    reply["cmd"] = cmd;

    if (cmd == "ping") {
        reply["ok"] = true;
        sendRemoteReply(client, reply);
        return;
    }

    if (cmd == "list_kmz") {
        const QString dir_path = kmz_dir_edit_->text().trimmed();
        QDir d(dir_path);
        if (!d.exists()) {
            reply["ok"]    = false;
            reply["error"] = QStringLiteral("kmz dir not found: %1").arg(dir_path);
            sendRemoteReply(client, reply);
            return;
        }
        QJsonArray files;
        for (const QString &name :
             d.entryList({"*.kmz"}, QDir::Files | QDir::Readable, QDir::Name)) {
            files.append(name);
        }
        reply["ok"]    = true;
        reply["dir"]   = d.absolutePath();
        reply["files"] = files;
        sendRemoteReply(client, reply);
        return;
    }

    if (cmd == "deploy_kmz") {
        const QString name = req.value("name").toString();
        const QString target_ip =
            req.value("target").toString(kmz_ip_combo_->currentText());
        const int target_port = req.value("port").toInt(kmz_port_spin_->value());
        if (name.isEmpty()) {
            reply["ok"]    = false;
            reply["error"] = "missing 'name'";
            sendRemoteReply(client, reply);
            return;
        }
        const QString full_path = resolveKmzPath(name);
        if (full_path.isEmpty()) {
            reply["ok"]    = false;
            reply["error"] = QStringLiteral("file not found: %1 (in %2)")
                                 .arg(name, kmz_dir_edit_->text());
            sendRemoteReply(client, reply);
            return;
        }
        QString err;
        if (!beginRemoteDeploy(full_path, target_ip,
                                static_cast<quint16>(target_port), &err)) {
            reply["ok"]    = false;
            reply["error"] = err;
            sendRemoteReply(client, reply);
            return;
        }
        // Record so onKmzDisconnected / onKmzError can deliver the final
        // result back to this caller.
        pending_remote_client_ = client;
        pending_remote_req_id_ = req_id;
        pending_remote_name_   = name;

        // Send an interim ack so the remote knows we accepted the request.
        QJsonObject ack;
        ack["id"]      = req_id;
        ack["cmd"]     = cmd;
        ack["ok"]      = true;
        ack["stage"]   = "accepted";
        ack["name"]    = name;
        ack["resolved"]= full_path;
        ack["target"]  = target_ip;
        ack["port"]    = target_port;
        ack["bytes"]   = kmz_data_.size();
        sendRemoteReply(client, ack);
        return;
    }

    reply["ok"]    = false;
    reply["error"] = QStringLiteral("unknown cmd: %1").arg(cmd);
    sendRemoteReply(client, reply);
}
