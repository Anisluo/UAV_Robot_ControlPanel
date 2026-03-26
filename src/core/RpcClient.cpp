#include "RpcClient.h"

#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QThread>

RpcClient::RpcClient(QObject *parent)
    : QObject(parent)
    , socket_(new QTcpSocket(this))
{
    connect(socket_, &QTcpSocket::connected,    this, &RpcClient::onConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &RpcClient::onDisconnected);
    connect(socket_, &QTcpSocket::readyRead,    this, &RpcClient::onReadyRead);
    connect(socket_, &QAbstractSocket::errorOccurred, this, &RpcClient::onError);
    connect(socket_, &QAbstractSocket::stateChanged, this,
            [this](QAbstractSocket::SocketState state) {
                emit logMessage(QString("[RPC] State changed: %1").arg(static_cast<int>(state)));
            });
    connect(socket_, &QTcpSocket::bytesWritten, this,
            [this](qint64 bytes) {
                emit logMessage(QString("[RPC] bytesWritten=%1").arg(bytes));
            });
}

void RpcClient::setHost(const QString &host, quint16 port)
{
    host_ = host;
    port_ = port;
}

void RpcClient::connectToHost()
{
    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->abort();
    }
    recv_buf_.clear();
    pending_.clear();
    emit logMessage(QString("[RPC] Connecting to %1:%2 ...").arg(host_).arg(port_));

    QString connectHost = host_;
    quint16 connectPort = port_;
    if (host_ != "127.0.0.1" && host_ != "localhost") {
        if (!ensureRelay()) {
            emit logMessage("[RPC] Failed to start local relay");
            emit disconnected();
            return;
        }
        connectHost = "127.0.0.1";
        connectPort = relay_listen_port_;
        emit logMessage(QString("[RPC] Using local relay %1:%2 -> %3:%4")
                        .arg(connectHost)
                        .arg(connectPort)
                        .arg(host_)
                        .arg(port_));
    } else {
        stopRelay();
    }

    socket_->connectToHost(connectHost, connectPort);
}

void RpcClient::disconnectFromHost()
{
    if (socket_->state() == QAbstractSocket::UnconnectedState) {
        stopRelay();
        return;
    }
    socket_->disconnectFromHost();
    stopRelay();
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
    emit logMessage("[RPC] TX " + QString::fromUtf8(data.trimmed()));
    socket_->write(data);
    socket_->flush();
}

void RpcClient::onConnected()
{
    emit logMessage("[RPC] Connected to " + host_ + ":" + QString::number(port_)
                    + QString(" (state=%1)").arg(static_cast<int>(socket_->state())));
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

        emit logMessage("[RPC] RX " + QString::fromUtf8(line));

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
    emit logMessage(QString("[RPC] Socket error (%1): %2")
                    .arg(static_cast<int>(err))
                    .arg(socket_->errorString()));
    // Connection refused / timeout: socket never reached Connected state,
    // so disconnected() won't fire automatically – emit it here so the UI resets.
    if (socket_->state() != QAbstractSocket::ConnectedState) {
        pending_.clear();
        emit disconnected();
    }
}

bool RpcClient::ensureRelay()
{
    if (relay_process_ != nullptr
        && relay_process_->state() != QProcess::NotRunning
        && relay_target_host_ == host_
        && relay_target_port_ == port_) {
        return true;
    }

    stopRelay();

    QProcess *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    relay_process_ = proc;

    static const char kRelayScript[] = R"PY(
import socket, threading, sys
LISTEN_HOST = '127.0.0.1'
TARGET_HOST = sys.argv[1]
TARGET_PORT = int(sys.argv[2])
ls = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
ls.bind((LISTEN_HOST, 0))
LISTEN_PORT = ls.getsockname()[1]
print(LISTEN_PORT, flush=True)
ls.listen(5)

def pump(src, dst):
    try:
        while True:
            data = src.recv(4096)
            if not data:
                try:
                    dst.shutdown(socket.SHUT_WR)
                except Exception:
                    pass
                break
            dst.sendall(data)
    except Exception:
        try:
            dst.shutdown(socket.SHUT_WR)
        except Exception:
            pass

while True:
    client, _addr = ls.accept()
    server = socket.create_connection((TARGET_HOST, TARGET_PORT), timeout=5)
    client.settimeout(None)
    server.settimeout(None)
    threading.Thread(target=pump, args=(client, server), daemon=True).start()
    threading.Thread(target=pump, args=(server, client), daemon=True).start()
)PY";

    QStringList args;
    args << "-u" << "-c" << kRelayScript << host_ << QString::number(port_);
    proc->start("python3", args);
    if (!proc->waitForStarted(3000)) {
        emit logMessage("[RPC relay] python3 relay failed to start");
        stopRelay();
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    QByteArray buffer;
    QByteArray portLine;
    while (timer.elapsed() < 3000) {
        if (proc->bytesAvailable() > 0 || proc->waitForReadyRead(100)) {
            buffer.append(proc->readAllStandardOutput());
            int newlineIndex = buffer.indexOf('\n');
            if (newlineIndex >= 0) {
                portLine = buffer.left(newlineIndex).trimmed();
                QByteArray remaining = buffer.mid(newlineIndex + 1).trimmed();
                if (!remaining.isEmpty()) {
                    emit logMessage("[RPC relay] " + QString::fromUtf8(remaining));
                }
                break;
            }
        }
        QThread::msleep(20);
    }
    bool ok = false;
    int portValue = QString::fromUtf8(portLine).toInt(&ok);
    if (!ok || portValue <= 0 || portValue > 65535) {
        emit logMessage("[RPC relay] failed to get listen port");
        stopRelay();
        return false;
    }

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        if (relay_process_ != proc) {
            return;
        }
        QByteArray text = proc->readAllStandardOutput().trimmed();
        if (!text.isEmpty()) {
            emit logMessage("[RPC relay] " + QString::fromUtf8(text));
        }
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int, QProcess::ExitStatus) {
                if (relay_process_ == proc) {
                    relay_process_ = nullptr;
                    relay_target_host_.clear();
                    relay_target_port_ = 0;
                    relay_listen_port_ = 0;
                }
            });

    relay_target_host_ = host_;
    relay_target_port_ = port_;
    relay_listen_port_ = static_cast<quint16>(portValue);
    QThread::msleep(50);
    return true;
}

void RpcClient::stopRelay()
{
    if (relay_process_ == nullptr) {
        return;
    }
    relay_process_->kill();
    relay_process_->waitForFinished(1000);
    relay_process_->deleteLater();
    relay_process_ = nullptr;
    relay_target_host_.clear();
    relay_target_port_ = 0;
    relay_listen_port_ = 0;
}
