#include "Tab2CommConfig.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QFrame>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QString>

Tab2CommConfig::Tab2CommConfig(RpcClient *rpc, QWidget *parent)
    : QWidget(parent)
    , rpc_(rpc)
{
    buildUi();
}

// ── Visual helpers ──────────────────────────────────────────────────────
namespace {

void styleOuter(QGroupBox *g, const QColor &accent)
{
    g->setStyleSheet(QString(
        "QGroupBox {"
        " border: 1px solid %1;"
        " border-radius: 6px;"
        " margin-top: 12px;"
        " font-weight: bold;"
        " color: #d8e0f0;"
        " background: rgba(28,34,50,200);"
        "}"
        "QGroupBox::title {"
        " subcontrol-origin: margin;"
        " left: 10px;"
        " padding: 0 8px;"
        " color: %1;"
        " font-size: 13px;"
        "}"
    ).arg(accent.name()));
}

QGroupBox *makeSub(QWidget *parent, const QString &title, const QColor &accent)
{
    auto *g = new QGroupBox(title, parent);
    g->setStyleSheet(QString(
        "QGroupBox {"
        " border: 1px dashed rgba(%1,%2,%3,180);"
        " border-radius: 4px;"
        " margin-top: 10px;"
        " font-weight: normal;"
        " color: #b0bcd6;"
        " background: rgba(22,28,40,160);"
        " padding: 0;"
        "}"
        "QGroupBox::title {"
        " subcontrol-origin: margin;"
        " left: 8px;"
        " padding: 0 4px;"
        " color: rgba(%1,%2,%3,255);"
        " font-size: 11px;"
        "}"
    ).arg(accent.red()).arg(accent.green()).arg(accent.blue()));
    return g;
}

QFormLayout *innerForm(QGroupBox *g)
{
    auto *f = new QFormLayout(g);
    f->setContentsMargins(8, 14, 8, 6);
    f->setSpacing(4);
    f->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return f;
}

QSpinBox *mkInt(QWidget *p, int min, int max, int val, const QString &suffix = {})
{
    auto *s = new QSpinBox(p);
    s->setRange(min, max);
    s->setValue(val);
    if (!suffix.isEmpty()) s->setSuffix(" " + suffix);
    s->setMinimumWidth(96);
    return s;
}

QDoubleSpinBox *mkDouble(QWidget *p, double min, double max, double val,
                         int decimals = 2, double step = 0.1,
                         const QString &suffix = {})
{
    auto *s = new QDoubleSpinBox(p);
    s->setRange(min, max);
    s->setValue(val);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    if (!suffix.isEmpty()) s->setSuffix(" " + suffix);
    s->setMinimumWidth(96);
    return s;
}

QPushButton *makeApply(QWidget *parent)
{
    auto *btn = new QPushButton(QStringLiteral("应用本模块"), parent);
    btn->setFixedHeight(26);
    btn->setMinimumWidth(110);
    btn->setStyleSheet(
        "QPushButton {"
        " background:#2d5fb2; color:white; border-radius:3px;"
        " font-weight:bold; padding:2px 12px;"
        "}"
        "QPushButton:hover  { background:#3a73c9; }"
        "QPushButton:pressed{ background:#234d8d; }");
    return btn;
}

} // namespace

void Tab2CommConfig::buildUi()
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *container = new QWidget(scrollArea);
    auto *outer = new QVBoxLayout(container);
    outer->setSpacing(10);
    outer->setContentsMargins(12, 12, 12, 12);

    // ── Top bar: status note + lock toggle ───────────────────────────────
    auto *headerBar = new QWidget(container);
    auto *headerLay = new QHBoxLayout(headerBar);
    headerLay->setContentsMargins(0, 0, 0, 0);
    headerLay->setSpacing(8);

    auto *note = new QLabel(
        QStringLiteral(
        "本页镜像 uav_robot.env 与 proc_* 服务的运行参数,共 8 大模块、"
        "27 个子配置块。除主机连接 / NPU 策略等少数运行时可改字段外,"
        "其余在 systemd 服务启动时读取,修改后需重启对应单元才会生效。"),
        headerBar);
    note->setWordWrap(true);
    note->setStyleSheet(
        "color:#ffb37a; font-style:italic; padding:6px 8px;"
        "background: rgba(60,40,20,120); border-radius:4px;");
    headerLay->addWidget(note, /*stretch=*/1);

    auto *lockCol = new QWidget(headerBar);
    auto *lockColLay = new QVBoxLayout(lockCol);
    lockColLay->setContentsMargins(0, 0, 0, 0);
    lockColLay->setSpacing(2);
    lock_button_ = new QPushButton(QStringLiteral("🔒  已锁定 — 点击解锁"), lockCol);
    lock_button_->setFixedHeight(40);
    lock_button_->setMinimumWidth(190);
    lock_button_->setCursor(Qt::PointingHandCursor);
    lock_note_ = new QLabel(QStringLiteral("参数不可编辑"), lockCol);
    lock_note_->setAlignment(Qt::AlignHCenter);
    lock_note_->setStyleSheet("color:#9aa0b8; font-size:10px;");
    lockColLay->addWidget(lock_button_);
    lockColLay->addWidget(lock_note_);
    headerLay->addWidget(lockCol);
    connect(lock_button_, &QPushButton::clicked, this, &Tab2CommConfig::onToggleLock);

    outer->addWidget(headerBar);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);
    QWidget *m_host    = buildHostBox();         module_groups_.append(m_host);
    QWidget *m_arm     = buildArmBox();          module_groups_.append(m_arm);
    QWidget *m_car     = buildCarBox();          module_groups_.append(m_car);
    QWidget *m_aprail  = buildAirportRailBox();  module_groups_.append(m_aprail);
    QWidget *m_aprelay = buildAirportRelayBox(); module_groups_.append(m_aprelay);
    QWidget *m_rs      = buildRealsenseBox();    module_groups_.append(m_rs);
    QWidget *m_npu     = buildNpuBox();          module_groups_.append(m_npu);
    QWidget *m_grip    = buildGripperUartBox();  module_groups_.append(m_grip);
    // 3-column layout: 8 modules → 3 rows (3 + 3 + 2). Wider screen, less
    // vertical scrolling.
    grid->addWidget(m_host,    0, 0);
    grid->addWidget(m_arm,     0, 1);
    grid->addWidget(m_car,     0, 2);
    grid->addWidget(m_aprail,  1, 0);
    grid->addWidget(m_aprelay, 1, 1);
    grid->addWidget(m_rs,      1, 2);
    grid->addWidget(m_npu,     2, 0);
    grid->addWidget(m_grip,    2, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    outer->addLayout(grid);
    outer->addStretch();

    // Default state: locked. Disables every module group so the user
    // can't accidentally tweak a CAN / GPIO / port value until they
    // explicitly click "解锁".
    applyLockState();

    container->setLayout(outer);
    scrollArea->setWidget(container);
    outerLayout->addWidget(scrollArea);
}

// ════════════════════════════════════════════════════════════════════════
// MODULE 1: HOST CONNECTION
// ════════════════════════════════════════════════════════════════════════
QWidget *Tab2CommConfig::buildHostBox()
{
    const QColor ACC(0x6e, 0xb8, 0xff);
    auto *grp = new QGroupBox(QStringLiteral("◆  主机连接"), this);
    styleOuter(grp, ACC);
    auto *v = new QVBoxLayout(grp);
    v->setContentsMargins(10, 18, 10, 10);
    v->setSpacing(8);

    // Sub A: 网络地址
    {
        auto *sub = makeSub(grp, QStringLiteral("网络地址"), ACC);
        auto *f = innerForm(sub);
        host_edit_     = new QLineEdit("192.168.200.101", sub);
        subnet_edit_   = new QLineEdit("255.255.255.0", sub);
        gateway_edit_  = new QLineEdit("192.168.200.1", sub);
        mac_edit_      = new QLineEdit("AA:BB:CC:11:22:33", sub);
        eth_iface_combo_ = new QComboBox(sub);
        eth_iface_combo_->addItems({"eth0", "eth1", "wlan0"});
        mtu_spin_      = mkInt(sub, 576, 9000, 1500);
        f->addRow(QStringLiteral("主机 IP:"),  host_edit_);
        f->addRow(QStringLiteral("子网掩码:"), subnet_edit_);
        f->addRow(QStringLiteral("默认网关:"), gateway_edit_);
        f->addRow(QStringLiteral("本机 MAC:"), mac_edit_);
        f->addRow(QStringLiteral("接口:"),     eth_iface_combo_);
        f->addRow(QStringLiteral("MTU:"),      mtu_spin_);
        v->addWidget(sub);
    }
    // Sub B: TCP 端口
    {
        auto *sub = makeSub(grp, QStringLiteral("TCP 端口"), ACC);
        auto *f = innerForm(sub);
        rpc_port_spin_       = mkInt(sub, 1, 65535, Protocol::RPC_PORT);
        video_port_spin_     = mkInt(sub, 1, 65535, Protocol::VIDEO_PORT);
        robotd_port_spin_    = mkInt(sub, 1, 65535, 9001);
        telemetry_port_spin_ = mkInt(sub, 1, 65535, 7003);
        f->addRow(QStringLiteral("RPC 端口:"),       rpc_port_spin_);
        f->addRow(QStringLiteral("视频端口:"),       video_port_spin_);
        f->addRow(QStringLiteral("robotd 监听:"),    robotd_port_spin_);
        f->addRow(QStringLiteral("遥测端口:"),       telemetry_port_spin_);
        v->addWidget(sub);
    }
    // Sub C: 连接策略
    {
        auto *sub = makeSub(grp, QStringLiteral("连接策略"), ACC);
        auto *f = innerForm(sub);
        reconnect_timeout_spin_ = mkInt(sub, 500, 60000, 5000, "ms");
        retry_count_spin_       = mkInt(sub, 0, 100, 5);
        heartbeat_spin_         = mkInt(sub, 100, 60000, 1000, "ms");
        keepalive_combo_        = new QComboBox(sub);
        keepalive_combo_->addItems({QStringLiteral("启用 (TCP_KEEPALIVE)"),
                                    QStringLiteral("禁用")});
        f->addRow(QStringLiteral("重连超时:"), reconnect_timeout_spin_);
        f->addRow(QStringLiteral("重试次数:"), retry_count_spin_);
        f->addRow(QStringLiteral("心跳周期:"), heartbeat_spin_);
        f->addRow(QStringLiteral("保活机制:"), keepalive_combo_);
        v->addWidget(sub);
    }

    auto *bar = new QHBoxLayout;
    bar->addStretch();
    auto *btn = makeApply(grp);
    bar->addWidget(btn);
    v->addLayout(bar);
    connect(btn, &QPushButton::clicked, this, &Tab2CommConfig::onApplyHost);
    return grp;
}

