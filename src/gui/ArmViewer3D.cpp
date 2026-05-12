#include "ArmViewer3D.h"

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QFont>
#include <QPoint>
#include <QSurfaceFormat>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// ── GPU-side mesh — stays out of the public header so moc doesn't trip
// on QOpenGLBuffer in the class' member layout. ────────────────────────
struct ArmMeshGPU {
    int                      joint_idx    = 0;
    QOpenGLBuffer            vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject vao;
    GLsizei                  vertex_count = 0;
};

// ── Impl owns everything GL-related. PImpl over ArmViewer3D. ────────────
struct ArmViewer3D::Impl : public QOpenGLFunctions_3_3_Core {
    // Scene / kinematic config.
    ArmViewer3D::Config               config;
    QVector<float>                    joint_angles;

    // CPU meshes pending upload, and GPU meshes drawn every frame.
    QVector<ArmMeshCPU>                           pending;
    std::vector<std::unique_ptr<ArmMeshGPU>>      meshes;

    // Phong shader.
    QOpenGLShaderProgram prog;
    int loc_mvp = -1, loc_model = -1, loc_view_pos = -1;
    int loc_light_dir = -1, loc_base_color = -1, loc_highlight = -1;

    // Orbit camera.
    float   cam_yaw   = 35.0F;
    float   cam_pitch = 25.0F;
    float   cam_dist  = 900.0F;
    QPoint  last_mouse;

    bool    initialized  = false;
    bool    show_markers = false;  // J1..J6 cross markers (debug)
    bool    show_axes    = true;   // world-frame X/Y/Z arrows at origin
    bool    show_hud     = true;   // top-left pose + joint readout
    QString status_text;

    // End-effector pose for HUD (mm + deg). Pushed via setEndEffectorPose;
    // owning widget calls that whenever it polls arm.get_pose.
    float   pose_xyz[3]  = {0.0F, 0.0F, 0.0F};
    float   pose_rpy[3]  = {0.0F, 0.0F, 0.0F};
    bool    pose_valid   = false;
};

// ── Shaders ──────────────────────────────────────────────────────────────
static const char *kVertSrc = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uMVP;
uniform mat4 uModel;
out vec3 vWorld;
out vec3 vNormal;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorld  = world.xyz;
    // uModel is a rigid transform (R+T), so this works without a normal
    // matrix reconstruction (avoids inverting on the CPU every frame).
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

static const char *kFragSrc = R"GLSL(
#version 330 core
in vec3 vWorld;
in vec3 vNormal;
uniform vec3 uViewPos;
uniform vec3 uLightDir;
uniform vec3 uBaseColor;
uniform float uHighlight;
out vec4 fragColor;
void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDir);
    vec3 V = normalize(uViewPos - vWorld);
    vec3 R = reflect(-L, N);
    float ambient  = 0.25;
    float diffuse  = max(dot(N, L), 0.0) * 0.7;
    float specular = pow(max(dot(R, V), 0.0), 32.0) * 0.35;
    vec3  color    = uBaseColor * (ambient + diffuse) + vec3(1.0) * specular;
    color = mix(color, vec3(1.0, 0.85, 0.25), uHighlight);
    fragColor = vec4(color, 1.0);
}
)GLSL";


ArmViewer3D::ArmViewer3D(QWidget *parent)
    : QOpenGLWidget(parent), d_(std::make_unique<Impl>())
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);      // 4× MSAA — a small quality win on CAD meshes
    setFormat(fmt);
}

ArmViewer3D::~ArmViewer3D() {
    makeCurrent();
    d_->meshes.clear();     // destroys VAO/VBOs while context is current
    doneCurrent();
}

void ArmViewer3D::setConfig(const Config &c) {
    d_->config = c;
    d_->joint_angles.fill(0.0F, c.joint_axes.size());
    if (c.scene_radius > 0.0F) {
        d_->cam_dist = c.scene_radius * 2.4F;
    }
    update();
}

void ArmViewer3D::addMeshCPU(const ArmMeshCPU &m) {
    d_->pending.append(m);
    update();               // paintGL will pick it up and upload to GPU
}

void ArmViewer3D::setJointAngles(const QVector<float> &degrees) {
    const int n = std::min<int>(int(degrees.size()),
                                 int(d_->joint_angles.size()));
    for (int i = 0; i < n; ++i) d_->joint_angles[i] = degrees[i];
    update();
}

void ArmViewer3D::setStatusText(const QString &text) {
    d_->status_text = text;
    update();
}

void ArmViewer3D::setEndEffectorPose(float x_mm, float y_mm, float z_mm,
                                      float rx_deg, float ry_deg, float rz_deg) {
    d_->pose_xyz[0] = x_mm;  d_->pose_xyz[1] = y_mm;  d_->pose_xyz[2] = z_mm;
    d_->pose_rpy[0] = rx_deg; d_->pose_rpy[1] = ry_deg; d_->pose_rpy[2] = rz_deg;
    d_->pose_valid = true;
    update();
}

