#include "VideoClient.h"
#include "PlatformUtils.h"

#include <QTcpSocket>
#include <QAbstractSocket>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QThread>
#include <QtEndian>

#include <memory>

class VideoWorker : public QObject {
public:
    explicit VideoWorker(VideoClient *owner)
        : QObject(nullptr), owner_(owner) {}

    void startStream(const QString &host, quint16 port)
    {
        stopStream();

        recv_buf_.clear();
        expected_size_ = 0;
        frame_count_ = 0;
        fps_timer_.restart();

        postLog(QString("[Video] Connecting to %1:%2 ...").arg(host).arg(port));

        socket_ = new QTcpSocket(this);
        connect(socket_, &QTcpSocket::connected, this, [this, host, port]() {
            postLog(QString("[Video] Connected to %1:%2").arg(host).arg(port));
        });
        connect(socket_, &QTcpSocket::disconnected, this, [this]() {
            recv_buf_.clear();
            expected_size_ = 0;
            postLog("[Video] Disconnected.");
        });
        connect(socket_,
                QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
                this,
                [this](QAbstractSocket::SocketError err) {
                    if (socket_ == nullptr) return;
                    postLog(QString("[Video] Socket error (%1): %2")
                                .arg(static_cast<int>(err))
                                .arg(socket_->errorString()));
                });
        connect(socket_, &QTcpSocket::readyRead, this, [this]() {
            onReadyRead();
        });

        socket_->connectToHost(host, port);
    }

    void stopStream()
    {
        if (socket_ == nullptr) {
            recv_buf_.clear();
            expected_size_ = 0;
            return;
        }

        disconnect(socket_, nullptr, this, nullptr);
        socket_->abort();
        socket_->deleteLater();
        socket_ = nullptr;
        recv_buf_.clear();
        expected_size_ = 0;
    }

private:
    void onReadyRead()
    {
        if (socket_ == nullptr) return;

        recv_buf_.append(socket_->readAll());

        while (true) {
            if (expected_size_ == 0) {
                if (recv_buf_.size() < 4) break;

                quint32 be = 0;
                memcpy(&be, recv_buf_.constData(), 4);
                expected_size_ = qFromBigEndian(be);
                recv_buf_.remove(0, 4);

                if (expected_size_ == 0 || expected_size_ > 10 * 1024 * 1024) {
                    expected_size_ = 0;
                    recv_buf_.clear();
                    postLog("[Video] Invalid frame header, dropping buffer.");
                    break;
                }
            }

            if (static_cast<quint32>(recv_buf_.size()) < expected_size_) {
                break;
            }

            QByteArray jpeg = recv_buf_.left(static_cast<int>(expected_size_));
            recv_buf_.remove(0, static_cast<int>(expected_size_));
            expected_size_ = 0;

            QImage img;
            if (!img.loadFromData(jpeg, "JPEG")) {
                postLog("[Video] JPEG decode failed.");
                continue;
            }

            const QImage ready = img;
            QMetaObject::invokeMethod(owner_,
                                      [owner = owner_, ready]() {
                                          emit owner->frameReady(ready);
                                      },
                                      Qt::QueuedConnection);

            ++frame_count_;
            const qint64 elapsed = fps_timer_.elapsed();
            if (elapsed >= 1000) {
                const double fps = frame_count_ * 1000.0 / elapsed;
                QMetaObject::invokeMethod(owner_,
                                          [owner = owner_, fps]() {
                                              emit owner->fpsUpdated(fps);
                                              emit owner->logMessage(
                                                  QString("[Video] Receiving frames: %1 fps")
                                                      .arg(fps, 0, 'f', 1));
                                          },
                                          Qt::QueuedConnection);
                frame_count_ = 0;
                fps_timer_.restart();
            }
        }
    }

    void postLog(const QString &msg)
    {
        QMetaObject::invokeMethod(owner_,
                                  [owner = owner_, msg]() {
                                      emit owner->logMessage(msg);
                                  },
                                  Qt::QueuedConnection);
    }

    VideoClient *owner_{nullptr};
    QTcpSocket  *socket_{nullptr};
    QByteArray   recv_buf_;
    quint32      expected_size_{0};
    int          frame_count_{0};
    QElapsedTimer fps_timer_;
};

VideoClient::VideoClient(QObject *parent)
    : QObject(parent)
{
    worker_thread_ = new QThread(this);
    worker_ = new VideoWorker(this);
    worker_->moveToThread(worker_thread_);
    connect(worker_thread_, &QThread::finished, worker_, &QObject::deleteLater);
    worker_thread_->start();
}

VideoClient::~VideoClient()
{
    disconnectFromHost();
    if (worker_thread_ != nullptr) {
        worker_thread_->quit();
        worker_thread_->wait(2000);
    }
}

void VideoClient::setHost(const QString &host, quint16 port)
{
    host_ = host;
    port_ = port;
}

void VideoClient::connectToHost()
{
    stopRelay();
    if (worker_ == nullptr) return;

    const QString host = host_;
    const quint16 port = port_;
    QMetaObject::invokeMethod(worker_,
                              [this, host, port]() {
                                  if (worker_ != nullptr) {
                                      worker_->startStream(host, port);
                                  }
                              },
                              Qt::QueuedConnection);
}

void VideoClient::disconnectFromHost()
{
    if (worker_ != nullptr) {
        QMetaObject::invokeMethod(worker_,
                                  [this]() {
                                      if (worker_ != nullptr) {
                                          worker_->stopStream();
                                      }
                                  },
                                  Qt::QueuedConnection);
    }
    stopRelay();
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

    const auto launchSpec = PlatformUtils::pythonCommand({
        QStringLiteral("-u"),
        QStringLiteral("-c"),
        QString::fromUtf8(kRelayScript),
        host_,
        QString::number(port_)
    });
    proc->start(launchSpec.program, launchSpec.arguments);
    if (!proc->waitForStarted(3000)) {
        emit logMessage("[Video relay] Python relay failed to start");
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