// ════════════════════════════════════════════════════════════════════════
// MODULE 2: ARM (Piper gen-2 / legacy gen-1)
// ════════════════════════════════════════════════════════════════════════
QWidget *Tab2CommConfig::buildArmBox()
{
    const QColor ACC(0x6e, 0xff, 0xc0);
    auto *grp = new QGroupBox(QStringLiteral("◆  机械臂"), this);
    styleOuter(grp, ACC);
    auto *v = new QVBoxLayout(grp);
    v->setContentsMargins(10, 18, 10, 10);
    v->setSpacing(8);

    // Sub A: 后端 & CAN
    {
        auto *sub = makeSub(grp, QStringLiteral("后端 & CAN 总线"), ACC);
        auto *f = innerForm(sub);
        arm_backend_combo_ = new QComboBox(sub);
        arm_backend_combo_->addItems({QStringLiteral("piper (gen-2)"),
                                      QStringLiteral("legacy (gen-1)")});
        arm_can_edit_         = new QLineEdit("can_piper", sub);
        arm_bitrate_spin_     = mkInt(sub, 100000, 1000000, 1000000, "bps");
        arm_fifo_spin_        = mkInt(sub, 8, 4096, 256);
        arm_ack_timeout_spin_ = mkInt(sub, 50, 5000, 200, "ms");
        f->addRow(QStringLiteral("后端:"),      arm_backend_combo_);
        f->addRow(QStringLiteral("CAN 接口:"),  arm_can_edit_);
        f->addRow(QStringLiteral("CAN 比特率:"), arm_bitrate_spin_);
        f->addRow(QStringLiteral("RX FIFO:"),   arm_fifo_spin_);
        f->addRow(QStringLiteral("ACK 超时:"),  arm_ack_timeout_spin_);
        v->addWidget(sub);
    }
    // Sub B: 运动参数
    {
        auto *sub = makeSub(grp, QStringLiteral("运动参数"), ACC);
        auto *f = innerForm(sub);
        arm_speed_spin_       = mkInt(sub, 0, 1000, 30);
        arm_acc_spin_         = mkInt(sub, 0, 5000, 20);
        arm_jerk_spin_        = mkInt(sub, 0, 10000, 800);
        arm_speed_scale_spin_ = mkInt(sub, 1, 200, 100, "%");
        arm_reached_to_spin_  = mkInt(sub, 100, 60000, 8000, "ms");
        f->addRow(QStringLiteral("速度档位:"),   arm_speed_spin_);
        f->addRow(QStringLiteral("加速度:"),     arm_acc_spin_);
        f->addRow(QStringLiteral("Jerk 限制:"),  arm_jerk_spin_);
        f->addRow(QStringLiteral("速度缩放:"),   arm_speed_scale_spin_);
        f->addRow(QStringLiteral("到位超时:"),   arm_reached_to_spin_);
        v->addWidget(sub);
    }
    // Sub C: 关节软限位
    {
        auto *sub = makeSub(grp, QStringLiteral("关节软限位 (J1 ~ J6)"), ACC);
        auto *gl = new QGridLayout(sub);
        gl->setContentsMargins(8, 14, 8, 6);
        gl->setHorizontalSpacing(4);
        gl->setVerticalSpacing(4);
        gl->addWidget(new QLabel(QStringLiteral("min°"), sub), 0, 1);
        gl->addWidget(new QLabel(QStringLiteral("max°"), sub), 0, 2);
        const int defs_min[6] = {-180, 0, -80, -30, -110, -30};
        const int defs_max[6] = { 160, 180, 83, 305, 110, 305};
        for (int i = 0; i < 6; ++i) {
            gl->addWidget(new QLabel(QStringLiteral("J%1").arg(i+1), sub), i+1, 0);
            arm_j_min_spin_[i] = mkInt(sub, -360, 360, defs_min[i], "°");
            arm_j_max_spin_[i] = mkInt(sub, -360, 360, defs_max[i], "°");
            arm_j_min_spin_[i]->setMinimumWidth(76);
            arm_j_max_spin_[i]->setMinimumWidth(76);
            gl->addWidget(arm_j_min_spin_[i], i+1, 1);
            gl->addWidget(arm_j_max_spin_[i], i+1, 2);
        }
        v->addWidget(sub);
    }
    // Sub D: 末端 TCP / 负载
    {
        auto *sub = makeSub(grp, QStringLiteral("末端 TCP & 负载"), ACC);
        auto *f = innerForm(sub);
        arm_tcp_x_spin_   = mkDouble(sub, -500, 500, 0.0, 1, 1.0, "mm");
        arm_tcp_y_spin_   = mkDouble(sub, -500, 500, 0.0, 1, 1.0, "mm");
        arm_tcp_z_spin_   = mkDouble(sub, -500, 500, 142.0, 1, 1.0, "mm");
        arm_payload_spin_ = mkDouble(sub, 0,    5,   0.30, 2, 0.05, "kg");
        f->addRow(QStringLiteral("TCP 偏移 X:"), arm_tcp_x_spin_);
        f->addRow(QStringLiteral("TCP 偏移 Y:"), arm_tcp_y_spin_);
        f->addRow(QStringLiteral("TCP 偏移 Z:"), arm_tcp_z_spin_);
        f->addRow(QStringLiteral("末端负载:"),   arm_payload_spin_);
        v->addWidget(sub);
    }

    auto *bar = new QHBoxLayout;
    bar->addStretch();
    auto *btn = makeApply(grp);
    bar->addWidget(btn);
    v->addLayout(bar);
    connect(btn, &QPushButton::clicked, this, &Tab2CommConfig::onApplyArm);
    return grp;
}

