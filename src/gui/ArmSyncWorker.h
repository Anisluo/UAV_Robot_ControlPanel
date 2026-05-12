#ifndef ARMSYNCWORKER_H
#define ARMSYNCWORKER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QThread>

#include "ArmViewer3D.h"    // for ArmMeshCPU

class QTimer;

// Does two pieces of potentially-slow work off the UI thread:
//
//   1. Parse arm_model.json and all referenced binary STL files on
//      startup. STL parsing is CPU-bound (~10 MB of triangles total
//      for this model) and would stall the GUI event loop if done
//      in-line.  As each mesh is ready, it's emitted via meshReady()
//      for the viewer to upload to the GPU.
//
//   2. Poll the remote arm's motor angles on a timer. The TCP round-
//      trip itself is fast (<10 ms) but we don't want to block the
//      UI if the link is congested. The worker owns a QTimer and
//      emits requestAngles() so the main-thread RpcClient (which must
//      be main-thread to keep Qt socket affinity happy) can fetch.
//      When the reply comes back, the GUI forwards it to this worker
//      via pushAngles(); the worker then emits anglesReady() back to
//      the viewer. This round-trip keeps the polling cadence owned by
//      the worker without crossing thread affinity on the socket.
//
// Everything touching the UI must be signal/slot based: workers live
// on their own QThread and never call viewer methods directly.
class ArmSyncWorker : public QObject {
    Q_OBJECT
public:
    explicit ArmSyncWorker(QObject *parent = nullptr);

    // Load arm_model.json + its STL parts relative to this directory.
    // Triggered on the worker thread via queued connection.
    Q_INVOKABLE void loadAssets(const QString &model_dir);

    // Start / stop the angle-poll timer.  Intervals in ms.
    Q_INVOKABLE void startAnglePolling(int interval_ms = 200);
    Q_INVOKABLE void stopAnglePolling();

    // Called from the main thread after an RPC reply lands. Forwards
    // to anglesReady() for the viewer (keeps slot affinity consistent
    // — the viewer only listens on this one source).
    Q_INVOKABLE void pushAngles(const QVector<float> &degrees);

signals:
    // One mesh loaded and ready to be uploaded to the GPU.
    void meshReady(const ArmMeshCPU &mesh);
    // All STL files loaded (emitted once at end of loadAssets).
    void loadComplete(int n_meshes, QString status);
    // Fired by the poll timer — main thread connects to this and
    // makes the actual RPC call.
    void requestAngles();
    // Forwarded out to the viewer.
    void anglesReady(const QVector<float> &degrees);
    // Config data parsed from arm_model.json, for the viewer.
    void configReady(const QVector<QVector3D> &axes,
                     const QVector<QVector3D> &origins,
                     const QVector3D           &scene_center,
                     float                      scene_radius);
    // Progress / diagnostic messages for the log panel.
    void logMessage(const QString &msg);

private:
    QTimer *poll_timer_ = nullptr;
};

#endif // ARMSYNCWORKER_H
