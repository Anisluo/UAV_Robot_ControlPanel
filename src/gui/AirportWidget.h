#ifndef AIRPORTWIDGET_H
#define AIRPORTWIDGET_H

#include <QGroupBox>
#include <QList>

class RpcClient;
class QSlider;
class QSpinBox;
class QPushButton;
class QLabel;
class QTimer;

class AirportWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit AirportWidget(RpcClient *rpc, QWidget *parent = nullptr);

private slots:
    void onLockSliderChanged(int value);
    void onLockSpinChanged(int value);
    void onRail2SliderChanged(int value);
    void onRail2SpinChanged(int value);
    void onLock();
    void onRelease();
    void onRail2Move(bool forward);
    void onStopAll();
    void onHomeRails();          // 归零 — 跑到释放端硬限位并latch零点
    void pollHomeStatus();       // 归零期间轮询 airport.get_status
    void onGripper(bool open);   // relay-driven airport jaw (proc_gateway/airport.gripper)

private:
    void buildUi();
    void syncSliderAndSpin(QSlider *slider, QSpinBox *spinbox, int value);

    RpcClient   *rpc_;
    QSlider     *lock_slider_{nullptr};
    QSpinBox    *lock_spin_{nullptr};
    QSlider     *rail2_slider_{nullptr};
    QSpinBox    *rail2_spin_{nullptr};
    QPushButton *lock_btn_{nullptr};
    QPushButton *release_btn_{nullptr};
    QPushButton *rail2_fwd_btn_{nullptr};
    QPushButton *rail2_back_btn_{nullptr};
    QPushButton *stop_all_btn_{nullptr};
    QPushButton *home_btn_{nullptr};
    QLabel      *home_state_{nullptr};
    QTimer      *home_timer_{nullptr};
    QPushButton *gripper_open_btn_{nullptr};   // relay → 开 (jaw open)
    QPushButton *gripper_close_btn_{nullptr};  // relay → 关 (jaw close)
};

#endif // AIRPORTWIDGET_H
