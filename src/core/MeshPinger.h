#ifndef MESHPINGER_H
#define MESHPINGER_H

#include <QObject>
#include <QList>
#include <QSharedPointer>
#include <QVector>

#include "gui/MeshMapWidget.h"   // MeshNode

class QTimer;
class QProcess;

/*
 * MeshPinger — periodically pings 192.168.1.101..106 in parallel.
 *
 * On each cycle all 6 pings are launched simultaneously (ping -c1 -W1).
 * When every process finishes the nodesUpdated() signal is emitted with
 * MeshNode.reachable set according to the ping exit code (0 = reachable).
 *
 * Usage:
 *   pinger->start(5000);   // every 5 s
 *   connect(pinger, &MeshPinger::nodesUpdated,
 *           mesh_widget_, &MeshMapWidget::updateNodes);
 */
class MeshPinger : public QObject {
    Q_OBJECT
public:
    explicit MeshPinger(QObject *parent = nullptr);

    /* Start the ping cycle.  An immediate first run is triggered. */
    void start(int intervalMs = 5000);
    void stop();

signals:
    void nodesUpdated(const QList<MeshNode> &nodes);

private slots:
    void onTimer();

private:
    QTimer *timer_;
    bool    busy_;   // guard: don't start a new cycle while one is running
};

#endif // MESHPINGER_H
