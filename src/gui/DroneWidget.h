#ifndef DRONEWIDGET_H
#define DRONEWIDGET_H

#include <QGroupBox>
#include <QByteArray>
#include <QHash>
#include <QAbstractSocket>
#include <QJsonObject>
#include <functional>

#include "MeshMapWidget.h"

class QLineEdit;
class QComboBox;
class QSpinBox;
class QLabel;
class QPushButton;
class QFrame;
class QTimer;
class QUdpSocket;
class QTcpSocket;
class QTcpServer;
class QJsonObject;

class DroneWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit DroneWidget(QWidget *parent = nullptr);

public slots:
    void updateMeshNodes(const QList<MeshNode> &nodes);
    void setDefaultTargetHost(const QString &host);
    // Mesh-widget 仿真 button: when active, fill .102~.105 with mock
    // telemetry and freeze the live RPC refresh path so simulated values
    // aren't overwritten on the next periodic poll.
    void setSimulationTelemetry(bool active, const QList<MeshNode> &nodes);

signals:
    // Emitted whenever a node reports a WGS-84 GPS fix (real telemetry or 仿真).
    // MainWindow routes this to the dashboard MapWidget for marker + trajectory.
    void dronePositionUpdated(int octet, double lat, double lng);

private slots:
    void refreshDroneStates();
    void onUdpReadyRead();
    void onKmzLoadFile();
    void onKmzSend();
    void onKmzConnected();
    void onKmzBytesWritten(qint64 bytes);
    void onKmzDisconnected();
    void onKmzError(QAbstractSocket::SocketError err);

    // Remote control: a line-delimited JSON TCP server. Remote platforms
    // send {"id":N,"cmd":"deploy_kmz","name":"<file>","target":"192.168.200.10X","port":14550}
    // and HostGUI loads the named file from the configured KMZ library
    // directory, forwards it to the drone, and replies with the result.
    void onRemoteToggle(bool checked);
    void onRemoteNewConnection();
    void onRemoteClientReadyRead();
    void onRemoteClientDisconnected();
    void onKmzDirBrowse();

private slots:
    // 一键起飞 / KMZ 航线 (PSDK psdkd JSON-RPC over UDP :5555)
    void onTakeoff();
    void onTakeoffPoll();
    void onKmzUploadSpec();      // drone.kmz_begin → kmz_chunk → kmz_end
    void onMissionCommand();     // start / stop / pause / resume
    void onRpcTimeoutTick();

