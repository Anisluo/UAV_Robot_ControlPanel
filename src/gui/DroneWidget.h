#ifndef DRONEWIDGET_H
#define DRONEWIDGET_H

#include <QGroupBox>
#include <QByteArray>
#include <QHash>
#include <QAbstractSocket>

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
    // send {"id":N,"cmd":"deploy_kmz","name":"<file>","target":"192.168.1.10X","port":14550}
    // and HostGUI loads the named file from the configured KMZ library
    // directory, forwards it to the drone, and replies with the result.
    void onRemoteToggle(bool checked);
    void onRemoteNewConnection();
    void onRemoteClientReadyRead();
    void onRemoteClientDisconnected();
    void onKmzDirBrowse();

private:
    struct PendingRequest {
        int     octet{0};
        QString method;
    };

    void buildUi();
    void createNodeCard(class QGridLayout *grid, int row, int octet);
    void setStatus(const QString &text, const QString &color = "#888aaa");
    void setNodeActive(int octet, bool active);
    void updateNodeTimestamp(int octet, const QString &text);
    QString nodeIp(int octet) const;
    void sendRpcRequest(int octet, const QString &method);

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
    QComboBox   *kmz_ip_combo_;   // 192.168.1.102..106 (drone mesh nodes)
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
};

#endif // DRONEWIDGET_H
