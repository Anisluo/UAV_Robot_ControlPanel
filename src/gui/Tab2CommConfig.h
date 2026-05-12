#ifndef TAB2COMMCONFIG_H
#define TAB2COMMCONFIG_H

#include <QWidget>
#include <QList>

class RpcClient;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QLabel;
class QSettings;

// Tab "接口配置" — a 2-column grid of major modules; every module is
// itself a stack of small sub-group-boxes covering CAN / serial / network
// / motion / encoder / safety / image-pipeline parameters. Most fields
// are read-only persisted (saved to QSettings) since the underlying
// proc_* services consume them at boot via uav_robot.env. The "应用"
// buttons still push the primary fields through the existing RPC config
// methods.
class Tab2CommConfig : public QWidget {
    Q_OBJECT
public:
    explicit Tab2CommConfig(RpcClient *rpc, QWidget *parent = nullptr);

    void setConnectionParams(const QString &host, quint16 rpcPort, quint16 videoPort);
    void loadConfig(QSettings &s);
    void saveConfig(QSettings &s) const;

private slots:
    void onApplyHost();
    void onApplyArm();
    void onApplyCar();
    void onApplyAirportRail();
    void onApplyAirportRelay();
    void onApplyRealsense();
    void onApplyNpu();
    void onApplyGripperUart();

    // Toggles edit-lock on every module group. Default = locked so a stray
    // click can't change a CAN baud rate / GPIO mapping by accident.
    void onToggleLock();

private:
    void buildUi();
    void applyLockState();   // disables/enables every module group widget
    QWidget *buildHostBox();
    QWidget *buildArmBox();
    QWidget *buildCarBox();
    QWidget *buildAirportRailBox();
    QWidget *buildAirportRelayBox();
    QWidget *buildRealsenseBox();
    QWidget *buildNpuBox();
    QWidget *buildGripperUartBox();

    RpcClient *rpc_;

    // ── Edit-lock ─────────────────────────────────────────────────────
    QPushButton  *lock_button_   = nullptr;
    QLabel       *lock_note_     = nullptr;
    QList<QWidget*> module_groups_;       // 8 top-level boxes, used by applyLockState
    bool          params_locked_ = true;  // default: locked

    // ── Host connection ────────────────────────────────────────────────
    QLineEdit  *host_edit_              = nullptr;
    QLineEdit  *subnet_edit_            = nullptr;
    QLineEdit  *gateway_edit_           = nullptr;
    QLineEdit  *mac_edit_               = nullptr;
    QComboBox  *eth_iface_combo_        = nullptr;
    QSpinBox   *mtu_spin_               = nullptr;

    QSpinBox   *rpc_port_spin_          = nullptr;
    QSpinBox   *video_port_spin_        = nullptr;
    QSpinBox   *robotd_port_spin_       = nullptr;
    QSpinBox   *telemetry_port_spin_    = nullptr;

    QSpinBox   *reconnect_timeout_spin_ = nullptr;
    QSpinBox   *retry_count_spin_       = nullptr;
    QSpinBox   *heartbeat_spin_         = nullptr;
    QComboBox  *keepalive_combo_        = nullptr;

    // ── Arm ────────────────────────────────────────────────────────────
    QComboBox  *arm_backend_combo_   = nullptr;
    QLineEdit  *arm_can_edit_        = nullptr;
    QSpinBox   *arm_bitrate_spin_    = nullptr;
    QSpinBox   *arm_fifo_spin_       = nullptr;
    QSpinBox   *arm_ack_timeout_spin_= nullptr;

    QSpinBox   *arm_speed_spin_      = nullptr;
    QSpinBox   *arm_acc_spin_        = nullptr;
    QSpinBox   *arm_jerk_spin_       = nullptr;
    QSpinBox   *arm_speed_scale_spin_= nullptr;
    QSpinBox   *arm_reached_to_spin_ = nullptr;

    QSpinBox   *arm_j_min_spin_[6]   = {nullptr};
    QSpinBox   *arm_j_max_spin_[6]   = {nullptr};

    QDoubleSpinBox *arm_tcp_x_spin_  = nullptr;
    QDoubleSpinBox *arm_tcp_y_spin_  = nullptr;
    QDoubleSpinBox *arm_tcp_z_spin_  = nullptr;
    QDoubleSpinBox *arm_payload_spin_= nullptr;

    // ── Car chassis ────────────────────────────────────────────────────
    QLineEdit  *car_can_edit_        = nullptr;
    QSpinBox   *car_bitrate_spin_    = nullptr;
    QSpinBox   *car_fifo_spin_       = nullptr;

    QSpinBox   *car_track_spin_      = nullptr;
    QSpinBox   *car_wheel_diam_spin_ = nullptr;
    QDoubleSpinBox *car_gear_ratio_spin_ = nullptr;

    QSpinBox   *car_min_rpm_spin_       = nullptr;
    QSpinBox   *car_turn_min_rpm_spin_  = nullptr;
    QSpinBox   *car_max_rpm_spin_       = nullptr;
    QDoubleSpinBox *car_mmps_per_rpm_spin_ = nullptr;

