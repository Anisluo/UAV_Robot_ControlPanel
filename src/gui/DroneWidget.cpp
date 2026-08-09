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
#include <QMessageBox>
#include <QProgressBar>
#include <QElapsedTimer>

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
    , takeoff_poll_timer_(new QTimer(this))
    , rpc_timeout_timer_(new QTimer(this))
    , rpc_clock_(new QElapsedTimer)
{
    rpc_clock_->start();
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

    takeoff_poll_timer_->setInterval(1000);       // spec: poll flight_status 1-5 Hz
    connect(takeoff_poll_timer_, &QTimer::timeout, this, &DroneWidget::onTakeoffPoll);
    rpc_timeout_timer_->setInterval(250);         // resolution for the 5s budget
    connect(rpc_timeout_timer_, &QTimer::timeout, this, &DroneWidget::onRpcTimeoutTick);

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
    // This combo is the single target for EVERY drone command — KMZ upload,
    // mission control, and 起飞/降落/返航 alike. Default to .103 (the M3E
    // board) rather than the first list entry: silently defaulting to .102
    // would aim a takeoff at whichever node happened to sort first.
    {
        const int idx103 = kmz_ip_combo_->findText(QStringLiteral("192.168.200.103"));
        if (idx103 >= 0) kmz_ip_combo_->setCurrentIndex(idx103);
    }
    kmz_ip_combo_->setToolTip(
        QStringLiteral("所有无人机指令 (起飞/降落/返航/KMZ/航线) 的目标节点。\n"
                       "PSDK 协议端口固定 UDP 5555。"));

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

    kmz_progress_ = new QProgressBar(this);
    kmz_progress_->setTextVisible(false);
    kmz_progress_->setFixedHeight(6);
    kmz_progress_->setVisible(false);
    layout->addWidget(kmz_progress_);

    kmz_status_label_ = new QLabel("就绪", this);
    kmz_status_label_->setStyleSheet("font-family: Consolas; color: #888aaa;");
    layout->addWidget(kmz_status_label_);

    // 航线执行 — the aircraft holds the uploaded route until told to fly it.
    auto *missionRow = new QHBoxLayout;
    auto *missionLabel = new QLabel("航线执行:", this);
    missionLabel->setStyleSheet("color: #00c8d7; font-family: Consolas;");
    missionLabel->setFixedWidth(64);
    missionRow->addWidget(missionLabel);
    struct { const char *text; const char *method; } kMission[] = {
        {"开始", "drone.start_mission"},
        {"暂停", "drone.pause_mission"},
        {"继续", "drone.resume_mission"},
        {"停止", "drone.stop_mission"},
    };
    QPushButton **slots_[] = {&btn_mission_start_, &btn_mission_pause_,
                              &btn_mission_resume_, &btn_mission_stop_};
    for (int i = 0; i < 4; ++i) {
        auto *b = new QPushButton(QString::fromUtf8(kMission[i].text), this);
        b->setFixedHeight(26);
        b->setProperty("psdk_method", QString::fromLatin1(kMission[i].method));
        connect(b, &QPushButton::clicked, this, &DroneWidget::onMissionCommand);
        missionRow->addWidget(b);
        *slots_[i] = b;
    }
    missionRow->addStretch();
    layout->addLayout(missionRow);

    connect(btn_kmz_load_, &QPushButton::clicked, this, &DroneWidget::onKmzLoadFile);
    // 下发 now speaks the documented protocol: drone.kmz_begin / kmz_chunk /
    // kmz_end over the same UDP :5555 socket as every other command. The old
    // raw-TCP push (onKmzSend) is still reachable from the remote-control
    // listener path, which has not been migrated.
    btn_kmz_send_->setText("下 发");
    btn_kmz_send_->setToolTip(
        QStringLiteral("按 PSDK 协议分块下发: drone.kmz_begin → kmz_chunk(base64) → kmz_end\n"
                       "目标端口固定为协议规定的 UDP 5555, 与上方端口框无关。"));
    btn_kmz_upload_spec_ = btn_kmz_send_;
    connect(btn_kmz_send_, &QPushButton::clicked, this, &DroneWidget::onKmzUploadSpec);

    // ── 一键起飞 ──────────────────────────────────────────────────────────
    auto *flyHr = new QFrame(this);
    flyHr->setFrameShape(QFrame::HLine);
    flyHr->setStyleSheet("color: #3a3f52;");
    layout->addWidget(flyHr);

    auto *flyRow = new QHBoxLayout;
    btn_takeoff_ = new QPushButton("一键起飞", this);
    btn_takeoff_->setFixedHeight(30);
    btn_takeoff_->setStyleSheet(
        "QPushButton { background:#2f8f4f; color:white; font-weight:bold;"
        "              padding:4px 16px; border-radius:4px; }"
        "QPushButton:hover { background:#37a75d; }"
        "QPushButton:disabled { background:#3a3d52; color:#7c7f96; }");
    auto *btn_land = new QPushButton("降 落", this);
    auto *btn_home = new QPushButton("返 航", this);
    for (QPushButton *b : {btn_land, btn_home}) b->setFixedHeight(30);
    btn_land->setProperty("psdk_method", QStringLiteral("drone.land"));
    btn_home->setProperty("psdk_method", QStringLiteral("drone.go_home"));
    connect(btn_land, &QPushButton::clicked, this, &DroneWidget::onMissionCommand);
    connect(btn_home, &QPushButton::clicked, this, &DroneWidget::onMissionCommand);
    connect(btn_takeoff_, &QPushButton::clicked, this, &DroneWidget::onTakeoff);

    flyRow->addWidget(btn_takeoff_);
    flyRow->addWidget(btn_land);
    flyRow->addWidget(btn_home);
    flyRow->addStretch();
    layout->addLayout(flyRow);

    takeoff_status_ = new QLabel(this);
    takeoff_status_->setStyleSheet("font-family: Consolas; color: #888aaa;");
    layout->addWidget(takeoff_status_);

    // Always show which node the buttons will actually command. Two of the
    // three flight buttons are irreversible once they land on the wrong
    // aircraft, so the target must never be something you have to go
    // hunting for in another section of the panel.
    auto showTarget = [this]() {
        takeoff_status_->setText(
            QString("就绪 — 目标 %1 : 5555，起飞前先查 gps_fix")
                .arg(kmz_ip_combo_->currentText()));
        takeoff_status_->setStyleSheet("font-family: Consolas; color: #888aaa;");
    };
    connect(kmz_ip_combo_, &QComboBox::currentTextChanged, this, showTarget);
    showTarget();

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
    // Telemetry poll: fire-and-forget on purpose. It repeats every refresh
    // tick anyway, so a retry would only pile duplicate datagrams onto a
    // link that is already dropping them.
    PendingRequest req;
    req.octet = octet;
    req.method = method;
    req.retries_left = 0;
    req.timeout_ms = 5000;
    const int requestId = next_request_id_++;
    pending_requests_.insert(requestId, req);
    transmit(requestId, req);
}

