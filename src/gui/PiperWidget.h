#ifndef PIPERWIDGET_H
#define PIPERWIDGET_H

#include <QWidget>
#include <QVector>
#include <QString>
#include <QTimer>
#include <array>

class RpcClient;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QTabWidget;
class QGroupBox;
class QSettings;
class ArmViewer3D;
class ArmSyncWorker;
class QThread;

// PiperWidget — gen-2 arm control panel for the AgileX Piper 6-DOF arm.
//
// Renders in the MainWindow "主控面板" dashboard in place of ArmWidget, when
// system.get_backend returns "piper". Layout intentionally echoes the
// vendor's ArmRobotUA.exe (top bar with enable/e-stop/speed, mode tabs in
// the middle, 3D viewer on the right, real-time readout overlay, status
// bar at the bottom) so users familiar with that tool feel at home.
//
// All commands flow through the gateway via RpcClient -> proc_piper. We do
// not talk to the SDK directly here.
class PiperWidget : public QWidget {
    Q_OBJECT
public:
    explicit PiperWidget(RpcClient *rpc, QWidget *parent = nullptr);
    ~PiperWidget() override;

    // Persistence (called by MainWindow on close / start)
    void loadConfig(QSettings &s);
    void saveConfig(QSettings &s) const;

public slots:
    // Called by MainWindow after the RPC connection is up. Triggers the
    // initial state pull + starts the periodic poll timer.
    void onRpcConnected();
    void onRpcDisconnected();

private slots:
    // ── periodic polls ────────────────────────────────────────────────
    void pollJointsAndPose();   // 20Hz: arm.get_angles + arm.get_pose
    void pollStatus();          // 2Hz : piper.get_status

    // ── top bar buttons ───────────────────────────────────────────────
    void onEnableClicked();     // piper.handshake (re-do CAN_CTRL handshake)
    void onEmergencyStopClicked();
    void onHomeClicked();       // arm.home (MoveJ to all-zero)
    void onSpeedChanged(int pct);

    // ── joint tab ─────────────────────────────────────────────────────
    void onJointSliderReleased(int joint_idx);
    void onJointSpinChanged(int joint_idx, double deg);
    void onSendJointsClicked();
    void onJointZeroClicked();

    // ── cartesian tab ─────────────────────────────────────────────────
    void onJogClicked(int axis_idx, int direction);   // axis 0..5 = X/Y/Z/RX/RY/RZ ; dir = ±1
    void onSendCartesianClicked();
    void onCartesianModeChanged(int idx);

    // ── gripper tab ───────────────────────────────────────────────────
    void onGripperSliderReleased();
    void onGripperOpenClicked();
    void onGripperCloseClicked();

private:
    // ── construction helpers ──────────────────────────────────────────
    void buildLayout();
    void buildTopBar();
    void buildMainTabs();
    void buildJointTab();
    void buildCartesianTab();
    void buildGripperTab();
    void buildRightPanel();
    void buildStatusBar();
    void connectSignals();

    // ── data → UI updaters ────────────────────────────────────────────
    void updateJointReadouts(const std::array<double, 6> &deg);
    void updatePoseReadouts (const std::array<double, 6> &xyzrpy);
    void updateStatusBar(int ctrl_mode, int arm_status, int mode_feed,
                        int motion_status, int teach_status, bool heartbeat_alive);

    // ── helpers ───────────────────────────────────────────────────────
    QString ctrlModeText(int m)    const;
    QString armStatusText(int s)   const;
    QString motionStatusText(int s) const;

    // ── state ────────────────────────────────────────────────────────
    RpcClient *rpc_;

    // Live readback (most recent)
    std::array<double, 6> live_joints_deg_{};
    std::array<double, 6> live_pose_{};    // x,y,z mm + rx,ry,rz deg
    int  ctrl_mode_     = 0;
    bool heartbeat_alive_ = false;

    // ── widgets ──────────────────────────────────────────────────────
    // top bar
    QLabel       *conn_led_      = nullptr;
    QSlider      *speed_slider_  = nullptr;
    QSpinBox     *speed_spin_    = nullptr;
    QPushButton  *enable_btn_    = nullptr;
    QPushButton  *estop_btn_     = nullptr;
    QPushButton  *home_btn_      = nullptr;

    // main tabs
    QTabWidget   *tabs_          = nullptr;

    // joint tab
    std::array<QSlider*, 6>        joint_sliders_  {};   // 0.01° resolution
    std::array<QDoubleSpinBox*, 6> joint_spins_    {};
    QPushButton                   *send_joints_btn_ = nullptr;
    QPushButton                   *joints_zero_btn_ = nullptr;

    // cartesian tab
    std::array<QDoubleSpinBox*, 6> cart_spins_     {};   // X/Y/Z mm + RX/RY/RZ deg
    std::array<QDoubleSpinBox*, 6> cart_deltas_    {};   // ΔX..ΔRZ jog step sizes
    int                            cart_mode_idx_   = 0; // 0 = MOVE_P, 1 = MOVE_L
    QPushButton                   *send_cart_btn_   = nullptr;

    // gripper tab
    QSlider      *gripper_slider_   = nullptr;       // 0..80 mm × 100
    QDoubleSpinBox *gripper_spin_   = nullptr;
    QPushButton  *gripper_open_btn_ = nullptr;
    QPushButton  *gripper_close_btn_= nullptr;

    // right panel
    ArmViewer3D  *viewer_3d_     = nullptr;
    QLabel       *pose_readout_  = nullptr;
    std::array<QLabel*, 6> joint_readouts_ {};

    // 3D model loader (runs ArmSyncWorker::loadAssets off the GUI thread
    // so the 9 MB of Piper STLs don't freeze the UI on startup). We only
    // use the worker for asset loading — the live joint angles are pushed
    // directly from pollJointsAndPose() into viewer_3d_->setJointAngles(),
    // so we don't run the worker's startAnglePolling path.
    QThread        *sim_thread_  = nullptr;
    ArmSyncWorker  *sim_worker_  = nullptr;

    // status bar
    QLabel       *status_ctrl_   = nullptr;
    QLabel       *status_arm_    = nullptr;
    QLabel       *status_motion_ = nullptr;
    QLabel       *status_heartbeat_ = nullptr;

    // timers
    QTimer       *poll_state_timer_  = nullptr;
    QTimer       *poll_status_timer_ = nullptr;
};

#endif // PIPERWIDGET_H
