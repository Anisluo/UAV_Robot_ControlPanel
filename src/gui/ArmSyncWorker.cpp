#include "ArmSyncWorker.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QtEndian>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <cstring>

// Binary STL layout (little-endian):
//   uint8[80] header
//   uint32 triangle count
//   { float[3] normal; float[3] v0; float[3] v1; float[3] v2; uint16 attrib }
//     per triangle.
//
// We ignore stored normals and recompute them from triangle geometry.
static bool parseStlBinary(const QByteArray &data,
                           QVector<float> &vbo_out,
                           qsizetype &vertex_count_out)
{
    if (data.size() < 84) return false;

    uint32_t n_tri = 0;
    std::memcpy(&n_tri, data.constData() + 80, sizeof(n_tri));
    n_tri = qFromLittleEndian(n_tri);

    const qsizetype expected = 84 + qsizetype(n_tri) * 50;
    if (data.size() < expected) return false;

    vbo_out.clear();
    vbo_out.reserve(int(n_tri) * 3 * 6);

    const char *p = data.constData() + 84;
    for (uint32_t i = 0; i < n_tri; ++i) {
        const char *v = p + 12;  // skip stored normal
        float v0[3], v1[3], v2[3];
        std::memcpy(v0, v + 0 * 12, 12);
        std::memcpy(v1, v + 1 * 12, 12);
        std::memcpy(v2, v + 2 * 12, 12);

        const float ux = v1[0] - v0[0];
        const float uy = v1[1] - v0[1];
        const float uz = v1[2] - v0[2];
        const float vx = v2[0] - v0[0];
        const float vy = v2[1] - v0[1];
        const float vz = v2[2] - v0[2];
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;
        const float mag = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (mag > 1e-6F) {
            nx /= mag;
            ny /= mag;
            nz /= mag;
        }

        const float *verts[3] = {v0, v1, v2};
        for (int k = 0; k < 3; ++k) {
            vbo_out.append(verts[k][0]);
            vbo_out.append(verts[k][1]);
            vbo_out.append(verts[k][2]);
            vbo_out.append(nx);
            vbo_out.append(ny);
            vbo_out.append(nz);
        }

        p += 50;
    }

    vertex_count_out = qsizetype(n_tri) * 3;
    return true;
}

ArmSyncWorker::ArmSyncWorker(QObject *parent) : QObject(parent) {}

void ArmSyncWorker::loadAssets(const QString &model_dir)
{
    const QDir dir(model_dir);
    const QString jsonPath = dir.filePath("arm_model.json");
    QFile jf(jsonPath);
    if (!jf.open(QIODevice::ReadOnly)) {
        emit logMessage(QStringLiteral("[3D] cannot open ") + jsonPath);
        emit loadComplete(0, QStringLiteral("arm_model.json not found"));
        return;
    }

    const QByteArray raw = jf.readAll();
    jf.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit logMessage(QStringLiteral("[3D] arm_model.json parse failed: ")
                        + err.errorString());
        emit loadComplete(0, err.errorString());
        return;
    }

    const QJsonObject root = doc.object();

    QVector<QVector3D> axes;
    for (const auto &v : root.value("joint_axes").toArray()) {
        const QJsonArray a = v.toArray();
        axes.append(QVector3D(float(a.at(0).toDouble(0)),
                              float(a.at(1).toDouble(0)),
                              float(a.at(2).toDouble(1))).normalized());
    }

    QVector<QVector3D> origins;
    for (const auto &v : root.value("joint_origins").toArray()) {
        const QJsonArray a = v.toArray();
        origins.append(QVector3D(float(a.at(0).toDouble(0)),
                                 float(a.at(1).toDouble(0)),
                                 float(a.at(2).toDouble(0))));
    }

    QVector3D scene_center;
    float scene_radius = 400.0F;
    const QJsonArray bbox = root.value("scene_bbox").toArray();
    if (bbox.size() == 6) {
        const float xmin = float(bbox.at(0).toDouble());
        const float ymin = float(bbox.at(1).toDouble());
        const float zmin = float(bbox.at(2).toDouble());
        const float xmax = float(bbox.at(3).toDouble());
        const float ymax = float(bbox.at(4).toDouble());
        const float zmax = float(bbox.at(5).toDouble());
        scene_center = QVector3D((xmin + xmax) * 0.5F,
                                 (ymin + ymax) * 0.5F,
                                 (zmin + zmax) * 0.5F);
        scene_radius = std::max({xmax - xmin, ymax - ymin, zmax - zmin}) * 0.6F;
    }

    emit configReady(axes, origins, scene_center, scene_radius);

    int n_loaded = 0;
    for (const auto &v : root.value("parts").toArray()) {
        const QJsonObject part = v.toObject();
        const QString stlName = part.value("stl").toString();
        const int joint = part.value("joint").toInt(0);

        QFile sf(dir.filePath(stlName));
        if (!sf.open(QIODevice::ReadOnly)) {
            emit logMessage(QStringLiteral("[3D] missing STL: ") + stlName);
            continue;
        }

        const QByteArray data = sf.readAll();
        sf.close();

        ArmMeshCPU mesh;
        mesh.joint_idx = joint;
        qsizetype vcount = 0;
        if (!parseStlBinary(data, mesh.vbo_data, vcount)) {
            emit logMessage(QStringLiteral("[3D] bad STL: ") + stlName);
            continue;
        }

        mesh.vertex_count = size_t(vcount);
        emit meshReady(mesh);
        ++n_loaded;
    }

    emit loadComplete(n_loaded,
                      QStringLiteral("load complete: %1 parts").arg(n_loaded));
}

void ArmSyncWorker::startAnglePolling(int interval_ms)
{
    if (!poll_timer_) {
        poll_timer_ = new QTimer(this);
        QObject::connect(poll_timer_, &QTimer::timeout,
                         this, [this]() { emit requestAngles(); });
    }
    poll_timer_->start(interval_ms);
}

void ArmSyncWorker::stopAnglePolling()
{
    if (poll_timer_) poll_timer_->stop();
}

void ArmSyncWorker::pushAngles(const QVector<float> &degrees)
{
    emit anglesReady(degrees);
}
