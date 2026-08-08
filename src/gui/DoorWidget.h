#ifndef DOORWIDGET_H
#define DOORWIDGET_H

#include <QGroupBox>
#include <QJsonObject>
#include <QString>

class RpcClient;
class QLabel;
class QPushButton;
class QTimer;

// 机场舱门 + 停机坪升降 — driven by proc_door over RS485 / Modbus RTU.
//
// Wiring behind the RPC (see proc_door/README.md):
//   Y1+Y2 停机坪升降   Y3 舱门使能   Y4 舱门方向
//   X1 坪-上限位  X2 坪-下限位  X3 舱门-开到位  X4 舱门-关到位
//
// Motion is asynchronous on the backend: a command returns as soon as the
// coils are energised, and proc_door's own supervisor cuts power when the
// limit switch trips or the timeout expires. So this widget is a pure
// command + poll surface — it never blocks, and 停止 always gets through
// even mid-travel.
class DoorWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit DoorWidget(RpcClient *rpc, QWidget *parent = nullptr);

public slots:
    void onRpcConnected();
    void onRpcDisconnected();

private slots:
    void poll();
    void onReconnect();   // 继电器板断电重上电后，强制重开 RS485

private:
    void buildUi();
    void sendCommand(const QString &method);
    void applyStatus(const QJsonObject &result);
    void setOffline();

    // One sensor / relay indicator dot + caption.
    struct Led {
        QLabel *dot = nullptr;
        QLabel *text = nullptr;
    };
    Led makeLed(const QString &caption, QWidget *parent);
    static void setLed(const Led &led, bool on, const QString &onColor);

    // Paint the link indicator: dot colour + caption in one place, so the
    // LED and the text can never disagree.
    void setLinkState(const QString &text, const QString &color);

    RpcClient *rpc_{nullptr};
    QTimer    *poll_timer_{nullptr};

    // 舱门
    QPushButton *hatch_open_btn_{nullptr};
    QPushButton *hatch_close_btn_{nullptr};
    QPushButton *hatch_stop_btn_{nullptr};
    QLabel      *hatch_state_{nullptr};

    // 停机坪
    QPushButton *pad_up_btn_{nullptr};
    QPushButton *pad_down_btn_{nullptr};
    QPushButton *pad_stop_btn_{nullptr};
    QLabel      *pad_state_{nullptr};

    QPushButton *stop_all_btn_{nullptr};
    QPushButton *reconnect_btn_{nullptr};
    QLabel      *link_dot_{nullptr};     // 串口/模块连接指示灯
    QLabel      *link_state_{nullptr};   // 同一状态的文字说明

    Led in_leds_[4];    // X1..X4
    Led out_leds_[4];   // Y1..Y4
};

#endif // DOORWIDGET_H