// ════════════════════════════════════════════════════════════════════════
// MODULE 3: CAR CHASSIS
// ════════════════════════════════════════════════════════════════════════
QWidget *Tab2CommConfig::buildCarBox()
{
    const QColor ACC(0xff, 0xc8, 0x6e);
    auto *grp = new QGroupBox(QStringLiteral("◆  小车底盘"), this);
    styleOuter(grp, ACC);
    auto *v = new QVBoxLayout(grp);
    v->setContentsMargins(10, 18, 10, 10);
    v->setSpacing(8);

    // Sub A: CAN
    {
        auto *sub = makeSub(grp, QStringLiteral("CAN 总线"), ACC);
        auto *f = innerForm(sub);
        car_can_edit_     = new QLineEdit("can3", sub);
        car_bitrate_spin_ = mkInt(sub, 100000, 1000000, 1000000, "bps");
        car_fifo_spin_    = mkInt(sub, 8, 4096, 128);
        f->addRow(QStringLiteral("CAN 接口:"),  car_can_edit_);
        f->addRow(QStringLiteral("CAN 比特率:"), car_bitrate_spin_);
        f->addRow(QStringLiteral("RX FIFO:"),   car_fifo_spin_);
        v->addWidget(sub);
    }
    // Sub B: 底盘几何
    {
        auto *sub = makeSub(grp, QStringLiteral("底盘几何"), ACC);
        auto *f = innerForm(sub);
        car_track_spin_      = mkInt(sub, 100, 3000, 600, "mm");
        car_wheel_diam_spin_ = mkInt(sub, 50, 500, 152, "mm");
        car_gear_ratio_spin_ = mkDouble(sub, 1, 200, 30.0, 2, 0.5);
        f->addRow(QStringLiteral("轨距:"),      car_track_spin_);
        f->addRow(QStringLiteral("轮直径:"),    car_wheel_diam_spin_);
        f->addRow(QStringLiteral("减速比:"),    car_gear_ratio_spin_);
        v->addWidget(sub);
    }
    // Sub C: 速度
    {
        auto *sub = makeSub(grp, QStringLiteral("速度参数"), ACC);
        auto *f = innerForm(sub);
        car_min_rpm_spin_      = mkInt(sub, 0, 5000, 60);
        car_turn_min_rpm_spin_ = mkInt(sub, 0, 5000, 90);
        car_max_rpm_spin_      = mkInt(sub, 0, 5000, 1500);
        car_mmps_per_rpm_spin_ = mkDouble(sub, 0.1, 50, 5.0, 2, 0.1, "mm/s/rpm");
        f->addRow(QStringLiteral("起步 RPM:"),  car_min_rpm_spin_);
        f->addRow(QStringLiteral("转向 RPM:"),  car_turn_min_rpm_spin_);
        f->addRow(QStringLiteral("最大 RPM:"),  car_max_rpm_spin_);
        f->addRow(QStringLiteral("速度系数:"),  car_mmps_per_rpm_spin_);
        v->addWidget(sub);
    }
    // Sub D: PWM + 修正 + 发送节拍
    {
        auto *sub = makeSub(grp, QStringLiteral("PWM / 修正 / 节拍"), ACC);
        auto *f = innerForm(sub);
        car_limit_pwm_spin_      = mkInt(sub, 0, 5000, 800);
        car_turn_limit_pwm_spin_ = mkInt(sub, 0, 5000, 1200);
        car_left_trim_spin_      = mkDouble(sub, 0.5, 1.5, 1.0, 3, 0.01);
        car_right_trim_spin_     = mkDouble(sub, 0.5, 1.5, 1.0, 3, 0.01);
        car_ramp_steps_spin_     = mkInt(sub, 1, 32, 4);
        car_ramp_dt_spin_        = mkInt(sub, 5, 500, 50, "ms");
        car_send_interval_spin_  = mkInt(sub, 10, 1000, 120, "ms");
        f->addRow(QStringLiteral("PWM 限制:"),       car_limit_pwm_spin_);
        f->addRow(QStringLiteral("转向 PWM 限制:"),  car_turn_limit_pwm_spin_);
        f->addRow(QStringLiteral("左轮系数:"),       car_left_trim_spin_);
        f->addRow(QStringLiteral("右轮系数:"),       car_right_trim_spin_);
        f->addRow(QStringLiteral("斜坡步数:"),       car_ramp_steps_spin_);
        f->addRow(QStringLiteral("斜坡步长:"),       car_ramp_dt_spin_);
        f->addRow(QStringLiteral("发送间隔:"),       car_send_interval_spin_);
        v->addWidget(sub);
    }

    auto *bar = new QHBoxLayout;
    bar->addStretch();
    auto *btn = makeApply(grp);
    bar->addWidget(btn);
    v->addLayout(bar);
    connect(btn, &QPushButton::clicked, this, &Tab2CommConfig::onApplyCar);
    return grp;
}

// ════════════════════════════════════════════════════════════════════════
// MODULE 4: AIRPORT RAILS
// ════════════════════════════════════════════════════════════════════════
QWidget *Tab2CommConfig::buildAirportRailBox()
{
    const QColor ACC(0xff, 0x9a, 0xc8);
    auto *grp = new QGroupBox(QStringLiteral("◆  机场导轨"), this);
    styleOuter(grp, ACC);
    auto *v = new QVBoxLayout(grp);
    v->setContentsMargins(10, 18, 10, 10);
    v->setSpacing(8);

    // Sub A: CAN + 地址
    {
        auto *sub = makeSub(grp, QStringLiteral("CAN 总线 & 节点地址"), ACC);
        auto *f = innerForm(sub);
        ap_can_edit_     = new QLineEdit("can1", sub);
        ap_bitrate_spin_ = mkInt(sub, 100000, 1000000, 500000, "bps");
        ap_rail1_addr_spin_ = mkInt(sub, 1, 254, 1);
        ap_rail2_addr_spin_ = mkInt(sub, 1, 254, 2);
        ap_rail3_addr_spin_ = mkInt(sub, 1, 254, 3);
        f->addRow(QStringLiteral("CAN 接口:"),    ap_can_edit_);
        f->addRow(QStringLiteral("CAN 比特率:"),  ap_bitrate_spin_);
        f->addRow(QStringLiteral("导轨 1 地址:"), ap_rail1_addr_spin_);
        f->addRow(QStringLiteral("导轨 2 地址:"), ap_rail2_addr_spin_);
        f->addRow(QStringLiteral("导轨 3 地址:"), ap_rail3_addr_spin_);
        v->addWidget(sub);
    }
    // Sub B: 运动 + 编码器
    {
        auto *sub = makeSub(grp, QStringLiteral("运动 & 编码器"), ACC);
        auto *f = innerForm(sub);
        ap_rail_rpm_spin_      = mkInt(sub, 0, 5000, 1500);
        ap_rail_acc_spin_      = mkInt(sub, 0, 5000, 0);
        ap_pulses_per_mm_spin_ = mkInt(sub, 1, 10000, 100);
        ap_home_offset_spin_   = mkInt(sub, -50, 50, 0, "mm");
        ap_home_dir_combo_     = new QComboBox(sub);
        ap_home_dir_combo_->addItems({QStringLiteral("CW (0)"),
                                      QStringLiteral("CCW (1)")});
        ap_home_dir_combo_->setCurrentIndex(1);
        f->addRow(QStringLiteral("默认 RPM:"),    ap_rail_rpm_spin_);
        f->addRow(QStringLiteral("加速段:"),      ap_rail_acc_spin_);
        f->addRow(QStringLiteral("脉冲/毫米:"),   ap_pulses_per_mm_spin_);
        f->addRow(QStringLiteral("回零偏移:"),    ap_home_offset_spin_);
        f->addRow(QStringLiteral("回零方向:"),    ap_home_dir_combo_);
        v->addWidget(sub);
    }
    // Sub C: 软限位
    {
        auto *sub = makeSub(grp, QStringLiteral("软限位 (mm)"), ACC);
        auto *gl = new QGridLayout(sub);
        gl->setContentsMargins(8, 14, 8, 6);
        gl->setHorizontalSpacing(4);
        gl->setVerticalSpacing(4);
        gl->addWidget(new QLabel(QStringLiteral("min"), sub), 0, 1);
        gl->addWidget(new QLabel(QStringLiteral("max"), sub), 0, 2);
        const int mn[3] = {0,   0,   0};
        const int mx[3] = {300, 300, 300};
        for (int i = 0; i < 3; ++i) {
            gl->addWidget(new QLabel(QStringLiteral("导轨 %1").arg(i+1), sub), i+1, 0);
            ap_rail_min_spin_[i] = mkInt(sub, -1000, 1000, mn[i], "mm");
            ap_rail_max_spin_[i] = mkInt(sub, -1000, 1000, mx[i], "mm");
            gl->addWidget(ap_rail_min_spin_[i], i+1, 1);
            gl->addWidget(ap_rail_max_spin_[i], i+1, 2);
        }
        v->addWidget(sub);
    }

    auto *bar = new QHBoxLayout;
    bar->addStretch();
    auto *btn = makeApply(grp);
    bar->addWidget(btn);
    v->addLayout(bar);
    connect(btn, &QPushButton::clicked, this, &Tab2CommConfig::onApplyAirportRail);
    return grp;
}

// ════════════════════════════════════════════════════════════════════════
// MODULE 5: AIRPORT RELAYS
// ════════════════════════════════════════════════════════════════════════
QWidget *Tab2CommConfig::buildAirportRelayBox()
{
    const QColor ACC(0xff, 0x7a, 0x6e);
    auto *grp = new QGroupBox(QStringLiteral("◆  机场继电器"), this);
    styleOuter(grp, ACC);
    auto *v = new QVBoxLayout(grp);
    v->setContentsMargins(10, 18, 10, 10);
    v->setSpacing(8);

    // Sub A: GPIO 路径
    {
        auto *sub = makeSub(grp, QStringLiteral("GPIO 路径"), ACC);
        auto *f = innerForm(sub);
        ap_relay1_path_edit_   = new QLineEdit("GPIO_1", sub);
        ap_relay2_path_edit_   = new QLineEdit("GPIO_2", sub);
        ap_relay3_path_edit_   = new QLineEdit("GPIO_3", sub);
        ap_relay4_path_edit_   = new QLineEdit("IO4",    sub);
        ap_gripper_relay_edit_ = new QLineEdit("IO4",    sub);
        f->addRow(QStringLiteral("继电器 1:"),  ap_relay1_path_edit_);
        f->addRow(QStringLiteral("继电器 2:"),  ap_relay2_path_edit_);
        f->addRow(QStringLiteral("继电器 3:"),  ap_relay3_path_edit_);
        f->addRow(QStringLiteral("继电器 4:"),  ap_relay4_path_edit_);
        f->addRow(QStringLiteral("夹爪继电器:"), ap_gripper_relay_edit_);
        v->addWidget(sub);
    }
    // Sub B: 电平 & 模式
    {
        auto *sub = makeSub(grp, QStringLiteral("电平 & 驱动模式"), ACC);
        auto *f = innerForm(sub);
        ap_relay_active_combo_ = new QComboBox(sub);
        ap_relay_active_combo_->addItems({QStringLiteral("高电平有效"),
                                          QStringLiteral("低电平有效")});
        ap_gpio_mode_combo_    = new QComboBox(sub);
        ap_gpio_mode_combo_->addItems({QStringLiteral("推挽 push-pull"),
                                       QStringLiteral("开漏 open-drain")});
        f->addRow(QStringLiteral("有效电平:"), ap_relay_active_combo_);
        f->addRow(QStringLiteral("输出模式:"), ap_gpio_mode_combo_);
        v->addWidget(sub);
    }
    // Sub C: 时序 & 安全
    {
        auto *sub = makeSub(grp, QStringLiteral("时序 & 安全"), ACC);
        auto *f = innerForm(sub);
        ap_pulse_width_spin_ = mkInt(sub, 5, 5000, 200, "ms");
        ap_debounce_spin_    = mkInt(sub, 0, 1000, 20, "ms");
        ap_watchdog_spin_    = mkInt(sub, 0, 60000, 0, "ms");
        ap_estop_combo_      = new QComboBox(sub);
        ap_estop_combo_->addItems({QStringLiteral("急停时全部断开"),
                                   QStringLiteral("急停时保持"),
                                   QStringLiteral("急停时锁存上次状态")});
        f->addRow(QStringLiteral("脉冲宽度:"), ap_pulse_width_spin_);
        f->addRow(QStringLiteral("去抖时间:"), ap_debounce_spin_);
        f->addRow(QStringLiteral("看门狗:"),   ap_watchdog_spin_);
        f->addRow(QStringLiteral("急停联锁:"), ap_estop_combo_);
        v->addWidget(sub);
    }

    auto *bar = new QHBoxLayout;
    bar->addStretch();
    auto *btn = makeApply(grp);
    bar->addWidget(btn);
    v->addLayout(bar);
    connect(btn, &QPushButton::clicked, this, &Tab2CommConfig::onApplyAirportRelay);
    return grp;
}