private:
    // ok=false carries `error` (psdkd returns a plain string, not a
    // JSON-RPC error object) or a local timeout message.
    using RpcCallback = std::function<void(bool ok, const QJsonObject &result,
                                           const QString &error)>;

    struct PendingRequest {
        int         octet{0};
        QString     method;
        QJsonObject params;          // kept so a retry resends byte-identically
        RpcCallback cb;              // null for the legacy telemetry path
        int         retries_left{0};
        int         timeout_ms{5000};
        qint64      sent_ms{0};
    };

    void buildUi();
    void createNodeCard(class QGridLayout *grid, int row, int octet);
    void setStatus(const QString &text, const QString &color = "#888aaa");
    void setNodeActive(int octet, bool active);
    void updateNodeTimestamp(int octet, const QString &text);
    QString nodeIp(int octet) const;
    void sendRpcRequest(int octet, const QString &method);

    // Spec §Reliability: 5s timeout, 3 retries, match on `id`, discard
    // unmatched datagrams. Retries reuse the SAME id — psdkd documents
    // idempotent handling for critical methods, and reusing the id is what
    // lets a duplicated reply still be matched instead of orphaned.
    void callRpc(int octet, const QString &method, const QJsonObject &params,
                 RpcCallback cb, int timeout_ms = 5000, int retries = 3);
    void transmit(int request_id, const PendingRequest &req);
    int  kmzTargetOctet() const;

    // KMZ upload state machine (spec §KMZ Waypoint Mission Upload)
    void kmzSendNextChunk();
    void kmzFinish(bool ok, const QString &message);
    void setKmzStatus(const QString &text, const QString &color);

    // Remote-control helpers
    void handleRemoteCommand(QTcpSocket *client, const QJsonObject &req);
    void sendRemoteReply(QTcpSocket *client, const QJsonObject &reply);
    QString resolveKmzPath(const QString &name) const;
    bool   beginRemoteDeploy(const QString &full_path, const QString &target_ip,
                              quint16 target_port, QString *err_out);

    QLineEdit   *target_host_edit_;
    QLabel      *status_label_;
    QLabel      *refresh_label_;
    QPushButton *btn_refresh_;

    bool         sim_active_{false};

    QTimer      *refresh_timer_;
    QUdpSocket  *udp_socket_;
    int          next_request_id_{1};
    QHash<int, PendingRequest> pending_requests_;
    QHash<int, bool> active_nodes_;
    QHash<int, QFrame*> node_cards_;
    QHash<int, QLabel*> state_labels_;
    QHash<int, QLabel*> host_labels_;
    QHash<int, QLabel*> battery_labels_;
    QHash<int, QLabel*> altitude_labels_;
    QHash<int, QLabel*> heading_labels_;
    QHash<int, QLabel*> gps_labels_;
    QHash<int, QLabel*> updated_labels_;

    // KMZ 路径规划下发
    QLineEdit   *kmz_path_edit_;
    QComboBox   *kmz_ip_combo_;   // 192.168.200.102..106 (drone mesh nodes)
    QSpinBox    *kmz_port_spin_;
    QPushButton *btn_kmz_load_;
    QPushButton *btn_kmz_send_;
    QLabel      *kmz_status_label_;
    QTcpSocket  *kmz_socket_;
    QByteArray   kmz_data_;
    qint64       kmz_bytes_written_{0};

    // Remote-control panel: a line-delimited JSON TCP server (default 7100).
    // Persists port + KMZ library dir via QSettings under DroneWidget/...
    QLineEdit   *kmz_dir_edit_         = nullptr;
    QPushButton *btn_kmz_dir_browse_   = nullptr;
    QSpinBox    *remote_port_spin_     = nullptr;
    QPushButton *btn_remote_toggle_    = nullptr;
    QLabel      *remote_status_label_  = nullptr;
    QTcpServer  *remote_server_        = nullptr;

    // When a remote-triggered deployment is in flight, remember which
    // client connection + request id is waiting for the result so
    // onKmzDisconnected can route the final reply. nullptr → idle.
    QTcpSocket  *pending_remote_client_ = nullptr;
    int          pending_remote_req_id_ = 0;
    QString      pending_remote_name_;     // for echoing back

    // ── 一键起飞 ────────────────────────────────────────────────────────
    QPushButton *btn_takeoff_        = nullptr;
    QLabel      *takeoff_status_     = nullptr;
    QTimer      *takeoff_poll_timer_ = nullptr;
    int          takeoff_polls_left_ = 0;
    int          takeoff_octet_      = 0;

    // ── KMZ 航线下发 (协议规范版, UDP) ─────────────────────────────────
    QPushButton  *btn_kmz_upload_spec_ = nullptr;
    QPushButton  *btn_mission_start_   = nullptr;
    QPushButton  *btn_mission_stop_    = nullptr;
    QPushButton  *btn_mission_pause_   = nullptr;
    QPushButton  *btn_mission_resume_  = nullptr;
    class QProgressBar *kmz_progress_  = nullptr;

    QByteArray   kmz_spec_bytes_;      // whole file, held for the chunk loop
    QString      kmz_spec_filename_;
    int          kmz_spec_octet_    = 0;
    int          kmz_chunk_raw_max_ = 2048;  // server-declared in kmz_begin
    int          kmz_next_seq_      = 0;
    int          kmz_sent_bytes_    = 0;
    bool         kmz_spec_busy_     = false;

    QTimer      *rpc_timeout_timer_ = nullptr;
    // Timeout bookkeeping needs a monotonic clock; QDateTime is wall-clock
    // and this board's clock has jumped hours in a single session.
    class QElapsedTimer *rpc_clock_ = nullptr;
};

#endif // DRONEWIDGET_H