// One datagram = one complete JSON message (spec §Transport). psdkd caps a
// datagram at 4096 bytes; anything larger is a programming error here, not
// something to discover on the wire, so it is refused loudly.
void DroneWidget::transmit(int request_id, const PendingRequest &req)
{
    QJsonObject obj;
    obj["jsonrpc"] = QStringLiteral("2.0");
    obj["id"]      = request_id;
    obj["method"]  = req.method;
    if (!req.params.isEmpty()) {
        obj["params"] = req.params;      // spec: params omissible when empty
    }

    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    if (payload.size() > 4096) {
        qWarning("DroneWidget: %s datagram %d bytes exceeds the 4096 limit",
                 qPrintable(req.method), payload.size());
        return;
    }
    udp_socket_->writeDatagram(payload, QHostAddress(nodeIp(req.octet)), kDroneRpcPort);
}

void DroneWidget::callRpc(int octet, const QString &method, const QJsonObject &params,
                          RpcCallback cb, int timeout_ms, int retries)
{
    PendingRequest req;
    req.octet        = octet;
    req.method       = method;
    req.params       = params;
    req.cb           = std::move(cb);
    req.retries_left = retries;
    req.timeout_ms   = timeout_ms;
    req.sent_ms      = rpc_clock_->elapsed();

    const int requestId = next_request_id_++;
    pending_requests_.insert(requestId, req);
    transmit(requestId, req);
    if (rpc_timeout_timer_ && !rpc_timeout_timer_->isActive()) {
        rpc_timeout_timer_->start();
    }
}

