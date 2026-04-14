#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QtGlobal>

// JSON-RPC over TCP (port 7001)
// Video stream (port 7002)

namespace Protocol {

// Default ports
constexpr quint16 RPC_PORT   = 7001;
constexpr quint16 VIDEO_PORT = 7002;

// Method name constants
namespace Methods {
    constexpr const char* SYSTEM_PING        = "system.ping";
    constexpr const char* ARM_SET_JOINTS     = "arm.set_joints";
    constexpr const char* ARM_GET_ANGLES     = "arm.get_angles";
    constexpr const char* ARM_JOG            = "arm.jog";
    constexpr const char* ARM_HOME           = "arm.home";
    constexpr const char* ARM_HOME_JOINT     = "arm.home_joint";
    constexpr const char* ARM_MOVE_JOINT     = "arm.move_joint";
    constexpr const char* ARM_STOP           = "arm.stop";
    constexpr const char* ARM_SET_SPEEDS     = "arm.set_speeds";
    constexpr const char* UGV_SET_VELOCITY   = "ugv.set_velocity";
    constexpr const char* UGV_STOP           = "ugv.stop";
    constexpr const char* AIRPORT_SET_RAIL   = "airport.set_rail";
    constexpr const char* AIRPORT_SET_SPEED  = "airport.set_speed";
    constexpr const char* AIRPORT_STOP       = "airport.stop";
    constexpr const char* AIRPORT_LOCK       = "airport.lock";
    constexpr const char* AIRPORT_RELEASE    = "airport.release";
    constexpr const char* AIRPORT_STOP_ALL   = "airport.stop_all";
    constexpr const char* AIRPORT_GRIPPER    = "airport.gripper";
    constexpr const char* ARM_GRIPPER_SET    = "arm_gripper.set";
    constexpr const char* CAMERA_SET_PROFILE = "camera.set_profile";
    constexpr const char* CAMERA_SET_EXPOSURE= "camera.set_exposure";
    constexpr const char* CONFIG_SET_CAN     = "config.set_can";
    constexpr const char* CONFIG_SET_SERIAL  = "config.set_serial";
    constexpr const char* CONFIG_SET_RELAY   = "config.set_relay";
    constexpr const char* CONFIG_SET_ETHERNET= "config.set_ethernet";
    // NPU / recognition
    constexpr const char* NPU_START          = "npu.start";
    constexpr const char* NPU_STOP           = "npu.stop";
    constexpr const char* NPU_SET_STRATEGY   = "npu.set_strategy";
    constexpr const char* NPU_SET_THRESHOLD  = "npu.set_threshold";
    constexpr const char* NPU_GET_DETECTIONS = "npu.get_detections";
    constexpr const char* NPU_GET_STATUS     = "npu.get_status";
    // Task control
    constexpr const char* TASK_START         = "task.start";
    constexpr const char* TASK_STOP          = "task.stop";
    constexpr const char* TASK_RESET         = "task.reset";
    constexpr const char* TASK_GET_STATUS    = "task.get_status";
    // Video control
    constexpr const char* VIDEO_SET_ENABLED  = "video.set_enabled";
    constexpr const char* VIDEO_GET_STATUS   = "video.get_status";
    constexpr const char* VIDEO_SET_SOURCE   = "video.set_source";  // "rgb" | "depth"
}

// JSON field name constants
namespace Fields {
    constexpr const char* ID      = "id";
    constexpr const char* METHOD  = "method";
    constexpr const char* PARAMS  = "params";
    constexpr const char* RESULT  = "result";
    constexpr const char* ERROR   = "error";

    // system.ping result
    constexpr const char* PONG      = "pong";
    constexpr const char* UPTIME_MS = "uptime_ms";

    // arm
    constexpr const char* JOINTS = "joints";
    constexpr const char* ANGLES = "angles";
    constexpr const char* AXIS   = "axis";
    constexpr const char* DELTA  = "delta";

    // ugv
    constexpr const char* VX    = "vx";
    constexpr const char* VY    = "vy";
    constexpr const char* OMEGA = "omega";

    // airport
    constexpr const char* RAIL   = "rail";
    constexpr const char* POS_MM = "pos_mm";
    constexpr const char* DELTA_MM = "delta_mm";
    constexpr const char* SPEED_RPM = "speed_rpm";
    constexpr const char* OPEN   = "open";

    // arm_gripper
    constexpr const char* POS = "pos";

    // camera
    constexpr const char* WIDTH       = "width";
    constexpr const char* HEIGHT      = "height";
    constexpr const char* FPS         = "fps";
    constexpr const char* EXPOSURE_US = "exposure_us";

    // npu / detection
    constexpr const char* STRATEGY_ID  = "strategy_id";
    constexpr const char* THRESHOLD    = "threshold";
    constexpr const char* FRAME_ID     = "frame_id";
    constexpr const char* NUM_DETS     = "num_detections";
    constexpr const char* DETECTIONS   = "detections";
    constexpr const char* CLASS_ID     = "class_id";
    constexpr const char* SCORE        = "score";
    constexpr const char* X1           = "x1";
    constexpr const char* Y1           = "y1";
    constexpr const char* X2           = "x2";
    constexpr const char* Y2           = "y2";
    constexpr const char* X_MM         = "x_mm";
    constexpr const char* Y_MM         = "y_mm";
    constexpr const char* Z_MM         = "z_mm";
    constexpr const char* HAS_XYZ      = "has_xyz";
    // task
    constexpr const char* TASK_NAME    = "task";
    constexpr const char* TASK_STATE   = "task_state";
    // config
    constexpr const char* DEVICE      = "device";
    constexpr const char* BITRATE     = "bitrate";
    constexpr const char* BAUD_RATE   = "baud_rate";
    constexpr const char* GPIO_PIN    = "gpio_pin";
    constexpr const char* ACTIVE_LOW  = "active_low";
    constexpr const char* HOST        = "host";
    constexpr const char* RPC_PORT_F  = "rpc_port";
    constexpr const char* VIDEO_PORT_F= "video_port";
    constexpr const char* ENABLED     = "enabled";
    constexpr const char* CLIENTS     = "clients";
}

} // namespace Protocol

#endif // PROTOCOL_H
