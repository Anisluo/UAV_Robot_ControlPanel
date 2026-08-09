#ifndef AIRPORTWIDGET_H
#define AIRPORTWIDGET_H

#include <QGroupBox>
#include <QList>

class RpcClient;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
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
    void onHomeRail2();          // 导轨2 独立归零
    void onRail2GoTo();          // 导轨2 走到绝对位置 (距零点 N mm)
    void onRail2Accel();         // 导轨2 加速度档位 (0..255, 下次运动生效)
    void pollHomeStatus();       // 归零期间轮询 airport.get_status
    void pollRail2Move();        // 绝对位置运动期间轮询, 到位后恢复按钮
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
    QPushButton *home_rail2_btn_{nullptr};
    QLabel      *home_rail2_state_{nullptr};
    QLabel      *home_state_{nullptr};
    QTimer      *home_timer_{nullptr};
    // 导轨2 绝对位置 — 距归零零点 N mm。只有归零过才有意义, 网关会拒掉
    // 未归零的调用 (驱动器多圈计数掉电不保持)。
    QDoubleSpinBox *rail2_pos_spin_{nullptr};
    QPushButton    *rail2_goto_btn_{nullptr};
    QLabel         *rail2_goto_state_{nullptr};
    QTimer         *rail2_move_timer_{nullptr};

    // Homing progress tracked as state, not by reading the status label's
    // text back out. The label is shared with other messages, so "is the
    // rail homing?" must not depend on nobody else having written to it.
    bool  home_pair_pending_{false};
    bool  home_rail2_pending_{false};
    qint64 home_started_ms_{0};
    // 加速度档位 (ZDT 0xF6 第4字节): 0=直接启动, 1..255 越大越快。
    // 默认 10 实测慢到命令 900rpm 只跑出 127rpm — 短行程全程都在加速段。
    QSpinBox       *rail2_accel_spin_{nullptr};
    QPushButton    *rail2_accel_btn_{nullptr};
    QLabel         *rail2_accel_state_{nullptr};
    QPushButton *gripper_open_btn_{nullptr};   // relay → 开 (jaw open)
    QPushButton *gripper_close_btn_{nullptr};  // relay → 关 (jaw close)
};

#endif // AIRPORTWIDGET_H
