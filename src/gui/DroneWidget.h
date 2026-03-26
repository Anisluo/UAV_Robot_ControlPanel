#ifndef DRONEWIDGET_H
#define DRONEWIDGET_H

#include <QGroupBox>
#include <QByteArray>
#include <QHash>
#include <QAbstractSocket>

#include "MeshMapWidget.h"

class QLineEdit;
class QSpinBox;
class QLabel;
class QPushButton;
class QFrame;
class QTimer;
class QUdpSocket;
class QTcpSocket;

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
    QLineEdit   *kmz_ip_edit_;
    QSpinBox    *kmz_port_spin_;
    QPushButton *btn_kmz_load_;
    QPushButton *btn_kmz_send_;
    QLabel      *kmz_status_label_;
    QTcpSocket  *kmz_socket_;
    QByteArray   kmz_data_;
    qint64       kmz_bytes_written_{0};
};

#endif // DRONEWIDGET_H