// Retransmit or give up. UDP is connectionless, so a lost request and a dead
// peer look identical from here — the only difference is how many attempts
// we have spent.
void DroneWidget::onRpcTimeoutTick()
{
    const qint64 now = rpc_clock_->elapsed();
    QList<int> expired;
    for (auto it = pending_requests_.begin(); it != pending_requests_.end(); ++it) {
        if (!it.value().cb) continue;                       // telemetry: no timeout
        if (now - it.value().sent_ms >= it.value().timeout_ms) expired.append(it.key());
    }

    for (int id : expired) {
        PendingRequest req = pending_requests_.value(id);
        if (req.retries_left > 0) {
            req.retries_left--;
            req.sent_ms = now;
            pending_requests_[id] = req;
            transmit(id, req);                              // same id — see callRpc
            continue;
        }
        pending_requests_.remove(id);
        if (req.cb) {
            req.cb(false, QJsonObject{},
                   QString("%1 无响应 (超时 %2ms, 已重试 3 次)")
                       .arg(req.method).arg(req.timeout_ms));
        }
    }

    bool any_tracked = false;
    for (auto it = pending_requests_.begin(); it != pending_requests_.end(); ++it) {
        if (it.value().cb) { any_tracked = true; break; }
    }
    if (!any_tracked && rpc_timeout_timer_) rpc_timeout_timer_->stop();
}

// ── KMZ 航线下发 (spec §KMZ Waypoint Mission Upload) ────────────────────
//
// drone.kmz_begin {file,size} → {ready,chunk_raw_max}
//   loop  drone.kmz_chunk {seq,data:base64} → {ack,received}
// drone.kmz_end {upload:1} → {file,size,uploaded}
//
// Strictly sequential: the next chunk is only sent after the current one is
// acked. That is not just politeness — the server rejects out-of-order seq
// with "bad seq (expected N)", and over UDP "sent" says nothing about
// "arrived", so a windowed sender would reorder itself into that error.
int DroneWidget::kmzTargetOctet() const
{
    const QString ip = kmz_ip_combo_ ? kmz_ip_combo_->currentText() : QString();
    const int dot = ip.lastIndexOf('.');
    return dot < 0 ? 0 : ip.mid(dot + 1).toInt();
}

void DroneWidget::setKmzStatus(const QString &text, const QString &color)
{
    if (!kmz_status_label_) return;
    kmz_status_label_->setText(text);
    kmz_status_label_->setStyleSheet(
        QString("color:%1; font-family: Consolas;").arg(color));
}

