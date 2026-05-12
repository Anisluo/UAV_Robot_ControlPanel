#ifndef ARMVIEWER3D_H
#define ARMVIEWER3D_H

// Keep heavy OpenGL headers out of this public header — moc happily
// parses them but MinGW 13's moc on Windows segfaults while serialising
// types that reference QOpenGLBuffer in member layouts. Full definition
// of ArmMeshGPU + OpenGL plumbing lives in ArmViewer3D.cpp.
#include <QOpenGLWidget>
#include <QMatrix4x4>
#include <QMetaType>
#include <QVector>
#include <QVector3D>
#include <QString>

#include <memory>

// CPU-side mesh, copyable / queued-signal friendly.
struct ArmMeshCPU {
    int               joint_idx = 0;    // which joint this part follows
    QVector<float>    vbo_data;          // interleaved pos3 + normal3 per vertex
    size_t            vertex_count = 0;
};

struct ArmMeshGPU;                       // full def in the .cpp

class ArmViewer3D : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit ArmViewer3D(QWidget *parent = nullptr);
    ~ArmViewer3D() override;

    // Scene / kinematic configuration — set once after construction.
    struct Config {
        QVector<QVector3D> joint_axes;    // axes for J1..J6 (unit vectors)
        QVector<QVector3D> joint_origins; // rotation centres in model space
        QVector3D          scene_center;  // for camera framing
        float              scene_radius = 400.0F;
    };
    void setConfig(const Config &c);

public slots:
    // Called from the loader thread via queued connection. Appends one
    // mesh and queues an upload on the next paintGL().
    void addMeshCPU(const ArmMeshCPU &m);

    // Live joint angles in degrees (length should match joint_axes.size()).
    // Safe to call from any thread — Qt wraps through the event loop.
    void setJointAngles(const QVector<float> &degrees);

    // Overlay status message (shown top-left, HUD-style).
    void setStatusText(const QString &text);

    // Show / hide small coloured markers at every joint rotation centre.
    // Useful for verifying joint_origins by eye.
    void setShowJointMarkers(bool on);

    // End-effector pose for the HUD readout (mm + deg). Push from the
    // owning widget whenever it polls arm.get_pose.
    void setEndEffectorPose(float x_mm, float y_mm, float z_mm,
                            float rx_deg, float ry_deg, float rz_deg);

    // Show / hide the world-frame XYZ axis triad rooted at scene origin.
    void setShowAxesTriad(bool on);

    // Show / hide the top-left pose + joint readout HUD (X/Y/Z/RX/RY/RZ
    // + J1..J6, monospace, mirrors AgileX ArmRobotUA.exe layout).
    void setShowPoseHud(bool on);

signals:
    // UI thread signals into the loader thread — see ArmSyncWorker.
    void requestReload();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

public:
    // Exposed only so ArmViewer3D.cpp's free helpers can touch it; the
    // actual definition and OpenGL plumbing stay confined to the .cpp.
    // Keeping it nested avoids leaking OpenGL headers out of this file.
    struct Impl;
private:
    std::unique_ptr<Impl> d_;
};

Q_DECLARE_METATYPE(ArmMeshCPU)

#endif // ARMVIEWER3D_H
