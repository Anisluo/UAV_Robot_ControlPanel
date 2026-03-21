#ifndef DRONEWIDGET_H
#define DRONEWIDGET_H

#include <QGroupBox>
#include <QByteArray>
#include <QAbstractSocket>
#include <QTcpSocket>

class QLineEdit;
class QSpinBox;
class QPushButton;
class QLabel;
class QTcpSocket;

class DroneWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit DroneWidget(QWidget *parent = nullptr);

private slots:
    void onLoadFile();
    void onSend();
    void onConnected();
    void onError(QAbstractSocket::SocketError err);
    void onBytesWritten(qint64 bytes);
    void onDisconnected();

private:
    void buildUi();
    void setStatus(const QString &text, const QString &color = "#888aaa");

    QLineEdit   *file_path_edit_;
    QLineEdit   *ip_edit_;
    QSpinBox    *port_spin_;
    QPushButton *btn_load_;
    QPushButton *btn_send_;
    QLabel      *status_label_;

    QTcpSocket  *socket_;
    QByteArray   file_data_;
    qint64       bytes_written_{0};
};

#endif // DRONEWIDGET_H