// ════════════════════════════════════════════════════════════════════════
// MODULE 6: REALSENSE
// ════════════════════════════════════════════════════════════════════════
QWidget *Tab2CommConfig::buildRealsenseBox()
{
    const QColor ACC(0x6e, 0xd8, 0xff);
    auto *grp = new QGroupBox(QStringLiteral("◆  RealSense 相机"), this);
    styleOuter(grp, ACC);
    auto *v = new QVBoxLayout(grp);
    v->setContentsMargins(10, 18, 10, 10);
    v->setSpacing(8);

    // Sub A: 彩色流
    {
        auto *sub = makeSub(grp, QStringLiteral("彩色流"), ACC);
        auto *f = innerForm(sub);
        rs_width_spin_  = mkInt(sub, 160, 4096, 424);
        rs_height_spin_ = mkInt(sub, 120, 4096, 240);
        rs_fps_spin_    = mkInt(sub, 1, 120, 15, "fps");
        rs_color_format_combo_ = new QComboBox(sub);
        rs_color_format_combo_->addItems({"BGR8", "RGB8", "YUYV", "MJPEG"});
        f->addRow(QStringLiteral("宽度:"),    rs_width_spin_);
        f->addRow(QStringLiteral("高度:"),    rs_height_spin_);
        f->addRow(QStringLiteral("帧率:"),    rs_fps_spin_);
        f->addRow(QStringLiteral("像素格式:"), rs_color_format_combo_);
        v->addWidget(sub);
    }
    // Sub B: 深度流
    {
        auto *sub = makeSub(grp, QStringLiteral("深度流"), ACC);
        auto *f = innerForm(sub);
        rs_depth_w_spin_   = mkInt(sub, 160, 4096, 424);
        rs_depth_h_spin_   = mkInt(sub, 120, 4096, 240);
        rs_depth_fps_spin_ = mkInt(sub, 1, 120, 15, "fps");
        rs_depth_combo_    = new QComboBox(sub);
        rs_depth_combo_->addItems({QStringLiteral("启用"), QStringLiteral("关闭")});
        rs_laser_power_spin_ = mkInt(sub, 0, 360, 150, "mW");
        f->addRow(QStringLiteral("宽度:"),    rs_depth_w_spin_);
        f->addRow(QStringLiteral("高度:"),    rs_depth_h_spin_);
        f->addRow(QStringLiteral("帧率:"),    rs_depth_fps_spin_);
        f->addRow(QStringLiteral("启用状态:"), rs_depth_combo_);
        f->addRow(QStringLiteral("激光功率:"), rs_laser_power_spin_);
        v->addWidget(sub);
    }
    // Sub C: 图像参数
    {
        auto *sub = makeSub(grp, QStringLiteral("图像参数"), ACC);
        auto *f = innerForm(sub);
        rs_exposure_auto_combo_ = new QComboBox(sub);
        rs_exposure_auto_combo_->addItems({QStringLiteral("自动 AE"),
                                           QStringLiteral("手动 manual")});
        rs_exposure_us_spin_ = mkInt(sub, 10, 50000, 8000, "µs");
        rs_gain_spin_        = mkInt(sub, 0, 248, 64);
        rs_wb_combo_         = new QComboBox(sub);
        rs_wb_combo_->addItems({QStringLiteral("自动白平衡"),
                                QStringLiteral("日光 5500K"),
                                QStringLiteral("室内 3200K")});
        f->addRow(QStringLiteral("曝光模式:"), rs_exposure_auto_combo_);
        f->addRow(QStringLiteral("曝光时间:"), rs_exposure_us_spin_);
        f->addRow(QStringLiteral("增益:"),    rs_gain_spin_);
        f->addRow(QStringLiteral("白平衡:"),   rs_wb_combo_);
        v->addWidget(sub);
    }
    // Sub D: 深度后处理 + 对齐
    {
        auto *sub = makeSub(grp, QStringLiteral("深度后处理 & 对齐"), ACC);
        auto *f = innerForm(sub);
        rs_decimation_spin_ = mkInt(sub, 1, 8, 2);
        rs_temporal_spin_   = mkInt(sub, 0, 4, 1);
        rs_spatial_spin_    = mkInt(sub, 0, 5, 2);
        rs_align_combo_     = new QComboBox(sub);
        rs_align_combo_->addItems({QStringLiteral("Color → Depth"),
                                   QStringLiteral("Depth → Color"),
                                   QStringLiteral("不对齐")});
        rs_align_combo_->setCurrentIndex(1);
        f->addRow(QStringLiteral("空间下采样:"), rs_decimation_spin_);
        f->addRow(QStringLiteral("时间滤波:"),   rs_temporal_spin_);
        f->addRow(QStringLiteral("空间滤波:"),   rs_spatial_spin_);
        f->addRow(QStringLiteral("流对齐:"),     rs_align_combo_);
        v->addWidget(sub);
    }

    auto *bar = new QHBoxLayout;
    bar->addStretch();
    auto *btn = makeApply(grp);
    bar->addWidget(btn);
    v->addLayout(bar);
    connect(btn, &QPushButton::clicked, this, &Tab2CommConfig::onApplyRealsense);
    return grp;
}

// ════════════════════════════════════════════════════════════════════════
// MODULE 7: NPU
// ════════════════════════════════════════════════════════════════════════
QWidget *Tab2CommConfig::buildNpuBox()
{
    const QColor ACC(0xc8, 0x9a, 0xff);
    auto *grp = new QGroupBox(QStringLiteral("◆  NPU 推理"), this);
    styleOuter(grp, ACC);
    auto *v = new QVBoxLayout(grp);
    v->setContentsMargins(10, 18, 10, 10);
    v->setSpacing(8);

    // Sub A: 模型 & 策略
    {
        auto *sub = makeSub(grp, QStringLiteral("模型 & 策略"), ACC);
        auto *f = innerForm(sub);
        npu_strategy_combo_ = new QComboBox(sub);
        npu_strategy_combo_->addItem(QStringLiteral("0 — 关闭"),         0);
        npu_strategy_combo_->addItem(QStringLiteral("1 — 通用 YOLOv8"),  1);
        npu_strategy_combo_->addItem(QStringLiteral("2 — 人脸"),          2);
        npu_strategy_combo_->addItem(QStringLiteral("3 — 电池"),          3);
        npu_strategy_combo_->addItem(QStringLiteral("4 — 二维码"),        4);
        npu_strategy_combo_->addItem(QStringLiteral("5 — Mavic3 无人机"), 5);
        npu_strategy_combo_->setCurrentIndex(5);
        npu_model_path_edit_ = new QLineEdit("/opt/uav_robot/mavic3_drone.rknn", sub);
        npu_core_combo_      = new QComboBox(sub);
        npu_core_combo_->addItems({QStringLiteral("自动 (auto)"),
                                   QStringLiteral("CORE_0"),
                                   QStringLiteral("CORE_1"),
                                   QStringLiteral("CORE_2"),
                                   QStringLiteral("CORE_0_1_2 (3 路并发)")});
        npu_core_combo_->setCurrentIndex(0);
        f->addRow(QStringLiteral("当前策略:"), npu_strategy_combo_);
        f->addRow(QStringLiteral("模型路径:"), npu_model_path_edit_);
        f->addRow(QStringLiteral("NPU 核:"),   npu_core_combo_);
        v->addWidget(sub);
    }
    // Sub B: 推理参数
    {
        auto *sub = makeSub(grp, QStringLiteral("推理参数"), ACC);
        auto *f = innerForm(sub);
        npu_threshold_spin_  = mkDouble(sub, 0.0, 1.0, 0.50, 2, 0.05);
        npu_nms_thresh_spin_ = mkDouble(sub, 0.0, 1.0, 0.45, 2, 0.05);
        npu_input_size_spin_ = mkInt(sub, 64, 1280, 640);
        npu_max_boxes_spin_  = mkInt(sub, 1, 1000, 100);
        f->addRow(QStringLiteral("置信阈值:"), npu_threshold_spin_);
        f->addRow(QStringLiteral("NMS 阈值:"), npu_nms_thresh_spin_);
        f->addRow(QStringLiteral("输入尺寸:"), npu_input_size_spin_);
        f->addRow(QStringLiteral("最大框数:"), npu_max_boxes_spin_);
        v->addWidget(sub);
    }
    // Sub C: 后处理 / 过滤
    {
        auto *sub = makeSub(grp, QStringLiteral("后处理 & 过滤"), ACC);
        auto *f = innerForm(sub);
        npu_top_k_spin_ = mkInt(sub, 1, 100, 10);
        npu_class_filter_edit_ = new QLineEdit("drone,battery,qrcode", sub);
        npu_post_decode_combo_ = new QComboBox(sub);
        npu_post_decode_combo_->addItems({QStringLiteral("kaylorchen (9-output)"),
                                          QStringLiteral("airockchip (3-output)"),
                                          QStringLiteral("自动检测")});
        f->addRow(QStringLiteral("Top-K:"),    npu_top_k_spin_);
        f->addRow(QStringLiteral("类别过滤:"), npu_class_filter_edit_);
        f->addRow(QStringLiteral("解码方案:"), npu_post_decode_combo_);
        v->addWidget(sub);
    }

    auto *bar = new QHBoxLayout;
    bar->addStretch();
    auto *btn = makeApply(grp);
    bar->addWidget(btn);
    v->addLayout(bar);
    connect(btn, &QPushButton::clicked, this, &Tab2CommConfig::onApplyNpu);
    return grp;
}

