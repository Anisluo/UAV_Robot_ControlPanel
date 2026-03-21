#include "RpcClient.h"

#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>

RpcClient::RpcClient(QObject *parent)
    : QObject(parent)
    , socket_(new QTcpSocket(this))
{
    connect(socket_, &QTcpSocket::connected,    this, &RpcClient::onConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &RpcClient::onDisconnected);
    connect(socket_, &QTcpSocket::readyRead,    this, &RpcClient::onReadyRead);
    connect(socket_, &QAbstractSocket::errorOccurred, this, &RpcClient::onError);
}

void RpcClient::setHost(const QString &host, quint16 port)
{
    host_ = host;
    port_ = port;
}

void RpcClient::connectToHost()
{
    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->disconnectFromHost();
    }
    recv_buf_.clear();
    pending_.clear();
    emit logMessage(QString("[RPC] Connecting to %1:%2 ...").arg(host_).arg(port_));
    socket_->connectToHost(host_, port_);
}

void RpcClient::disconnectFromHost()
{
    socket_->disconnectFromHost();
}

bool RpcClient::isConnected() const
{
    return socket_->state() == QAbstractSocket::ConnectedState;
}

void RpcClient::call(const QString &method,
                     const QJsonObject &params,
                     std::function<void(QJsonObject)> cb)
{
    if (!isConnected()) {
        emit logMessage("[RPC] Not connected – dropping call: " + method);
        return;
    }

    int id = next_id_++;
    QJsonObject req;
    req["id"]     = id;
    req["method"] = method;
    req["params"] = params;

    if (cb) {
        pending_.insert(id, cb);
    }

    QByteArray data = QJsonDocument(req).toJson(QJsonDocument::Compact);
    data.append('\n');
    socket_->write(data);
}

void RpcClient::onConnected()
{
    emit logMessage("[RPC] Connected to " + host_ + ":" + QString::number(port_));
    emit connected();
}

void RpcClient::onDisconnected()
{
    emit logMessage("[RPC] Disconnected from host.");
    pending_.clear();
    emit disconnected();
}

void RpcClient::onReadyRead()
{
    recv_buf_.append(socket_->readAll());

    while (true) {
        int idx = recv_buf_.indexOf('\n');
        if (idx < 0) break;

        QByteArray line = recv_buf_.left(idx).trimmed();
        recv_buf_.remove(0, idx + 1);

        if (line.isEmpty()) continue;

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            emit logMessage("[RPC] Parse error: " + err.errorString());
            continue;
        }

        QJsonObject resp = doc.object();
        int id = resp.value("id").toInt(-1);

        if (resp.contains("error")) {
            QString errMsg = resp.value("error").toString();
            emit logMessage(QString("[RPC] Error (id=%1): %2").arg(id).arg(errMsg));
        }

        if (id >= 0 && pending_.contains(id)) {
            auto cb = pending_.take(id);
            QJsonObject result = resp.value("result").toObject();
            cb(result);
        }
    }
}

void RpcClient::onError(QAbstractSocket::SocketError err)
{
    Q_UNUSED(err)
    emit logMessage("[RPC] Socket error: " + socket_->errorString());
    // Connection refused / timeout: socket never reached Connected state,
    // so disconnected() won't fire automatically – emit it here so the UI resets.
    if (socket_->state() != QAbstractSocket::ConnectedState) {
        pending_.clear();
        emit disconnected();
    }
}
