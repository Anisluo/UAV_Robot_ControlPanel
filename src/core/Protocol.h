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
    constexpr const char* ARM_JOG            = "arm.jog";
    constexpr const char* ARM_HOME           = "arm.home";
    constexpr const char* UGV_SET_VELOCITY   = "ugv.set_velocity";
    constexpr const char* UGV_STOP           = "ugv.stop";
    constexpr const char* AIRPORT_SET_RAIL   = "airport.set_rail";
    constexpr const char* AIRPORT_GRIPPER    = "airport.gripper";
    constexpr const char* ARM_GRIPPER_SET    = "arm_gripper.set";
    constexpr const char* CAMERA_SET_PROFILE = "camera.set_profile";
    constexpr const char* CAMERA_SET_EXPOSURE= "camera.set_exposure";
    constexpr const char* CONFIG_SET_CAN     = "config.set_can";
    constexpr const char* CONFIG_SET_SERIAL  = "config.set_serial";
    constexpr const char* CONFIG_SET_RELAY   = "config.set_relay";
    constexpr const char* CONFIG_SET_ETHERNET= "config.set_ethernet";
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
    constexpr const char* AXIS   = "axis";
    constexpr const char* DELTA  = "delta";

    // ugv
    constexpr const char* VX    = "vx";
    constexpr const char* VY    = "vy";
    constexpr const char* OMEGA = "omega";

    // airport
    constexpr const char* RAIL   = "rail";
    constexpr const char* POS_MM = "pos_mm";
    constexpr const char* OPEN   = "open";

    // arm_gripper
    constexpr const char* POS = "pos";

    // camera
    constexpr const char* WIDTH       = "width";
    constexpr const char* HEIGHT      = "height";
    constexpr const char* FPS         = "fps";
    constexpr const char* EXPOSURE_US = "exposure_us";

    // config
    constexpr const char* DEVICE      = "device";
    constexpr const char* BITRATE     = "bitrate";
    constexpr const char* BAUD_RATE   = "baud_rate";
    constexpr const char* GPIO_PIN    = "gpio_pin";
    constexpr const char* ACTIVE_LOW  = "active_low";
    constexpr const char* HOST        = "host";
    constexpr const char* RPC_PORT_F  = "rpc_port";
    constexpr const char* VIDEO_PORT_F= "video_port";
}

} // namespace Protocol

#endif // PROTOCOL_H