// ════════════════════════════════════════════════════════════════════════
// MODULE 8: GRIPPER UART (legacy)
// ════════════════════════════════════════════════════════════════════════
QWidget *Tab2CommConfig::buildGripperUartBox()
{
    const QColor ACC(0x88, 0x90, 0xa8);
    auto *grp = new QGroupBox(QStringLiteral("◆  夹爪 UART (legacy)"), this);
    styleOuter(grp, ACC);
    auto *v = new QVBoxLayout(grp);
    v->setContentsMargins(10, 18, 10, 10);
    v->setSpacing(8);

    // Sub A: 串口
    {
        auto *sub = makeSub(grp, QStringLiteral("串口"), ACC);
        auto *f = innerForm(sub);
        gripper_uart_edit_       = new QLineEdit("/dev/ttyUSB0", sub);
        gripper_uart_baud_combo_ = new QComboBox(sub);
        gripper_uart_baud_combo_->addItems({"9600", "19200", "38400", "57600",
                                            "115200", "230400", "460800", "921600"});
        gripper_uart_baud_combo_->setCurrentText("115200");
        f->addRow(QStringLiteral("设备:"),   gripper_uart_edit_);
        f->addRow(QStringLiteral("波特率:"), gripper_uart_baud_combo_);
        v->addWidget(sub);
    }
    // Sub B: 帧格式
    {
        auto *sub = makeSub(grp, QStringLiteral("帧格式"), ACC);
        auto *f = innerForm(sub);
        gripper_uart_databits_combo_ = new QComboBox(sub);
        gripper_uart_databits_combo_->addItems({"5", "6", "7", "8"});
        gripper_uart_databits_combo_->setCurrentText("8");
        gripper_uart_stopbits_combo_ = new QComboBox(sub);
        gripper_uart_stopbits_combo_->addItems({"1", "1.5", "2"});
        gripper_uart_parity_combo_   = new QComboBox(sub);
        gripper_uart_parity_combo_->addItems({QStringLiteral("无 None"),
                                              QStringLiteral("偶 Even"),
                                              QStringLiteral("奇 Odd")});
        f->addRow(QStringLiteral("数据位:"), gripper_uart_databits_combo_);
        f->addRow(QStringLiteral("停止位:"), gripper_uart_stopbits_combo_);
        f->addRow(QStringLiteral("校验位:"), gripper_uart_parity_combo_);
        v->addWidget(sub);
    }
    // Sub C: 流控 & 超时
    {
        auto *sub = makeSub(grp, QStringLiteral("流控 & 超时"), ACC);
        auto *f = innerForm(sub);
        gripper_uart_flow_combo_  = new QComboBox(sub);
        gripper_uart_flow_combo_->addItems({QStringLiteral("无"),
                                            QStringLiteral("硬件 RTS/CTS"),
                                            QStringLiteral("软件 XON/XOFF")});
        gripper_uart_timeout_spin_ = mkInt(sub, 10, 5000, 100, "ms");
        gripper_uart_retry_spin_   = mkInt(sub, 0, 10, 2);
        f->addRow(QStringLiteral("流控:"),   gripper_uart_flow_combo_);
        f->addRow(QStringLiteral("响应超时:"), gripper_uart_timeout_spin_);
        f->addRow(QStringLiteral("重试次数:"), gripper_uart_retry_spin_);
        v->addWidget(sub);
    }

    // Note label about legacy
    auto *legacy_note = new QLabel(
        QStringLiteral("⚠ 仅 backend=legacy 时使用 — piper 后端夹爪走 CAN 总线。"),
        grp);
    legacy_note->setStyleSheet("color:#9aa0b8; font-size:11px;");
    v->addWidget(legacy_note);

    auto *bar = new QHBoxLayout;
    bar->addStretch();
    auto *btn = makeApply(grp);
    bar->addWidget(btn);
    v->addLayout(bar);
    connect(btn, &QPushButton::clicked, this, &Tab2CommConfig::onApplyGripperUart);
    return grp;
}

// ════════════════════════════════════════════════════════════════════════
// Apply slots (push primary fields via existing RPC config methods)
// ════════════════════════════════════════════════════════════════════════
void Tab2CommConfig::setConnectionParams(const QString &host, quint16 rpcPort, quint16 videoPort)
{
    if (host_edit_)       host_edit_->setText(host);
    if (rpc_port_spin_)   rpc_port_spin_->setValue(rpcPort);
    if (video_port_spin_) video_port_spin_->setValue(videoPort);
}

void Tab2CommConfig::onToggleLock()
{
    params_locked_ = !params_locked_;
    applyLockState();
}

void Tab2CommConfig::applyLockState()
{
    // Toggle each module group's enabled state. setEnabled() recurses into
    // children, so every QLineEdit / QSpinBox / QComboBox / QPushButton
    // inside the 8 modules follows automatically.
    for (QWidget *g : module_groups_) {
        if (g) g->setEnabled(!params_locked_);
    }

    if (lock_button_) {
        if (params_locked_) {
            lock_button_->setText(QStringLiteral("🔒  已锁定 — 点击解锁"));
            lock_button_->setStyleSheet(
                "QPushButton {"
                " background:#2d3850; color:#ffd06e; border:1px solid #ffd06e;"
                " border-radius:5px; font-weight:bold; padding:4px 14px;"
                "}"
                "QPushButton:hover  { background:#384563; }"
                "QPushButton:pressed{ background:#1f2a40; }");
        } else {
            lock_button_->setText(QStringLiteral("🔓  已解锁 — 点击锁定"));
            lock_button_->setStyleSheet(
                "QPushButton {"
                " background:#1e3a2c; color:#7af0a0; border:1px solid #7af0a0;"
                " border-radius:5px; font-weight:bold; padding:4px 14px;"
                "}"
                "QPushButton:hover  { background:#2a5040; }"
                "QPushButton:pressed{ background:#152920; }");
        }
    }
    if (lock_note_) {
        lock_note_->setText(params_locked_
                            ? QStringLiteral("参数不可编辑")
                            : QStringLiteral("⚠ 解锁中,改完记得锁回"));
        lock_note_->setStyleSheet(params_locked_
            ? "color:#9aa0b8; font-size:10px;"
            : "color:#ffb37a; font-size:10px; font-weight:bold;");
    }
}

void Tab2CommConfig::onApplyHost()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject p;
    p[Protocol::Fields::HOST]         = host_edit_->text();
    p[Protocol::Fields::RPC_PORT_F]   = rpc_port_spin_->value();
    p[Protocol::Fields::VIDEO_PORT_F] = video_port_spin_->value();
    rpc_->call(Protocol::Methods::CONFIG_SET_ETHERNET, p);
}

void Tab2CommConfig::onApplyArm()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    {
        QJsonObject p;
        p[Protocol::Fields::DEVICE]  = arm_can_edit_->text();
        p[Protocol::Fields::BITRATE] = arm_bitrate_spin_->value();
        rpc_->call(Protocol::Methods::CONFIG_SET_CAN, p);
    }
    {
        QJsonObject p;
        QJsonArray spds;
        const int v = arm_speed_spin_->value();
        for (int i = 0; i < 6; ++i) spds.append(v);
        p["speeds"] = spds;
        rpc_->call(Protocol::Methods::ARM_SET_SPEEDS, p);
    }
}

