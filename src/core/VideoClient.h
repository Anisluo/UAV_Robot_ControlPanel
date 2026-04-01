#ifndef VIDEOCLIENT_H
#define VIDEOCLIENT_H

#include <QObject>
#include <QString>
#include <QImage>
#include <QProcess>
#include <QThread>

class QTcpSocket;
class VideoWorker;

class VideoClient : public QObject {
    Q_OBJECT
public:
    explicit VideoClient(QObject *parent = nullptr);
    ~VideoClient() override;

    void setHost(const QString &host, quint16 port);
    void connectToHost();
    void disconnectFromHost();

signals:
    void frameReady(const QImage &img);
    void fpsUpdated(double fps);
    void logMessage(const QString &msg);

private:
    friend class VideoWorker;

    bool ensureRelay();
    void stopRelay();

    QProcess     *relay_process_{nullptr};
    QThread      *worker_thread_{nullptr};
    VideoWorker  *worker_{nullptr};
    QString       host_;
    QString       relay_target_host_;
    quint16       port_{7002};
    quint16       relay_target_port_{0};
    quint16       relay_listen_port_{0};
};

#endif // VIDEOCLIENT_H