void DroneWidget::onKmzUploadSpec()
{
    if (kmz_spec_busy_) {
        setKmzStatus("上一次下发尚未结束", "#e0a030");
        return;
    }
    const QString path = kmz_path_edit_ ? kmz_path_edit_->text().trimmed() : QString();
    if (path.isEmpty()) {
        setKmzStatus("未选择 KMZ 文件", "#e05050");
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setKmzStatus("无法打开文件: " + f.errorString(), "#e05050");
        return;
    }
    const QByteArray bytes = f.readAll();
    f.close();

    if (bytes.isEmpty()) {
        setKmzStatus("文件为空", "#e05050");
        return;
    }
    if (bytes.size() > 64LL * 1024 * 1024) {          // spec: max 64 MB
        setKmzStatus(QString("文件 %1 MB 超过协议上限 64MB")
                         .arg(bytes.size() / 1048576.0, 0, 'f', 1), "#e05050");
        return;
    }

    // Spec filename rules: UTF-8, ≤200 bytes, no '/', "..", quote,
    // backslash or control characters. Validated before the first datagram
    // so a bad name fails here instead of half-way through the transfer.
    const QString name = QFileInfo(path).fileName();
    const QByteArray name_utf8 = name.toUtf8();
    QString bad;
    if (name_utf8.size() > 200)                      bad = "文件名超过 200 字节";
    else if (name.contains('/') || name.contains('\\')) bad = "文件名含路径分隔符";
    else if (name.contains(QStringLiteral(".."))) bad = "文件名含 ..";
    else if (name.contains('"') || name.contains('\'')) bad = "文件名含引号";
    else {
        for (QChar c : name) {
            if (c.unicode() < 0x20 || c.unicode() == 0x7F) { bad = "文件名含控制字符"; break; }
        }
    }
    if (!bad.isEmpty()) {
        setKmzStatus("文件名不合规: " + bad, "#e05050");
        return;
    }

    const int octet = kmzTargetOctet();
    if (octet <= 0) {
        setKmzStatus("未选择目标节点", "#e05050");
        return;
    }

    kmz_spec_bytes_    = bytes;
    kmz_spec_filename_ = name;
    kmz_spec_octet_    = octet;
    kmz_next_seq_      = 0;
    kmz_sent_bytes_    = 0;
    kmz_chunk_raw_max_ = 2048;
    kmz_spec_busy_     = true;
    if (btn_kmz_upload_spec_) btn_kmz_upload_spec_->setEnabled(false);
    if (kmz_progress_) { kmz_progress_->setRange(0, bytes.size()); kmz_progress_->setValue(0);
                         kmz_progress_->setVisible(true); }

    setKmzStatus(QString("kmz_begin: %1 (%2 字节) → %3")
                     .arg(name).arg(bytes.size()).arg(nodeIp(octet)), "#e0a030");

    QJsonObject params;
    params["file"] = name;
    params["size"] = bytes.size();
    callRpc(octet, QStringLiteral("drone.kmz_begin"), params,
        [this](bool ok, const QJsonObject &res, const QString &err) {
            if (!ok) { kmzFinish(false, "kmz_begin 失败: " + err); return; }
            if (!res.value("ready").toBool(false)) {
                kmzFinish(false, "kmz_begin 未就绪 (ready != true)");
                return;
            }
            // Honour the server's advertised chunk size rather than assuming
            // 2048 — the doc calls 2048 the current value, not a constant.
            const int adv = res.value("chunk_raw_max").toInt(2048);
            kmz_chunk_raw_max_ = qBound(256, adv, 2048);
            setKmzStatus(QString("已就绪, 分块 %1 字节, 开始传输…")
                             .arg(kmz_chunk_raw_max_), "#e0a030");
            kmzSendNextChunk();
        });
}

void DroneWidget::kmzSendNextChunk()
{
    if (kmz_sent_bytes_ >= kmz_spec_bytes_.size()) {
        // All chunks acked → commit. upload:1 pushes on to the aircraft,
        // which is slow, hence the spec's 15s timeout for this one call.
        setKmzStatus("传输完成, kmz_end 推送到飞机中… (最长 15s)", "#e0a030");
        QJsonObject params;
        params["upload"] = 1;
        callRpc(kmz_spec_octet_, QStringLiteral("drone.kmz_end"), params,
            [this](bool ok, const QJsonObject &res, const QString &err) {
                if (!ok) { kmzFinish(false, "kmz_end 失败: " + err); return; }
                if (!res.value("uploaded").toBool(false)) {
                    kmzFinish(false, "kmz_end 返回 uploaded != true");
                    return;
                }
                kmzFinish(true, QString("✓ 航线已下发: %1 (%2 字节)")
                                    .arg(res.value("file").toString(kmz_spec_filename_))
                                    .arg(res.value("size").toInt(kmz_spec_bytes_.size())));
            }, 15000, 3);
        return;
    }

    const int len = qMin(kmz_chunk_raw_max_, kmz_spec_bytes_.size() - kmz_sent_bytes_);
    const QByteArray raw = kmz_spec_bytes_.mid(kmz_sent_bytes_, len);
    const int seq = kmz_next_seq_;

    QJsonObject params;
    params["seq"]  = seq;
    params["data"] = QString::fromLatin1(raw.toBase64());

    callRpc(kmz_spec_octet_, QStringLiteral("drone.kmz_chunk"), params,
        [this, len, seq](bool ok, const QJsonObject &res, const QString &err) {
            if (!ok) {
                kmzFinish(false, QString("分块 %1 失败: %2").arg(seq).arg(err));
                return;
            }
            // A mismatched ack means our seq and the server's disagree, and
            // continuing would just walk further out of step. Abort with both
            // numbers shown — the upload is idempotent, so a clean retry from
            // seq 0 is safe and is the honest recovery.
            const QJsonValue ackv = res.value("ack");
            if (ackv.isDouble() && ackv.toInt() != seq) {
                kmzFinish(false, QString("分块乱序: 发送 seq=%1, 服务端 ack=%2")
                                     .arg(seq).arg(ackv.toInt()));
                return;
            }
            kmz_sent_bytes_ += len;
            kmz_next_seq_   = seq + 1;
            if (kmz_progress_) kmz_progress_->setValue(kmz_sent_bytes_);
            setKmzStatus(QString("传输中 %1 / %2 字节 (%3%)  seq=%4")
                             .arg(kmz_sent_bytes_).arg(kmz_spec_bytes_.size())
                             .arg(100.0 * kmz_sent_bytes_ / kmz_spec_bytes_.size(), 0, 'f', 1)
                             .arg(kmz_next_seq_), "#e0a030");
            kmzSendNextChunk();
        });
}

