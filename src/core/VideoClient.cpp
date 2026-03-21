#include "VideoClient.h"

#include <QTcpSocket>
#include <QAbstractSocket>
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
    socket_->connectToHost(host_, port_);
}

void VideoClient::disconnectFromHost()
{
    socket_->disconnectFromHost();
}

void VideoClient::onConnected()
{
    // nothing extra needed
}

void VideoClient::onDisconnected()
{
    recv_buf_.clear();
    expected_size_ = 0;
}

void VideoClient::onError(QAbstractSocket::SocketError err)
{
    Q_UNUSED(err)
    // Silently tolerate – UI will show NO SIGNAL from lack of frames
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
                frame_count_ = 0;
                fps_timer_.restart();
            }
        }
    }
}