    QSpinBox   *car_limit_pwm_spin_     = nullptr;
    QSpinBox   *car_turn_limit_pwm_spin_= nullptr;
    QDoubleSpinBox *car_left_trim_spin_ = nullptr;
    QDoubleSpinBox *car_right_trim_spin_= nullptr;
    QSpinBox   *car_ramp_steps_spin_    = nullptr;
    QSpinBox   *car_ramp_dt_spin_       = nullptr;
    QSpinBox   *car_send_interval_spin_ = nullptr;

    // ── Airport rails ──────────────────────────────────────────────────
    QLineEdit  *ap_can_edit_         = nullptr;
    QSpinBox   *ap_bitrate_spin_     = nullptr;
    QSpinBox   *ap_rail1_addr_spin_  = nullptr;
    QSpinBox   *ap_rail2_addr_spin_  = nullptr;
    QSpinBox   *ap_rail3_addr_spin_  = nullptr;

    QSpinBox   *ap_rail_rpm_spin_       = nullptr;
    QSpinBox   *ap_rail_acc_spin_       = nullptr;
    QSpinBox   *ap_pulses_per_mm_spin_  = nullptr;
    QSpinBox   *ap_home_offset_spin_    = nullptr;
    QComboBox  *ap_home_dir_combo_      = nullptr;

    QSpinBox   *ap_rail_min_spin_[3]    = {nullptr};
    QSpinBox   *ap_rail_max_spin_[3]    = {nullptr};

    // ── Airport relays ─────────────────────────────────────────────────
    QLineEdit  *ap_relay1_path_edit_      = nullptr;
    QLineEdit  *ap_relay2_path_edit_      = nullptr;
    QLineEdit  *ap_relay3_path_edit_      = nullptr;
    QLineEdit  *ap_relay4_path_edit_      = nullptr;
    QLineEdit  *ap_gripper_relay_edit_    = nullptr;
    QComboBox  *ap_relay_active_combo_    = nullptr;
    QComboBox  *ap_gpio_mode_combo_       = nullptr;
    QSpinBox   *ap_pulse_width_spin_      = nullptr;
    QSpinBox   *ap_debounce_spin_         = nullptr;
    QSpinBox   *ap_watchdog_spin_         = nullptr;
    QComboBox  *ap_estop_combo_           = nullptr;

    // ── RealSense ──────────────────────────────────────────────────────
    QSpinBox   *rs_width_spin_       = nullptr;
    QSpinBox   *rs_height_spin_      = nullptr;
    QSpinBox   *rs_fps_spin_         = nullptr;
    QComboBox  *rs_color_format_combo_ = nullptr;

    QSpinBox   *rs_depth_w_spin_     = nullptr;
    QSpinBox   *rs_depth_h_spin_     = nullptr;
    QSpinBox   *rs_depth_fps_spin_   = nullptr;
    QComboBox  *rs_depth_combo_      = nullptr;
    QSpinBox   *rs_laser_power_spin_ = nullptr;

    QComboBox  *rs_exposure_auto_combo_ = nullptr;
    QSpinBox   *rs_exposure_us_spin_    = nullptr;
    QSpinBox   *rs_gain_spin_           = nullptr;
    QComboBox  *rs_wb_combo_            = nullptr;
    QSpinBox   *rs_decimation_spin_     = nullptr;
    QSpinBox   *rs_temporal_spin_       = nullptr;
    QSpinBox   *rs_spatial_spin_        = nullptr;
    QComboBox  *rs_align_combo_         = nullptr;

    // ── NPU ────────────────────────────────────────────────────────────
    QComboBox  *npu_strategy_combo_   = nullptr;
    QLineEdit  *npu_model_path_edit_  = nullptr;
    QComboBox  *npu_core_combo_       = nullptr;

    QDoubleSpinBox *npu_threshold_spin_     = nullptr;
    QDoubleSpinBox *npu_nms_thresh_spin_    = nullptr;
    QSpinBox       *npu_input_size_spin_    = nullptr;
    QSpinBox       *npu_max_boxes_spin_     = nullptr;

    QSpinBox       *npu_top_k_spin_         = nullptr;
    QLineEdit      *npu_class_filter_edit_  = nullptr;
    QComboBox      *npu_post_decode_combo_  = nullptr;

    // ── Gripper UART (legacy) ──────────────────────────────────────────
    QLineEdit  *gripper_uart_edit_           = nullptr;
    QComboBox  *gripper_uart_baud_combo_     = nullptr;
    QComboBox  *gripper_uart_databits_combo_ = nullptr;
    QComboBox  *gripper_uart_stopbits_combo_ = nullptr;
    QComboBox  *gripper_uart_parity_combo_   = nullptr;
    QSpinBox   *gripper_uart_timeout_spin_   = nullptr;
    QComboBox  *gripper_uart_flow_combo_     = nullptr;
    QSpinBox   *gripper_uart_retry_spin_     = nullptr;
};

#endif // TAB2COMMCONFIG_H