void Tab2CommConfig::onApplyCar()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject p;
    p[Protocol::Fields::DEVICE]  = car_can_edit_->text();
    p[Protocol::Fields::BITRATE] = car_bitrate_spin_->value();
    rpc_->call(Protocol::Methods::CONFIG_SET_CAN, p);
}

void Tab2CommConfig::onApplyAirportRail()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject p;
    p[Protocol::Fields::DEVICE]  = ap_can_edit_->text();
    p[Protocol::Fields::BITRATE] = ap_bitrate_spin_->value();
    rpc_->call(Protocol::Methods::CONFIG_SET_CAN, p);
    QJsonObject sp;
    sp[Protocol::Fields::SPEED_RPM] = ap_rail_rpm_spin_->value();
    rpc_->call(Protocol::Methods::AIRPORT_SET_SPEED, sp);
}

void Tab2CommConfig::onApplyAirportRelay()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject p;
    p[Protocol::Fields::ACTIVE_LOW] = (ap_relay_active_combo_->currentIndex() == 1);
    p["relay1_path"] = ap_relay1_path_edit_->text();
    p["relay2_path"] = ap_relay2_path_edit_->text();
    p["relay3_path"] = ap_relay3_path_edit_->text();
    p["relay4_path"] = ap_relay4_path_edit_->text();
    p["gripper_relay_path"] = ap_gripper_relay_edit_->text();
    rpc_->call(Protocol::Methods::CONFIG_SET_RELAY, p);
}

void Tab2CommConfig::onApplyRealsense()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject p;
    p[Protocol::Fields::WIDTH]  = rs_width_spin_->value();
    p[Protocol::Fields::HEIGHT] = rs_height_spin_->value();
    p[Protocol::Fields::FPS]    = rs_fps_spin_->value();
    p["depth_enabled"] = (rs_depth_combo_->currentIndex() == 0);
    rpc_->call(Protocol::Methods::CAMERA_SET_PROFILE, p);
}

void Tab2CommConfig::onApplyNpu()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    {
        QJsonObject p;
        p[Protocol::Fields::STRATEGY_ID] = npu_strategy_combo_->currentData().toInt();
        rpc_->call(Protocol::Methods::NPU_SET_STRATEGY, p);
    }
    {
        QJsonObject p;
        p[Protocol::Fields::THRESHOLD] = npu_threshold_spin_->value();
        rpc_->call(Protocol::Methods::NPU_SET_THRESHOLD, p);
    }
}

void Tab2CommConfig::onApplyGripperUart()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject p;
    p[Protocol::Fields::DEVICE]    = gripper_uart_edit_->text();
    p[Protocol::Fields::BAUD_RATE] = gripper_uart_baud_combo_->currentText().toInt();
    rpc_->call(Protocol::Methods::CONFIG_SET_SERIAL, p);
}