void ArmViewer3D::setShowAxesTriad(bool on) {
    d_->show_axes = on;
    update();
}

void ArmViewer3D::setShowPoseHud(bool on) {
    d_->show_hud = on;
    update();
}

void ArmViewer3D::setShowJointMarkers(bool on) {
    d_->show_markers = on;
    update();
}

void ArmViewer3D::initializeGL() {
    d_->initializeOpenGLFunctions();
    d_->glEnable(GL_DEPTH_TEST);
    d_->glEnable(GL_MULTISAMPLE);
    d_->glClearColor(0.06f, 0.07f, 0.12f, 1.0f);

    if (!d_->prog.addShaderFromSourceCode(QOpenGLShader::Vertex,   kVertSrc) ||
        !d_->prog.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragSrc) ||
        !d_->prog.link()) {
        qWarning("ArmViewer3D: shader link failed: %s",
                 qPrintable(d_->prog.log()));
    }
    d_->loc_mvp        = d_->prog.uniformLocation("uMVP");
    d_->loc_model      = d_->prog.uniformLocation("uModel");
    d_->loc_view_pos   = d_->prog.uniformLocation("uViewPos");
    d_->loc_light_dir  = d_->prog.uniformLocation("uLightDir");
    d_->loc_base_color = d_->prog.uniformLocation("uBaseColor");
    d_->loc_highlight  = d_->prog.uniformLocation("uHighlight");
    d_->initialized    = true;
}

static void upload_pending(ArmViewer3D::Impl *d) {
    while (!d->pending.isEmpty()) {
        ArmMeshCPU m = d->pending.takeFirst();
        auto gpu = std::make_unique<ArmMeshGPU>();
        gpu->joint_idx    = m.joint_idx;
        gpu->vertex_count = static_cast<GLsizei>(m.vertex_count);

        gpu->vao.create();
        gpu->vao.bind();
        gpu->vbo.create();
        gpu->vbo.bind();
        gpu->vbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
        gpu->vbo.allocate(m.vbo_data.constData(),
                          static_cast<int>(m.vbo_data.size() * sizeof(float)));
        // layout: vec3 pos, vec3 normal, stride = 6 floats
        d->glEnableVertexAttribArray(0);
        d->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                                 6 * sizeof(float), (void *)0);
        d->glEnableVertexAttribArray(1);
        d->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                                 6 * sizeof(float),
                                 (void *)(3 * sizeof(float)));
        gpu->vbo.release();
        gpu->vao.release();
        d->meshes.push_back(std::move(gpu));
    }
}

static QMatrix4x4 link_transform(const ArmViewer3D::Impl *d, int joint_idx) {
    // Compose J1..Jk rotations about their own origins.  joint_idx == 0
    // means the part is stationary (base) — identity transform.
    QMatrix4x4 m;
    const int n = std::min<int>(joint_idx,
                                 std::min<int>(int(d->config.joint_axes.size()),
                                               int(d->joint_angles.size())));
    for (int j = 0; j < n; ++j) {
        const QVector3D o = d->config.joint_origins.value(j);
        const QVector3D a = d->config.joint_axes.value(j);
        m.translate( o);
        m.rotate(d->joint_angles[j], a);
        m.translate(-o);
    }
    return m;
}

