#include "VideoClient.h"

#include <QTcpSocket>
#include <QAbstractSocket>
#include <QThread>
#include <QtEndian>

VideoClient::VideoClient(QObject *parent)
    : QObject(parent)
    , socket_(new QTcpSocket(this))
{
    connect(socket_, &QTcpSocket::connected,    this, &VideoClient::onConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &VideoClient::onDisconnected);
    connect(socket_, &QTcpSocket::readyRead,    this, &VideoClient::onReadyRead);
    connect(socket_, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &VideoClient::onError);
    fps_timer_.start();
}

void VideoClient::setHost(const QString &host, quint16 port)
{
    host_ = host;
    port_ = port;
}

void VideoClient::connectToHost()
{
    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->disconnectFromHost();
    }
    recv_buf_.clear();
    expected_size_ = 0;
    frame_count_   = 0;
    fps_timer_.restart();
    emit logMessage(QString("[Video] Connecting to %1:%2 ...").arg(host_).arg(port_));

    QString connectHost = host_;
    quint16 connectPort = port_;
    if (host_ != "127.0.0.1" && host_ != "localhost") {
        if (!ensureRelay()) {
            emit logMessage("[Video] Failed to start local relay");
            return;
        }
        connectHost = "127.0.0.1";
        connectPort = relay_listen_port_;
        emit logMessage(QString("[Video] Using local relay %1:%2 -> %3:%4")
                        .arg(connectHost)
                        .arg(connectPort)
                        .arg(host_)
                        .arg(port_));
    } else {
        stopRelay();
    }

    socket_->connectToHost(connectHost, connectPort);
}

void VideoClient::disconnectFromHost()
{
    socket_->disconnectFromHost();
    stopRelay();
}

void VideoClient::onConnected()
{
    emit logMessage(QString("[Video] Connected to %1:%2").arg(host_).arg(port_));
}

void VideoClient::onDisconnected()
{
    recv_buf_.clear();
    expected_size_ = 0;
    emit logMessage("[Video] Disconnected.");
}

void VideoClient::onError(QAbstractSocket::SocketError err)
{
    emit logMessage(QString("[Video] Socket error (%1): %2")
                    .arg(static_cast<int>(err))
                    .arg(socket_->errorString()));
}

void VideoClient::onReadyRead()
{
    recv_buf_.append(socket_->readAll());

    while (true) {
        if (expected_size_ == 0) {
            // Need 4-byte header
            if (recv_buf_.size() < 4) break;

            quint32 be;
            memcpy(&be, recv_buf_.constData(), 4);
            expected_size_ = qFromBigEndian(be);
            recv_buf_.remove(0, 4);

            if (expected_size_ == 0 || expected_size_ > 10 * 1024 * 1024) {
                // Sanity check: reject absurd sizes
                expected_size_ = 0;
                recv_buf_.clear();
                break;
            }
        }

        if ((quint32)recv_buf_.size() < expected_size_) {
            break; // Wait for more data
        }

        QByteArray jpeg = recv_buf_.left(static_cast<int>(expected_size_));
        recv_buf_.remove(0, static_cast<int>(expected_size_));
        expected_size_ = 0;

        QImage img;
        if (img.loadFromData(jpeg, "JPEG")) {
            emit frameReady(img);

            ++frame_count_;
            qint64 elapsed = fps_timer_.elapsed();
            if (elapsed >= 1000) {
                double fps = frame_count_ * 1000.0 / elapsed;
                emit fpsUpdated(fps);
                emit logMessage(QString("[Video] Receiving frames: %1 fps").arg(fps, 0, 'f', 1));
                frame_count_ = 0;
                fps_timer_.restart();
            }
        }
    }
}

bool VideoClient::ensureRelay()
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
            data = src.recv(65536)
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
        emit logMessage("[Video relay] python3 relay failed to start");
        stopRelay();
        return false;
    }

    QByteArray buffer;
    QByteArray portLine;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000) {
        if (proc->bytesAvailable() > 0 || proc->waitForReadyRead(100)) {
            buffer.append(proc->readAllStandardOutput());
            int newlineIndex = buffer.indexOf('\n');
            if (newlineIndex >= 0) {
                portLine = buffer.left(newlineIndex).trimmed();
                QByteArray remaining = buffer.mid(newlineIndex + 1).trimmed();
                if (!remaining.isEmpty()) {
                    emit logMessage("[Video relay] " + QString::fromUtf8(remaining));
                }
                break;
            }
        }
        QThread::msleep(20);
    }

    bool ok = false;
    int portValue = QString::fromUtf8(portLine).toInt(&ok);
    if (!ok || portValue <= 0 || portValue > 65535) {
        emit logMessage("[Video relay] failed to get listen port");
        stopRelay();
        return false;
    }

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        if (relay_process_ != proc) return;
        QByteArray text = proc->readAllStandardOutput().trimmed();
        if (!text.isEmpty()) {
            emit logMessage("[Video relay] " + QString::fromUtf8(text));
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

void VideoClient::stopRelay()
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