void DroneWidget::kmzFinish(bool ok, const QString &message)
{
    kmz_spec_busy_ = false;
    kmz_spec_bytes_.clear();
    if (btn_kmz_upload_spec_) btn_kmz_upload_spec_->setEnabled(true);
    if (kmz_progress_ && ok) kmz_progress_->setValue(kmz_progress_->maximum());
    setKmzStatus(message, ok ? "#3ac06a" : "#e05050");
}

// start / stop / pause / resume — which one is decided by the sender button.
void DroneWidget::onMissionCommand()
{
    auto *btn = qobject_cast<QPushButton *>(sender());
    if (!btn) return;
    const QString method = btn->property("psdk_method").toString();
    if (method.isEmpty()) return;

    const int octet = kmzTargetOctet();
    if (octet <= 0) { setKmzStatus("未选择目标节点", "#e05050"); return; }

    setKmzStatus(method + " 已发送…", "#e0a030");
    callRpc(octet, method, QJsonObject{},
        [this, method](bool ok, const QJsonObject &res, const QString &err) {
            if (!ok) { setKmzStatus(method + " 失败: " + err, "#e05050"); return; }
            const QString st = res.value("mission").toString();
            setKmzStatus(QString("✓ %1 → %2").arg(method).arg(st.isEmpty() ? "ok" : st),
                         "#3ac06a");
        });
}

