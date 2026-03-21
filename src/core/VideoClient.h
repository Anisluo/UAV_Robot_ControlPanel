#ifndef VIDEOCLIENT_H
#define VIDEOCLIENT_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QImage>
#include <QElapsedTimer>
#include <QAbstractSocket>

class QTcpSocket;

class VideoClient : public QObject {
    Q_OBJECT
public:
    explicit VideoClient(QObject *parent = nullptr);

    void setHost(const QString &host, quint16 port);
    void connectToHost();
    void disconnectFromHost();

signals:
    void frameReady(const QImage &img);
    void fpsUpdated(double fps);

private slots:
    void onReadyRead();
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError err);

private:
    QTcpSocket   *socket_;
    QString       host_;
    quint16       port_{7002};
    QByteArray    recv_buf_;
    quint32       expected_size_{0};

    int           frame_count_{0};
    QElapsedTimer fps_timer_;
};

#endif // VIDEOCLIENT_H