// ════════════════════════════════════════════════════════════════════════
// Persistence
// ════════════════════════════════════════════════════════════════════════
void Tab2CommConfig::loadConfig(QSettings &s)
{
    s.beginGroup("Tab2CommConfig");

    // Host
    if (host_edit_)             host_edit_->setText(s.value("host", host_edit_->text()).toString());
    if (subnet_edit_)           subnet_edit_->setText(s.value("subnet", subnet_edit_->text()).toString());
    if (gateway_edit_)          gateway_edit_->setText(s.value("gateway", gateway_edit_->text()).toString());
    if (mac_edit_)              mac_edit_->setText(s.value("mac", mac_edit_->text()).toString());
    if (eth_iface_combo_)       eth_iface_combo_->setCurrentIndex(s.value("eth_iface_idx", 0).toInt());
    if (mtu_spin_)              mtu_spin_->setValue(s.value("mtu", mtu_spin_->value()).toInt());
    if (rpc_port_spin_)         rpc_port_spin_->setValue(s.value("rpc_port", rpc_port_spin_->value()).toInt());
    if (video_port_spin_)       video_port_spin_->setValue(s.value("video_port", video_port_spin_->value()).toInt());
    if (robotd_port_spin_)      robotd_port_spin_->setValue(s.value("robotd_port", robotd_port_spin_->value()).toInt());
    if (telemetry_port_spin_)   telemetry_port_spin_->setValue(s.value("telemetry_port", telemetry_port_spin_->value()).toInt());
    if (reconnect_timeout_spin_) reconnect_timeout_spin_->setValue(s.value("reconnect_to", reconnect_timeout_spin_->value()).toInt());
    if (retry_count_spin_)      retry_count_spin_->setValue(s.value("retry_count", retry_count_spin_->value()).toInt());
    if (heartbeat_spin_)        heartbeat_spin_->setValue(s.value("heartbeat", heartbeat_spin_->value()).toInt());
    if (keepalive_combo_)       keepalive_combo_->setCurrentIndex(s.value("keepalive_idx", 0).toInt());

    // Arm
    if (arm_backend_combo_)     arm_backend_combo_->setCurrentIndex(s.value("arm_backend", 0).toInt());
    if (arm_can_edit_)          arm_can_edit_->setText(s.value("arm_can", arm_can_edit_->text()).toString());
    if (arm_bitrate_spin_)      arm_bitrate_spin_->setValue(s.value("arm_bitrate", arm_bitrate_spin_->value()).toInt());
    if (arm_fifo_spin_)         arm_fifo_spin_->setValue(s.value("arm_fifo", arm_fifo_spin_->value()).toInt());
    if (arm_ack_timeout_spin_)  arm_ack_timeout_spin_->setValue(s.value("arm_ack_to", arm_ack_timeout_spin_->value()).toInt());
    if (arm_speed_spin_)        arm_speed_spin_->setValue(s.value("arm_speed", arm_speed_spin_->value()).toInt());
    if (arm_acc_spin_)          arm_acc_spin_->setValue(s.value("arm_acc", arm_acc_spin_->value()).toInt());
    if (arm_jerk_spin_)         arm_jerk_spin_->setValue(s.value("arm_jerk", arm_jerk_spin_->value()).toInt());
    if (arm_speed_scale_spin_)  arm_speed_scale_spin_->setValue(s.value("arm_speed_scale", arm_speed_scale_spin_->value()).toInt());
    if (arm_reached_to_spin_)   arm_reached_to_spin_->setValue(s.value("arm_reached_to", arm_reached_to_spin_->value()).toInt());
    for (int i = 0; i < 6; ++i) {
        if (arm_j_min_spin_[i]) arm_j_min_spin_[i]->setValue(s.value(QString("arm_j%1_min").arg(i+1), arm_j_min_spin_[i]->value()).toInt());
        if (arm_j_max_spin_[i]) arm_j_max_spin_[i]->setValue(s.value(QString("arm_j%1_max").arg(i+1), arm_j_max_spin_[i]->value()).toInt());
    }
    if (arm_tcp_x_spin_)   arm_tcp_x_spin_->setValue(s.value("arm_tcp_x", arm_tcp_x_spin_->value()).toDouble());
    if (arm_tcp_y_spin_)   arm_tcp_y_spin_->setValue(s.value("arm_tcp_y", arm_tcp_y_spin_->value()).toDouble());
    if (arm_tcp_z_spin_)   arm_tcp_z_spin_->setValue(s.value("arm_tcp_z", arm_tcp_z_spin_->value()).toDouble());
    if (arm_payload_spin_) arm_payload_spin_->setValue(s.value("arm_payload", arm_payload_spin_->value()).toDouble());

    // Car
    if (car_can_edit_)          car_can_edit_->setText(s.value("car_can", car_can_edit_->text()).toString());
    if (car_bitrate_spin_)      car_bitrate_spin_->setValue(s.value("car_bitrate", car_bitrate_spin_->value()).toInt());
    if (car_fifo_spin_)         car_fifo_spin_->setValue(s.value("car_fifo", car_fifo_spin_->value()).toInt());
    if (car_track_spin_)        car_track_spin_->setValue(s.value("car_track", car_track_spin_->value()).toInt());
    if (car_wheel_diam_spin_)   car_wheel_diam_spin_->setValue(s.value("car_wheel", car_wheel_diam_spin_->value()).toInt());
    if (car_gear_ratio_spin_)   car_gear_ratio_spin_->setValue(s.value("car_gear", car_gear_ratio_spin_->value()).toDouble());
    if (car_min_rpm_spin_)      car_min_rpm_spin_->setValue(s.value("car_min_rpm", car_min_rpm_spin_->value()).toInt());
    if (car_turn_min_rpm_spin_) car_turn_min_rpm_spin_->setValue(s.value("car_turn_min_rpm", car_turn_min_rpm_spin_->value()).toInt());
    if (car_max_rpm_spin_)      car_max_rpm_spin_->setValue(s.value("car_max_rpm", car_max_rpm_spin_->value()).toInt());
    if (car_mmps_per_rpm_spin_) car_mmps_per_rpm_spin_->setValue(s.value("car_mmps", car_mmps_per_rpm_spin_->value()).toDouble());
    if (car_limit_pwm_spin_)    car_limit_pwm_spin_->setValue(s.value("car_pwm", car_limit_pwm_spin_->value()).toInt());
    if (car_turn_limit_pwm_spin_) car_turn_limit_pwm_spin_->setValue(s.value("car_turn_pwm", car_turn_limit_pwm_spin_->value()).toInt());
    if (car_left_trim_spin_)    car_left_trim_spin_->setValue(s.value("car_left_trim", car_left_trim_spin_->value()).toDouble());
    if (car_right_trim_spin_)   car_right_trim_spin_->setValue(s.value("car_right_trim", car_right_trim_spin_->value()).toDouble());
    if (car_ramp_steps_spin_)   car_ramp_steps_spin_->setValue(s.value("car_ramp_steps", car_ramp_steps_spin_->value()).toInt());
    if (car_ramp_dt_spin_)      car_ramp_dt_spin_->setValue(s.value("car_ramp_dt", car_ramp_dt_spin_->value()).toInt());
    if (car_send_interval_spin_) car_send_interval_spin_->setValue(s.value("car_send_int", car_send_interval_spin_->value()).toInt());

    // Airport rails
    if (ap_can_edit_)            ap_can_edit_->setText(s.value("ap_can", ap_can_edit_->text()).toString());
    if (ap_bitrate_spin_)        ap_bitrate_spin_->setValue(s.value("ap_bitrate", ap_bitrate_spin_->value()).toInt());
    if (ap_rail1_addr_spin_)     ap_rail1_addr_spin_->setValue(s.value("ap_r1_addr", ap_rail1_addr_spin_->value()).toInt());
    if (ap_rail2_addr_spin_)     ap_rail2_addr_spin_->setValue(s.value("ap_r2_addr", ap_rail2_addr_spin_->value()).toInt());
    if (ap_rail3_addr_spin_)     ap_rail3_addr_spin_->setValue(s.value("ap_r3_addr", ap_rail3_addr_spin_->value()).toInt());
    if (ap_rail_rpm_spin_)       ap_rail_rpm_spin_->setValue(s.value("ap_rpm", ap_rail_rpm_spin_->value()).toInt());
    if (ap_rail_acc_spin_)       ap_rail_acc_spin_->setValue(s.value("ap_acc", ap_rail_acc_spin_->value()).toInt());
    if (ap_pulses_per_mm_spin_)  ap_pulses_per_mm_spin_->setValue(s.value("ap_pulses_mm", ap_pulses_per_mm_spin_->value()).toInt());
    if (ap_home_offset_spin_)    ap_home_offset_spin_->setValue(s.value("ap_home_off", ap_home_offset_spin_->value()).toInt());
    if (ap_home_dir_combo_)      ap_home_dir_combo_->setCurrentIndex(s.value("ap_home_dir", 1).toInt());
    for (int i = 0; i < 3; ++i) {
        if (ap_rail_min_spin_[i]) ap_rail_min_spin_[i]->setValue(s.value(QString("ap_r%1_min").arg(i+1), ap_rail_min_spin_[i]->value()).toInt());
        if (ap_rail_max_spin_[i]) ap_rail_max_spin_[i]->setValue(s.value(QString("ap_r%1_max").arg(i+1), ap_rail_max_spin_[i]->value()).toInt());
    }

    // Airport relays
    if (ap_relay1_path_edit_)    ap_relay1_path_edit_->setText(s.value("ap_relay1", ap_relay1_path_edit_->text()).toString());
    if (ap_relay2_path_edit_)    ap_relay2_path_edit_->setText(s.value("ap_relay2", ap_relay2_path_edit_->text()).toString());
    if (ap_relay3_path_edit_)    ap_relay3_path_edit_->setText(s.value("ap_relay3", ap_relay3_path_edit_->text()).toString());
    if (ap_relay4_path_edit_)    ap_relay4_path_edit_->setText(s.value("ap_relay4", ap_relay4_path_edit_->text()).toString());
    if (ap_gripper_relay_edit_)  ap_gripper_relay_edit_->setText(s.value("ap_gripper_relay", ap_gripper_relay_edit_->text()).toString());
    if (ap_relay_active_combo_)  ap_relay_active_combo_->setCurrentIndex(s.value("ap_active_idx", 0).toInt());
    if (ap_gpio_mode_combo_)     ap_gpio_mode_combo_->setCurrentIndex(s.value("ap_gpio_mode", 0).toInt());
    if (ap_pulse_width_spin_)    ap_pulse_width_spin_->setValue(s.value("ap_pulse_w", ap_pulse_width_spin_->value()).toInt());
    if (ap_debounce_spin_)       ap_debounce_spin_->setValue(s.value("ap_debounce", ap_debounce_spin_->value()).toInt());
    if (ap_watchdog_spin_)       ap_watchdog_spin_->setValue(s.value("ap_watchdog", ap_watchdog_spin_->value()).toInt());
    if (ap_estop_combo_)         ap_estop_combo_->setCurrentIndex(s.value("ap_estop_idx", 0).toInt());

    // RealSense
    if (rs_width_spin_)         rs_width_spin_->setValue(s.value("rs_w", rs_width_spin_->value()).toInt());
    if (rs_height_spin_)        rs_height_spin_->setValue(s.value("rs_h", rs_height_spin_->value()).toInt());
    if (rs_fps_spin_)           rs_fps_spin_->setValue(s.value("rs_fps", rs_fps_spin_->value()).toInt());
    if (rs_color_format_combo_) rs_color_format_combo_->setCurrentIndex(s.value("rs_fmt", 0).toInt());
    if (rs_depth_w_spin_)       rs_depth_w_spin_->setValue(s.value("rs_dw", rs_depth_w_spin_->value()).toInt());
    if (rs_depth_h_spin_)       rs_depth_h_spin_->setValue(s.value("rs_dh", rs_depth_h_spin_->value()).toInt());
    if (rs_depth_fps_spin_)     rs_depth_fps_spin_->setValue(s.value("rs_dfps", rs_depth_fps_spin_->value()).toInt());
    if (rs_depth_combo_)        rs_depth_combo_->setCurrentIndex(s.value("rs_depth_idx", 0).toInt());
    if (rs_laser_power_spin_)   rs_laser_power_spin_->setValue(s.value("rs_laser", rs_laser_power_spin_->value()).toInt());
    if (rs_exposure_auto_combo_) rs_exposure_auto_combo_->setCurrentIndex(s.value("rs_exp_auto", 0).toInt());
    if (rs_exposure_us_spin_)   rs_exposure_us_spin_->setValue(s.value("rs_exp_us", rs_exposure_us_spin_->value()).toInt());
    if (rs_gain_spin_)          rs_gain_spin_->setValue(s.value("rs_gain", rs_gain_spin_->value()).toInt());
    if (rs_wb_combo_)           rs_wb_combo_->setCurrentIndex(s.value("rs_wb", 0).toInt());
    if (rs_decimation_spin_)    rs_decimation_spin_->setValue(s.value("rs_dec", rs_decimation_spin_->value()).toInt());
    if (rs_temporal_spin_)      rs_temporal_spin_->setValue(s.value("rs_tem", rs_temporal_spin_->value()).toInt());
    if (rs_spatial_spin_)       rs_spatial_spin_->setValue(s.value("rs_spa", rs_spatial_spin_->value()).toInt());
    if (rs_align_combo_)        rs_align_combo_->setCurrentIndex(s.value("rs_align", 1).toInt());

    // NPU
    if (npu_strategy_combo_) {
        int idx = s.value("npu_strategy_idx", npu_strategy_combo_->currentIndex()).toInt();
        if (idx >= 0 && idx < npu_strategy_combo_->count()) npu_strategy_combo_->setCurrentIndex(idx);
    }
    if (npu_model_path_edit_)  npu_model_path_edit_->setText(s.value("npu_model", npu_model_path_edit_->text()).toString());
    if (npu_core_combo_)       npu_core_combo_->setCurrentIndex(s.value("npu_core", 0).toInt());
    if (npu_threshold_spin_)   npu_threshold_spin_->setValue(s.value("npu_conf", npu_threshold_spin_->value()).toDouble());
    if (npu_nms_thresh_spin_)  npu_nms_thresh_spin_->setValue(s.value("npu_nms", npu_nms_thresh_spin_->value()).toDouble());
    if (npu_input_size_spin_)  npu_input_size_spin_->setValue(s.value("npu_in", npu_input_size_spin_->value()).toInt());
    if (npu_max_boxes_spin_)   npu_max_boxes_spin_->setValue(s.value("npu_max", npu_max_boxes_spin_->value()).toInt());
    if (npu_top_k_spin_)       npu_top_k_spin_->setValue(s.value("npu_topk", npu_top_k_spin_->value()).toInt());
    if (npu_class_filter_edit_) npu_class_filter_edit_->setText(s.value("npu_filter", npu_class_filter_edit_->text()).toString());
    if (npu_post_decode_combo_) npu_post_decode_combo_->setCurrentIndex(s.value("npu_decode", 0).toInt());

    // Gripper UART
    if (gripper_uart_edit_)         gripper_uart_edit_->setText(s.value("gripper_uart", gripper_uart_edit_->text()).toString());
    if (gripper_uart_baud_combo_) {
        const QString baud = s.value("gripper_uart_baud", gripper_uart_baud_combo_->currentText()).toString();
        const int idx = gripper_uart_baud_combo_->findText(baud);
        if (idx >= 0) gripper_uart_baud_combo_->setCurrentIndex(idx);
    }
    if (gripper_uart_databits_combo_) gripper_uart_databits_combo_->setCurrentIndex(s.value("g_uart_data", 3).toInt());
    if (gripper_uart_stopbits_combo_) gripper_uart_stopbits_combo_->setCurrentIndex(s.value("g_uart_stop", 0).toInt());
    if (gripper_uart_parity_combo_)   gripper_uart_parity_combo_->setCurrentIndex(s.value("g_uart_par", 0).toInt());
    if (gripper_uart_flow_combo_)     gripper_uart_flow_combo_->setCurrentIndex(s.value("g_uart_flow", 0).toInt());
    if (gripper_uart_timeout_spin_)   gripper_uart_timeout_spin_->setValue(s.value("g_uart_to", gripper_uart_timeout_spin_->value()).toInt());
    if (gripper_uart_retry_spin_)     gripper_uart_retry_spin_->setValue(s.value("g_uart_retry", gripper_uart_retry_spin_->value()).toInt());

    s.endGroup();
}

