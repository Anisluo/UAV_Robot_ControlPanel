#ifndef MESHMAPWIDGET_H
#define MESHMAPWIDGET_H

#include <QGroupBox>
#include <QList>
#include <QString>
#include <QPointF>

struct MeshNode {
    int     id;
    float   x;        // normalized 0..1
    float   y;        // normalized 0..1
    int     rssi;     // e.g. -30 (strong) to -90 (weak)
    bool    reachable;
};

class MeshMapWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit MeshMapWidget(QWidget *parent = nullptr);

public slots:
    void updateNodes(const QList<MeshNode> &nodes);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void populateDemoNodes();
    QPointF nodePos(const MeshNode &n) const;
    int     rssiToThickness(int rssi) const;
    QColor  rssiToColor(int rssi) const;

    QList<MeshNode> nodes_;
};

#endif // MESHMAPWIDGET_H
