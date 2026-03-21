#ifndef RPCCLIENT_H
#define RPCCLIENT_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QAbstractSocket>
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
    QTcpSocket *socket_;
    QString     host_;
    quint16     port_{7001};
    int         next_id_{1};
    QByteArray  recv_buf_;
    QMap<int, std::function<void(QJsonObject)>> pending_;
};

#endif // RPCCLIENT_H
