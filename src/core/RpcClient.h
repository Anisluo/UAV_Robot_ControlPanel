#ifndef RPCCLIENT_H
#define RPCCLIENT_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QAbstractSocket>
#include <QProcess>
#include <functional>

class QTcpSocket;

class RpcClient : public QObject {
    Q_OBJECT
public:
    explicit RpcClient(QObject *parent = nullptr);

    void setHost(const QString &host, quint16 port);
    void connectToHost();
    void disconnectFromHost();
    bool isConnected() const;

    void call(const QString &method,
              const QJsonObject &params,
              std::function<void(QJsonObject)> cb = nullptr);

signals:
    void connected();
    void disconnected();
    void logMessage(const QString &msg);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError err);

private:
    bool ensureRelay();
    void stopRelay();

    // High-frequency polling methods whose TX/RX should NOT be logged. At
    // 40 Hz arm.get_angles + 40 Hz arm.get_pose + 3 Hz npu.get_detections
    // they were dumping ~170 log lines/sec into LogWidget, choking the GUI
    // thread on QTextEdit appends. Errors / connection events still emit
    // logMessage(), so a real failure is still visible.
    static bool isHighRatePollMethod(const QString &method);

    QTcpSocket *socket_;
    QProcess   *relay_process_{nullptr};
    QString     host_;
    QString     relay_target_host_;
    quint16     port_{7001};
    quint16     relay_target_port_{0};
    quint16     relay_listen_port_{0};
    int         next_id_{1};
    QByteArray  recv_buf_;
    QMap<int, std::function<void(QJsonObject)>> pending_;
    // Track which in-flight request IDs were high-rate polls so we can
    // also suppress their RX-side log when the response arrives.
    QSet<int>   quiet_ids_;
};

#endif // RPCCLIENT_H