void Tab2CommConfig::saveConfig(QSettings &s) const
{
    s.beginGroup("Tab2CommConfig");

    s.setValue("host",           host_edit_->text());
    s.setValue("subnet",         subnet_edit_->text());
    s.setValue("gateway",        gateway_edit_->text());
    s.setValue("mac",            mac_edit_->text());
    s.setValue("eth_iface_idx",  eth_iface_combo_->currentIndex());
    s.setValue("mtu",            mtu_spin_->value());
    s.setValue("rpc_port",       rpc_port_spin_->value());
    s.setValue("video_port",     video_port_spin_->value());
    s.setValue("robotd_port",    robotd_port_spin_->value());
    s.setValue("telemetry_port", telemetry_port_spin_->value());
    s.setValue("reconnect_to",   reconnect_timeout_spin_->value());
    s.setValue("retry_count",    retry_count_spin_->value());
    s.setValue("heartbeat",      heartbeat_spin_->value());
    s.setValue("keepalive_idx",  keepalive_combo_->currentIndex());

    s.setValue("arm_backend",    arm_backend_combo_->currentIndex());
    s.setValue("arm_can",        arm_can_edit_->text());
    s.setValue("arm_bitrate",    arm_bitrate_spin_->value());
    s.setValue("arm_fifo",       arm_fifo_spin_->value());
    s.setValue("arm_ack_to",     arm_ack_timeout_spin_->value());
    s.setValue("arm_speed",      arm_speed_spin_->value());
    s.setValue("arm_acc",        arm_acc_spin_->value());
    s.setValue("arm_jerk",       arm_jerk_spin_->value());
    s.setValue("arm_speed_scale", arm_speed_scale_spin_->value());
    s.setValue("arm_reached_to", arm_reached_to_spin_->value());
    for (int i = 0; i < 6; ++i) {
        s.setValue(QString("arm_j%1_min").arg(i+1), arm_j_min_spin_[i]->value());
        s.setValue(QString("arm_j%1_max").arg(i+1), arm_j_max_spin_[i]->value());
    }
    s.setValue("arm_tcp_x",     arm_tcp_x_spin_->value());
    s.setValue("arm_tcp_y",     arm_tcp_y_spin_->value());
    s.setValue("arm_tcp_z",     arm_tcp_z_spin_->value());
    s.setValue("arm_payload",   arm_payload_spin_->value());

    s.setValue("car_can",       car_can_edit_->text());
    s.setValue("car_bitrate",   car_bitrate_spin_->value());
    s.setValue("car_fifo",      car_fifo_spin_->value());
    s.setValue("car_track",     car_track_spin_->value());
    s.setValue("car_wheel",     car_wheel_diam_spin_->value());
    s.setValue("car_gear",      car_gear_ratio_spin_->value());
    s.setValue("car_min_rpm",   car_min_rpm_spin_->value());
    s.setValue("car_turn_min_rpm", car_turn_min_rpm_spin_->value());
    s.setValue("car_max_rpm",   car_max_rpm_spin_->value());
    s.setValue("car_mmps",      car_mmps_per_rpm_spin_->value());
    s.setValue("car_pwm",       car_limit_pwm_spin_->value());
    s.setValue("car_turn_pwm",  car_turn_limit_pwm_spin_->value());
    s.setValue("car_left_trim", car_left_trim_spin_->value());
    s.setValue("car_right_trim", car_right_trim_spin_->value());
    s.setValue("car_ramp_steps", car_ramp_steps_spin_->value());
    s.setValue("car_ramp_dt",   car_ramp_dt_spin_->value());
    s.setValue("car_send_int",  car_send_interval_spin_->value());

    s.setValue("ap_can",        ap_can_edit_->text());
    s.setValue("ap_bitrate",    ap_bitrate_spin_->value());
    s.setValue("ap_r1_addr",    ap_rail1_addr_spin_->value());
    s.setValue("ap_r2_addr",    ap_rail2_addr_spin_->value());
    s.setValue("ap_r3_addr",    ap_rail3_addr_spin_->value());
    s.setValue("ap_rpm",        ap_rail_rpm_spin_->value());
    s.setValue("ap_acc",        ap_rail_acc_spin_->value());
    s.setValue("ap_pulses_mm",  ap_pulses_per_mm_spin_->value());
    s.setValue("ap_home_off",   ap_home_offset_spin_->value());
    s.setValue("ap_home_dir",   ap_home_dir_combo_->currentIndex());
    for (int i = 0; i < 3; ++i) {
        s.setValue(QString("ap_r%1_min").arg(i+1), ap_rail_min_spin_[i]->value());
        s.setValue(QString("ap_r%1_max").arg(i+1), ap_rail_max_spin_[i]->value());
    }

    s.setValue("ap_relay1",         ap_relay1_path_edit_->text());
    s.setValue("ap_relay2",         ap_relay2_path_edit_->text());
    s.setValue("ap_relay3",         ap_relay3_path_edit_->text());
    s.setValue("ap_relay4",         ap_relay4_path_edit_->text());
    s.setValue("ap_gripper_relay",  ap_gripper_relay_edit_->text());
    s.setValue("ap_active_idx",     ap_relay_active_combo_->currentIndex());
    s.setValue("ap_gpio_mode",      ap_gpio_mode_combo_->currentIndex());
    s.setValue("ap_pulse_w",        ap_pulse_width_spin_->value());
    s.setValue("ap_debounce",       ap_debounce_spin_->value());
    s.setValue("ap_watchdog",       ap_watchdog_spin_->value());
    s.setValue("ap_estop_idx",      ap_estop_combo_->currentIndex());

    s.setValue("rs_w",          rs_width_spin_->value());
    s.setValue("rs_h",          rs_height_spin_->value());
    s.setValue("rs_fps",        rs_fps_spin_->value());
    s.setValue("rs_fmt",        rs_color_format_combo_->currentIndex());
    s.setValue("rs_dw",         rs_depth_w_spin_->value());
    s.setValue("rs_dh",         rs_depth_h_spin_->value());
    s.setValue("rs_dfps",       rs_depth_fps_spin_->value());
    s.setValue("rs_depth_idx",  rs_depth_combo_->currentIndex());
    s.setValue("rs_laser",      rs_laser_power_spin_->value());
    s.setValue("rs_exp_auto",   rs_exposure_auto_combo_->currentIndex());
    s.setValue("rs_exp_us",     rs_exposure_us_spin_->value());
    s.setValue("rs_gain",       rs_gain_spin_->value());
    s.setValue("rs_wb",         rs_wb_combo_->currentIndex());
    s.setValue("rs_dec",        rs_decimation_spin_->value());
    s.setValue("rs_tem",        rs_temporal_spin_->value());
    s.setValue("rs_spa",        rs_spatial_spin_->value());
    s.setValue("rs_align",      rs_align_combo_->currentIndex());

    s.setValue("npu_strategy_idx", npu_strategy_combo_->currentIndex());
    s.setValue("npu_model",        npu_model_path_edit_->text());
    s.setValue("npu_core",         npu_core_combo_->currentIndex());
    s.setValue("npu_conf",         npu_threshold_spin_->value());
    s.setValue("npu_nms",          npu_nms_thresh_spin_->value());
    s.setValue("npu_in",           npu_input_size_spin_->value());
    s.setValue("npu_max",          npu_max_boxes_spin_->value());
    s.setValue("npu_topk",         npu_top_k_spin_->value());
    s.setValue("npu_filter",       npu_class_filter_edit_->text());
    s.setValue("npu_decode",       npu_post_decode_combo_->currentIndex());

    s.setValue("gripper_uart",      gripper_uart_edit_->text());
    s.setValue("gripper_uart_baud", gripper_uart_baud_combo_->currentText());
    s.setValue("g_uart_data",       gripper_uart_databits_combo_->currentIndex());
    s.setValue("g_uart_stop",       gripper_uart_stopbits_combo_->currentIndex());
    s.setValue("g_uart_par",        gripper_uart_parity_combo_->currentIndex());
    s.setValue("g_uart_flow",       gripper_uart_flow_combo_->currentIndex());
    s.setValue("g_uart_to",         gripper_uart_timeout_spin_->value());
    s.setValue("g_uart_retry",      gripper_uart_retry_spin_->value());

    s.endGroup();
}