void ArmViewer3D::paintGL() {
    if (!d_->initialized) return;
    upload_pending(d_.get());

    d_->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = width() > 0 && height() > 0
                          ? float(width()) / float(height()) : 1.0F;
    QMatrix4x4 proj;
    proj.perspective(45.0F, aspect, 1.0F, 5000.0F);

    const float yaw_rad   = d_->cam_yaw   * float(M_PI / 180.0);
    const float pitch_rad = d_->cam_pitch * float(M_PI / 180.0);
    QVector3D eye(
        d_->cam_dist * std::cos(pitch_rad) * std::sin(yaw_rad),
        d_->cam_dist * std::cos(pitch_rad) * std::cos(yaw_rad),
        d_->cam_dist * std::sin(pitch_rad));
    const QVector3D center = d_->config.scene_center;
    eye += center;

    QMatrix4x4 view;
    view.lookAt(eye, center, QVector3D(0, 0, 1));

    d_->prog.bind();
    d_->prog.setUniformValue(d_->loc_view_pos,  eye);
    d_->prog.setUniformValue(d_->loc_light_dir,
                             QVector3D(-0.4F, -0.6F, -0.8F).normalized());
    d_->prog.setUniformValue(d_->loc_highlight, 0.0F);

    static const QVector3D kLinkColors[8] = {
        {0.55F, 0.58F, 0.66F},  // base — steel grey
        {0.92F, 0.54F, 0.25F},  // J1 — orange
        {0.38F, 0.66F, 0.94F},  // J2 — blue
        {0.54F, 0.86F, 0.45F},  // J3 — green
        {0.90F, 0.70F, 0.30F},  // J4 — amber
        {0.78F, 0.45F, 0.85F},  // J5 — purple
        {0.96F, 0.44F, 0.55F},  // J6 — rose
        {0.60F, 0.60F, 0.60F},  // fallback
    };

    for (auto &g : d_->meshes) {
        QMatrix4x4 model = link_transform(d_.get(), g->joint_idx);
        const QMatrix4x4 mvp = proj * view * model;
        d_->prog.setUniformValue(d_->loc_mvp,        mvp);
        d_->prog.setUniformValue(d_->loc_model,      model);
        const int ci = std::min<int>(g->joint_idx,
            int(sizeof(kLinkColors)/sizeof(kLinkColors[0])) - 1);
        d_->prog.setUniformValue(d_->loc_base_color, kLinkColors[ci]);
        g->vao.bind();
        d_->glDrawArrays(GL_TRIANGLES, 0, g->vertex_count);
        g->vao.release();
    }
    d_->prog.release();

    // Joint-origin gizmos + status HUD overlay (single QPainter pass).
    if (d_->show_markers || d_->show_axes || d_->show_hud ||
        !d_->status_text.isEmpty()) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QFont f = p.font();
        f.setFamily("Consolas");
        f.setPointSize(9);
        p.setFont(f);
        const QMatrix4x4 vp = proj * view;

        // ── World-frame XYZ axis triad at origin ──────────────────────
        // Projects origin + (axis_len, 0, 0) etc. to screen, draws a
        // coloured arrow + R/G/B label at each tip (matches AgileX
        // ArmRobotUA.exe's visual convention).
        if (d_->show_axes) {
            const float axis_len = d_->config.scene_radius * 0.6F;
            auto project = [&](const QVector3D &w) {
                QVector4D c = vp * QVector4D(w, 1.0F);
                if (c.w() <= 0.001F) return QPoint(-9999, -9999);
                return QPoint(
                    int(( c.x() / c.w() * 0.5F + 0.5F) * width()),
                    int((-c.y() / c.w() * 0.5F + 0.5F) * height()));
            };
            const QPoint o_px = project(QVector3D(0, 0, 0));
            static const struct { QVector3D dir; QColor col; const char *lbl; } kAxes[3] = {
                { QVector3D(1,0,0), QColor(0xff,0x40,0x40), "X" },
                { QVector3D(0,1,0), QColor(0x40,0xc8,0x40), "Y" },
                { QVector3D(0,0,1), QColor(0x60,0x80,0xff), "Z" },
            };
            for (int k = 0; k < 3; ++k) {
                const QPoint tip = project(kAxes[k].dir * axis_len);
                if (tip.x() < -1000 || o_px.x() < -1000) continue;
                QPen pen(kAxes[k].col); pen.setWidth(3);
                p.setPen(pen); p.setBrush(kAxes[k].col);
                p.drawLine(o_px, tip);
                // Tiny arrowhead at the tip — square dot keeps it simple.
                p.drawEllipse(tip, 4, 4);
                p.setPen(QColor(0,0,0,200));
                p.drawText(tip.x()+9, tip.y()-7, kAxes[k].lbl);
                p.drawText(tip.x()+7, tip.y()-7, kAxes[k].lbl);
                p.drawText(tip.x()+8, tip.y()-8, kAxes[k].lbl);
                p.drawText(tip.x()+8, tip.y()-6, kAxes[k].lbl);
                p.setPen(kAxes[k].col);
                p.drawText(tip.x()+8, tip.y()-7, kAxes[k].lbl);
            }
        }

        // ── Top-left pose + joint readout HUD ─────────────────────────
        // Mirrors ArmRobotUA.exe layout: X/Y/Z (mm), RX/RY/RZ (deg), and
        // J1..J6 (deg) in monospace, slight rounded background.
        if (d_->show_hud) {
            QFont hudFont = f; hudFont.setPointSize(10); p.setFont(hudFont);
            const int       lh = 18;
            const int       lines = 6 + std::min<int>(6, int(d_->joint_angles.size()));
            const int       pad = 8;
            const int       boxX = 8, boxY = 8;
            const int       boxW = 200;
            const int       boxH = lh * lines + pad * 2 + 6;  // extra for separator gap
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x10, 0x18, 0x28, 0xb0));
            p.drawRoundedRect(boxX, boxY, boxW, boxH, 8, 8);
            p.setPen(QColor(0xd0, 0xd8, 0xe8));
            int y = boxY + pad + 12;
            auto row = [&](const char *label, float v, const char *unit) {
                p.setPen(QColor(0xa8, 0xb4, 0xc8));
                p.drawText(boxX + pad,           y, QString::fromLatin1(label));
                p.setPen(QColor(0xff, 0xff, 0xff));
                p.drawText(boxX + pad + 56,      y, QStringLiteral("%1").arg(v, 8, 'f', 3));
                p.setPen(QColor(0xa8, 0xb4, 0xc8));
                p.drawText(boxX + pad + 130,     y, QString::fromLatin1(unit));
                y += lh;
            };
            row("X:",  d_->pose_xyz[0], "mm");
            row("Y:",  d_->pose_xyz[1], "mm");
            row("Z:",  d_->pose_xyz[2], "mm");
            row("RX:", d_->pose_rpy[0], "deg");
            row("RY:", d_->pose_rpy[1], "deg");
            row("RZ:", d_->pose_rpy[2], "deg");
            y += 4;
            const int n_show = std::min<int>(6, int(d_->joint_angles.size()));
            for (int i = 0; i < n_show; ++i) {
                char lbl[8]; std::snprintf(lbl, sizeof(lbl), "J%d:", i + 1);
                row(lbl, d_->joint_angles[i], "deg");
            }
        }

        if (d_->show_markers) {
            // Project each joint's rotation centre to screen and draw a
            // coloured cross + label. Pivot is in world coords AFTER all
            // upstream joints have rotated, so we left-multiply
            // joint_origins[k] by the chain transform up to joint k-1.
            // (vp was computed at the top of the QPainter block.)
            static const QColor kMarkerColors[6] = {
                QColor(0xff, 0x90, 0x40),
                QColor(0x60, 0xa8, 0xff),
                QColor(0x80, 0xe0, 0x70),
                QColor(0xff, 0xc0, 0x40),
                QColor(0xc0, 0x70, 0xff),
                QColor(0xff, 0x60, 0x90),
            };
            const int n = std::min<int>(6, int(d_->config.joint_axes.size()));
            for (int k = 0; k < n; ++k) {
                QMatrix4x4 parent = link_transform(d_.get(), k);
                QVector3D  o      = d_->config.joint_origins.value(k);
                QVector3D  world  = parent * o;
                QVector4D  clip   = vp * QVector4D(world, 1.0F);
                if (clip.w() <= 0.001F) continue;
                const float nx = clip.x() / clip.w();
                const float ny = clip.y() / clip.w();
                const int   sx = int(( nx * 0.5F + 0.5F) * width());
                const int   sy = int((-ny * 0.5F + 0.5F) * height());

                QPen pen(kMarkerColors[k]);
                pen.setWidth(2);
                p.setPen(pen);
                p.setBrush(Qt::NoBrush);
                p.drawLine(sx - 9, sy, sx + 9, sy);
                p.drawLine(sx, sy - 9, sx, sy + 9);
                p.drawEllipse(QPoint(sx, sy), 5, 5);

                const QString label =
                    QStringLiteral("J%1 (%2,%3,%4)")
                        .arg(k + 1)
                        .arg(int(o.x()))
                        .arg(int(o.y()))
                        .arg(int(o.z()));
                p.setPen(QColor(0, 0, 0, 220));
                p.drawText(sx + 13, sy - 7, label);
                p.drawText(sx + 11, sy - 7, label);
                p.drawText(sx + 12, sy - 8, label);
                p.drawText(sx + 12, sy - 6, label);
                p.setPen(kMarkerColors[k]);
                p.drawText(sx + 12, sy - 7, label);
            }
        }

        if (!d_->status_text.isEmpty()) {
            p.setPen(QColor(0xc8, 0xd0, 0xe8));
            p.drawText(12, 18, d_->status_text);
        }
    }
}

void ArmViewer3D::resizeGL(int w, int h) {
    if (d_->initialized) d_->glViewport(0, 0, w, h);
}

void ArmViewer3D::mousePressEvent(QMouseEvent *e) {
    d_->last_mouse = e->pos();
}

void ArmViewer3D::mouseMoveEvent(QMouseEvent *e) {
    if (!(e->buttons() & Qt::LeftButton)) return;
    const QPoint dlt = e->pos() - d_->last_mouse;
    d_->last_mouse = e->pos();
    d_->cam_yaw   -= dlt.x() * 0.4F;
    d_->cam_pitch  = std::clamp(d_->cam_pitch + dlt.y() * 0.3F,
                                -85.0F, 85.0F);
    update();
}

void ArmViewer3D::wheelEvent(QWheelEvent *e) {
    const float delta = e->angleDelta().y() / 120.0F;
    d_->cam_dist *= std::pow(0.9F, delta);
    d_->cam_dist  = std::clamp(d_->cam_dist, 80.0F, 5000.0F);
    update();
}