// ── 一键起飞 ────────────────────────────────────────────────────────────
//
// Spec §Takeoff workflow: check gps_fix ∈ [2,4] → send drone.takeoff →
// poll flight_status (0→1→2) → watch motors_on.
//
// The GPS gate is checked here rather than left to the aircraft so the
// operator sees WHY it was refused before anything spins. psdkd will also
// refuse on its own; `force:1` overrides both, and is only ever sent after
// an explicit confirmation.
void DroneWidget::onTakeoff()
{
    const int octet = kmzTargetOctet();
    if (octet <= 0) {
        setStatus("未选择目标节点", "#e05050");
        return;
    }
    takeoff_octet_ = octet;
    btn_takeoff_->setEnabled(false);
    takeoff_status_->setText(QString("查询 %1 GPS 状态…").arg(nodeIp(octet)));
    takeoff_status_->setStyleSheet("color:#e0a030; font-family: Consolas;");

    callRpc(octet, QStringLiteral("drone.get_telemetry"), QJsonObject{},
        [this, octet](bool ok, const QJsonObject &tel, const QString &err) {
            if (!ok) {
                takeoff_status_->setText("起飞中止: " + err);
                takeoff_status_->setStyleSheet("color:#e05050; font-family: Consolas;");
                btn_takeoff_->setEnabled(true);
                return;
            }
            const int fix  = tel.value("gps_fix").toInt(0);
            const int sats = tel.value("gps_sats").toInt(0);
            const int fs   = tel.value("flight_status").toInt(0);

            if (fs != 0) {
                takeoff_status_->setText(
                    QString("已在空中 (flight_status=%1), 无需起飞").arg(fs));
                takeoff_status_->setStyleSheet("color:#e0a030; font-family: Consolas;");
                btn_takeoff_->setEnabled(true);
                return;
            }

            // gps_fix: 0=none 1=DR 2=2D 3=3D 4=GPS+DR 5=timing-only.
            // 5 is deliberately NOT accepted — it is a time-only solution
            // with no usable position, despite being the largest value.
            const bool gps_ok = (fix >= 2 && fix <= 4);
            if (!gps_ok) {
                const auto answer = QMessageBox::warning(this,
                    QStringLiteral("GPS 未定位"),
                    QString("当前 gps_fix=%1 (%2), 卫星 %3 颗 — 不满足起飞条件。\n\n"
                            "强制起飞会绕过定位检查, 无人机可能漂移且无法返航。\n"
                            "确定要发送 force:1 强制起飞吗?")
                        .arg(fix)
                        .arg(fix == 0 ? "无定位" : fix == 1 ? "仅航位推算"
                                                            : fix == 5 ? "仅授时" : "未知")
                        .arg(sats),
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
                if (answer != QMessageBox::Yes) {
                    takeoff_status_->setText("已取消 (GPS 未定位)");
                    takeoff_status_->setStyleSheet("color:#888aaa; font-family: Consolas;");
                    btn_takeoff_->setEnabled(true);
                    return;
                }
            }

            QJsonObject params;
            if (!gps_ok) params["force"] = 1;

            takeoff_status_->setText(gps_ok ? QString("起飞中… (gps_fix=%1, %2 星)")
                                                  .arg(fix).arg(sats)
                                            : QStringLiteral("强制起飞中… (force:1)"));
            callRpc(octet, QStringLiteral("drone.takeoff"), params,
                [this](bool ok2, const QJsonObject &res, const QString &err2) {
                    if (!ok2) {
                        takeoff_status_->setText("起飞失败: " + err2);
                        takeoff_status_->setStyleSheet("color:#e05050; font-family: Consolas;");
                        btn_takeoff_->setEnabled(true);
                        return;
                    }
                    if (!res.value("takeoff").toBool(false)) {
                        takeoff_status_->setText("起飞被拒 (takeoff != true)");
                        takeoff_status_->setStyleSheet("color:#e05050; font-family: Consolas;");
                        btn_takeoff_->setEnabled(true);
                        return;
                    }
                    // Accepted — now confirm it actually leaves the ground.
                    // A "true" here only means the command was taken.
                    takeoff_polls_left_ = 30;          // 30 × 1s ≈ 30s
                    takeoff_status_->setText("已接受, 等待离地…");
                    takeoff_poll_timer_->start();
                });
        });
}

void DroneWidget::onTakeoffPoll()
{
    if (--takeoff_polls_left_ < 0) {
        takeoff_poll_timer_->stop();
        takeoff_status_->setText("起飞已发出, 但 30s 内未确认离地 — 请目视确认");
        takeoff_status_->setStyleSheet("color:#e0a030; font-family: Consolas;");
        btn_takeoff_->setEnabled(true);
        return;
    }
    callRpc(takeoff_octet_, QStringLiteral("drone.get_telemetry"), QJsonObject{},
        [this](bool ok, const QJsonObject &tel, const QString &) {
            if (!ok) return;                       // transient — keep polling
            const int fs = tel.value("flight_status").toInt(0);
            const bool motors = tel.value("motors_on").toBool(false);
            const double alt = tel.value("alt_rel_m").toDouble();

            if (fs == 2) {                          // 2 = airborne
                takeoff_poll_timer_->stop();
                takeoff_status_->setText(QString("✓ 已起飞 — 相对高度 %1m").arg(alt, 0, 'f', 1));
                takeoff_status_->setStyleSheet("color:#3ac06a; font-family: Consolas; font-weight:bold;");
                btn_takeoff_->setEnabled(true);
                return;
            }
            takeoff_status_->setText(
                QString("起飞中… flight_status=%1%2 高度 %3m")
                    .arg(fs).arg(motors ? " 电机已转" : " 电机未转").arg(alt, 0, 'f', 1));
        }, 2000, 0);       // short timeout, no retry: the next tick re-asks
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
            continue;               // spec: discard unmatched datagrams
        }

        const PendingRequest req = pending_requests_.take(requestId);

        // Command replies (takeoff / KMZ / mission) go to their callback.
        // Checked before the active-node filter: a command is issued against
        // an explicitly chosen target, and dropping its reply because the
        // node card happens to be inactive would hang the state machine.
        if (req.cb) {
            // psdkd reports failure as {"id":N,"error":"<string>"} — a plain
            // string, not a JSON-RPC error object (spec §Request/Response).
            const QJsonValue errv = obj.value("error");
            if (!errv.isUndefined() && !errv.isNull()) {
                req.cb(false, QJsonObject{},
                       errv.isString() ? errv.toString()
                                       : QString::fromUtf8(QJsonDocument::fromVariant(
                                             errv.toVariant()).toJson(QJsonDocument::Compact)));
            } else {
                req.cb(true, obj.value("result").toObject(), QString());
            }
            continue;
        }

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
