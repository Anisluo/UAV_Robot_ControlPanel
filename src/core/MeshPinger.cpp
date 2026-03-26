#include "MeshPinger.h"

#include <QProcess>
#include <QTimer>
#include <QSharedPointer>

// Fixed mesh topology layout: .101 is the host/gateway node, .102~.106 are UAVs.
static const struct {
    int   octet;
    float x;
    float y;
} kNodes[] = {
    { 101, 0.50f, 0.18f },
    { 102, 0.14f, 0.58f },
    { 103, 0.32f, 0.78f },
    { 104, 0.50f, 0.60f },
    { 105, 0.68f, 0.78f },
    { 106, 0.86f, 0.58f },
};
static constexpr int kNodeCount = static_cast<int>(sizeof(kNodes) / sizeof(kNodes[0]));

MeshPinger::MeshPinger(QObject *parent)
    : QObject(parent)
    , timer_(new QTimer(this))
    , busy_(false)
{
    connect(timer_, &QTimer::timeout, this, &MeshPinger::onTimer);
}

void MeshPinger::start(int intervalMs)
{
    timer_->start(intervalMs);
    onTimer();   // fire immediately so the UI isn't blank on first show
}

void MeshPinger::stop()
{
    timer_->stop();
}

void MeshPinger::onTimer()
{
    if (busy_) return;   // previous cycle still in flight
    busy_ = true;

    // Shared result vector — written from individual process callbacks,
    // read once all 6 are done.  Both are owned by the lambdas only.
    auto results   = QSharedPointer<QVector<MeshNode>>::create(kNodeCount);
    auto remaining = QSharedPointer<int>::create(kNodeCount);

    for (int i = 0; i < kNodeCount; ++i) {
        (*results)[i].id        = kNodes[i].octet;
        (*results)[i].x         = kNodes[i].x;
        (*results)[i].y         = kNodes[i].y;
        (*results)[i].rssi      = 0;   // not measured, ping-only activation state
        (*results)[i].reachable = false;
    }

    for (int i = 0; i < kNodeCount; ++i) {
        QString ip = QString("192.168.1.%1").arg(kNodes[i].octet);

        QProcess *proc = new QProcess(this);

        connect(proc,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this,
                [this, proc, i, results, remaining](int exitCode, QProcess::ExitStatus)
                {
                    (*results)[i].reachable = (exitCode == 0);
                    proc->deleteLater();

                    if (--(*remaining) == 0) {
                        // All 6 pings are done
                        busy_ = false;
                        QList<MeshNode> nodes;
                        for (const MeshNode &n : *results)
                            nodes.append(n);
                        emit nodesUpdated(nodes);
                    }
                });

        proc->start("ping", {"-c", "1", "-W", "1", ip});
    }
}
