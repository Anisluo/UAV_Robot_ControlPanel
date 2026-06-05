#ifndef MESHMAPWIDGET_H
#define MESHMAPWIDGET_H

#include <QGroupBox>
#include <QList>
#include <QString>
#include <QPointF>
#include <QWidget>

class QPushButton;
class QLabel;

struct MeshNode {
    int     id;
    float   x;        // normalized 0..1
    float   y;        // normalized 0..1
    int     rssi;     // e.g. -30 (strong) to -90 (weak)
    bool    reachable;
};

// Inner canvas: handles the actual node/link painting. Kept as a separate
// widget so MeshMapWidget can host a QVBoxLayout (canvas + button row +
// params strip) without fighting paintEvent geometry.
class MeshCanvas : public QWidget {
public:
    explicit MeshCanvas(QWidget *parent = nullptr);
    void setNodes(const QList<MeshNode> &nodes);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPointF nodePos(const MeshNode &n) const;
    int     rssiToThickness(int rssi) const;
    QColor  rssiToColor(int rssi) const;

    QList<MeshNode> nodes_;
};

class MeshMapWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit MeshMapWidget(QWidget *parent = nullptr);

public slots:
    void updateNodes(const QList<MeshNode> &nodes);

signals:
    // Emitted whenever the 仿真 button toggles. `nodes` is the simulated
    // mesh snapshot (4 lit UAV nodes when active; idle skeleton when off).
    // DroneWidget listens to fill in mock per-node telemetry.
    void simulationToggled(bool active, const QList<MeshNode> &nodes);

private slots:
    void onSimulateToggled();

private:
    void populateDemoNodes();
    QList<MeshNode> buildSimulationNodes() const;
    void refreshParamLabel(const QList<MeshNode> &nodes, bool simActive);

    MeshCanvas  *canvas_;
    QPushButton *sim_button_;
    QLabel      *param_label_;
    bool         sim_active_;
    QList<MeshNode> idle_nodes_;   // default 6-node skeleton (all inactive)
    QList<MeshNode> last_real_nodes_;
};

#endif // MESHMAPWIDGET_H
