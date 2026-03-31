#include "Tab3Help.h"

#include <QVBoxLayout>
#include <QTextBrowser>

Tab3Help::Tab3Help(QWidget *parent)
    : QWidget(parent)
    , browser_(new QTextBrowser(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    browser_->setOpenExternalLinks(false);
    browser_->setHtml(helpHtml());
    layout->addWidget(browser_);
}

QString Tab3Help::helpHtml()
{
    return QStringLiteral(
        "<!DOCTYPE html><html><head>"
        "<style>"
        "  body { background-color: #1e1f2e; color: #dde1f0; font-family: 'Segoe UI', sans-serif; "
        "         font-size: 13px; margin: 16px; }"
        "  h1   { color: #00c8d7; border-bottom: 1px solid #353650; padding-bottom: 6px; }"
        "  h2   { color: #00c8d7; margin-top: 18px; }"
        "  h3   { color: #88aacc; margin-top: 12px; }"
        "  code { background: #12131a; color: #00c8d7; font-family: Consolas, monospace; "
        "         padding: 2px 5px; border-radius: 3px; font-size: 12px; }"
        "  pre  { background: #12131a; color: #dde1f0; font-family: Consolas, monospace; "
        "         padding: 10px; border-radius: 4px; border: 1px solid #353650; "
        "         font-size: 12px; overflow-x: auto; }"
        "  table { border-collapse: collapse; width: 100%; }"
        "  th    { background: #252638; color: #00c8d7; padding: 6px 10px; "
        "          border: 1px solid #353650; text-align: left; }"
        "  td    { padding: 5px 10px; border: 1px solid #353650; color: #dde1f0; }"
        "  tr:nth-child(even) td { background: #1e1f2e; }"
        "  tr:nth-child(odd)  td { background: #252638; }"
        "  .note { background: #252638; border-left: 3px solid #ff9800; padding: 8px 12px; "
        "          margin: 10px 0; border-radius: 3px; }"
        "  .tip  { background: #252638; border-left: 3px solid #4caf50; padding: 8px 12px; "
        "          margin: 10px 0; border-radius: 3px; }"
        "</style>"
        "</head><body>"

        "<h1>无人机机器人控制台 &ndash; 帮助文档</h1>"

        "<h2>系统架构</h2>"
        "<p>HostGUI 运行于 Linux 工作站（Ubuntu / CentOS），与基于 RK3588 的机器人控制器<em>仅</em>通过 TCP/IP 通信。"
        "主机与机器人之间不存在 USB、串口或 CAN 直接连接，所有子系统控制均通过 RK3588 转发。</p>"
        "<pre>"
        "  [ Linux HostGUI ]  &lt;--- TCP 7001 (JSON-RPC) ---&gt; [ RK3588 机器人控制器 ]\n"
        "                     &lt;--- TCP 7002 (MJPEG)    ---&gt;\n"
        "\n"
        "  RK3588 内部控制:\n"
        "    - 六轴机械臂  (CAN总线)\n"
        "    - 六轮无人车  (CAN总线)\n"
        "    - 机场平台    (CAN总线)\n"
        "    - 机场夹爪    (GPIO继电器)\n"
        "    - 机械臂夹爪  (串口)\n"
        "    - 摄像头      (USB/MIPI)\n"
        "</pre>"

        "<h2>连接设置</h2>"
        "<p>在<strong>主控面板</strong>标签页右侧的连接设置面板中进行操作：</p>"
        "<ol>"
        "  <li>输入 RK3588 的 IP 地址（默认：<code>192.168.1.100</code>）。</li>"
        "  <li>RPC 端口默认：<code>7001</code>，视频端口默认：<code>7002</code>。</li>"
        "  <li>点击<strong>连接</strong>，连接成功后状态指示灯变为绿色。</li>"
        "  <li>收到数据后摄像头画面将自动显示。</li>"
        "</ol>"
        "<div class='note'>若连接失败，请确认 RK3588 服务端正在运行，且防火墙已放行 TCP 7001 和 7002 端口。</div>"

        "<h2>JSON-RPC 协议（端口 7001）</h2>"
        "<p>所有控制指令均通过持久化 TCP 连接以换行符分隔的 JSON 格式传输。</p>"
        "<h3>请求格式</h3>"
        "<pre>{\"id\": 1, \"method\": \"subsystem.action\", \"params\": {...}}\\n</pre>"
        "<h3>响应格式</h3>"
        "<pre>{\"id\": 1, \"result\": {...}}\\n          &larr; 成功\n"
        "{\"id\": 1, \"error\": \"message\"}\\n      &larr; 失败</pre>"

        "<h3>可用方法</h3>"
        "<table>"
        "<tr><th>方法</th><th>参数</th><th>说明</th></tr>"
        "<tr><td><code>system.ping</code></td><td><code>{}</code></td>"
        "    <td>检测连通性，返回 <code>{pong:true, uptime_ms:N}</code>。</td></tr>"
        "<tr><td><code>arm.set_joints</code></td><td><code>{joints:[j0..j5]}</code></td>"
        "    <td>设置全部6个关节角度（度）。</td></tr>"
        "<tr><td><code>arm.jog</code></td><td><code>{axis:0-5, delta:float}</code></td>"
        "    <td>单轴相对点动，增量单位为度。</td></tr>"
        "<tr><td><code>arm.home</code></td><td><code>{}</code></td>"
        "    <td>机械臂回零（所有关节归零）。</td></tr>"
        "<tr><td><code>ugv.set_velocity</code></td><td><code>{vx, vy, omega}</code></td>"
        "    <td>设置无人车本体速度，vx/vy 单位 m/s，omega 单位 rad/s。</td></tr>"
        "<tr><td><code>ugv.stop</code></td><td><code>{}</code></td>"
        "    <td>急停 &ndash; 立即停止无人车运动。</td></tr>"
        "<tr><td><code>airport.set_rail</code></td><td><code>{rail:0-2, pos_mm:float}</code></td>"
        "    <td>将导轨 0、1 或 2 移动至指定位置（mm）。</td></tr>"
        "<tr><td><code>airport.gripper</code></td><td><code>{open:bool}</code></td>"
        "    <td>张开或闭合机场夹爪（继电器驱动）。</td></tr>"
        "<tr><td><code>arm_gripper.set</code></td><td><code>{open:bool}</code></td>"
        "    <td>张开或闭合机械臂夹爪（串口指令）。</td></tr>"
        "<tr><td><code>camera.set_profile</code></td><td><code>{width, height, fps}</code></td>"
        "    <td>修改摄像头采集分辨率和帧率。</td></tr>"
        "<tr><td><code>camera.set_exposure</code></td><td><code>{exposure_us:int}</code></td>"
        "    <td>设置摄像头手动曝光时间（微秒）。</td></tr>"
        "<tr><td><code>config.set_can</code></td><td><code>{device, bitrate}</code></td>"
        "    <td>配置机器人端 CAN 总线接口。</td></tr>"
        "<tr><td><code>config.set_serial</code></td><td><code>{device, baud_rate}</code></td>"
        "    <td>配置机器人端串口。</td></tr>"
        "<tr><td><code>config.set_relay</code></td><td><code>{gpio_pin, active_low}</code></td>"
        "    <td>配置继电器 GPIO 引脚及有效电平极性。</td></tr>"
        "<tr><td><code>config.set_ethernet</code></td><td><code>{host, rpc_port, video_port}</code></td>"
        "    <td>更新机器人端网络配置。</td></tr>"
        "</table>"

        "<h2>视频流（端口 7002）</h2>"
        "<p>MJPEG 帧以长度前缀二进制包的形式传输：</p>"
        "<pre>[4字节大端序 uint32 = JPEG字节数][JPEG字节数据...]</pre>"
        "<p>GUI 视频流连接与 RPC 连接相互独立，断线后自动重连。"
        "若 3 秒内未收到帧数据，摄像头控件将显示<strong>无信号</strong>。</p>"

        "<h2>子系统详情</h2>"

        "<h3>六轴机械臂 [CAN]</h3>"
        "<p>通过 CAN 总线控制六自由度机械臂。关节角度单位为度，范围"
        "&minus;180&deg; 至 +180&deg;。可使用滑块或数值框调节，变化量经防抖处理（150 ms）后再发送，"
        "以减少网络流量。点击<strong>回零</strong>按钮可将所有关节归零。</p>"

        "<h3>六轮无人车 [CAN]</h3>"
        "<p>控制全向/麦克纳姆轮地面车辆。Vx 和 Vy 为平移速度（m/s，范围 &plusmn;1.0），"
        "omega 为旋转角速度（rad/s，范围 &plusmn;&pi;）。"
        "红色<strong>急停</strong>按钮立即发送 <code>ugv.stop</code> 并将所有速度域清零。</p>"

        "<h3>机场平台 [CAN]</h3>"
        "<p>控制 3 条直线导轨（0&ndash;1000 mm），每条导轨可独立设置。"
        "点击<strong>全部回零</strong>可将所有导轨移至 0 mm 位置。</p>"

        "<h3>夹爪控制</h3>"
        "<p><em>机场夹爪</em>由继电器控制（二值开合）。"
        "<em>机械臂夹爪</em>为串口舵机，通过位置百分比（0&ndash;100%）控制。</p>"

        "<h3>无线通信网络地图</h3>"
        "<p>可视化无线网状网络拓扑。每个节点以圆形显示："
        "<span style='color:#4caf50'>绿色 = 可达</span>，"
        "<span style='color:#555770'>灰色 = 不可达</span>。"
        "节点间连线表示链路，线条粗细和颜色反映 RSSI 信号强度。"
        "各节点上方显示 RSSI 数值。"
        "启动时地图以演示数据填充。</p>"

        "<h2>接口配置（标签页2）</h2>"
        "<p><em>接口配置</em>标签页用于配置 RK3588 端的硬件接口。"
        "设置通过 RPC 发送并在机器人端生效。此处修改不影响 HostGUI 与机器人之间的 TCP 连接本身"
        "（如需修改连接参数，请使用主控面板中的连接设置面板）。</p>"
        "<p>CAN 总线设备固定为三个选项："
        "<code>can1</code> 对应平台直线电机，"
        "<code>can3</code> 对应小车底盘，"
        "<code>can4</code> 对应机械臂；"
        "界面中可直接配置对应总线的波特率。</p>"

        "<div class='tip'>提示：通过以太网配置修改网络设置后，"
        "可能需要在主控面板中使用新的地址和端口重新连接。</div>"

        "<h2>日志面板</h2>"
        "<p>日志面板（主控面板左下方）显示来自 RPC 及视频客户端的带时间戳消息。"
        "可使用筛选下拉框仅显示信息、警告或错误消息。"
        "颜色说明："
        "<span style='color:#dde1f0'>信息 = 白色</span>，"
        "<span style='color:#ff9800'>警告 = 橙色</span>，"
        "<span style='color:#f44336'>错误 = 红色</span>，"
        "<span style='color:#888aaa'>调试 = 灰色</span>。</p>"

        "</body></html>"
    );
}
