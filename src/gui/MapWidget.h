#ifndef MAPWIDGET_H
#define MAPWIDGET_H

#include <QWidget>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QPointF>
#include <QPixmap>

class QNetworkAccessManager;
class QNetworkReply;
class QLabel;
class QCheckBox;
class QPushButton;

// MapWidget — a lightweight "slippy map" built on QWidget + QNetworkAccessManager.
//
// It fetches free OpenStreetMap raster tiles (Web-Mercator, WGS-84 datum — the
// same datum GPS reports in, so no China GCJ-02 offset correction is needed) and
// paints the drone position marker + flight trajectory on top. Tiles are cached
// in memory and on disk so the map keeps working offline after a first load.
//
// Deliberately avoids QtLocation / QtWebEngine so it links against the existing
// Widgets + Network components only. Default view is centred on Fuzhou (福州).
class MapWidget : public QWidget {
    Q_OBJECT
public:
    explicit MapWidget(QWidget *parent = nullptr);

public slots:
    // Feed one WGS-84 fix for drone node `octet` (.10X). Appends to that node's
    // track, moves its marker, and — when 跟随 is enabled — recentres the view.
    void setDronePosition(int octet, double lat, double lng);
    // Drop every recorded track (清空轨迹).
    void clearTracks();

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void requestVisibleTiles();
    void onTileDownloaded(QNetworkReply *reply);
    void updateCoordLabel(int octet, double lat, double lng);

    static QString tileKey(int z, int x, int y);
    QString        tileDiskPath(int z, int x, int y) const;

    // Web-Mercator forward projection (returns fractional tile coordinates).
    static double lonToTileX(double lon, int z);
    static double latToTileY(double lat, int z);

    QNetworkAccessManager  *nam_ = nullptr;
    QHash<QString, QPixmap> tile_cache_;   // "z/x/y" -> tile
    QSet<QString>           pending_tiles_;

    double center_lat_;
    double center_lng_;
    int    zoom_;

    // Per-node history. Points are stored as QPointF(lng, lat).
    QHash<int, QVector<QPointF>> tracks_;
    QHash<int, QPointF>          latest_;
    int    last_octet_ = -1;

    bool   follow_   = true;
    bool   dragging_ = false;
    QPoint drag_last_;

    QLabel    *coord_label_  = nullptr;
    QCheckBox *follow_check_ = nullptr;
};

#endif // MAPWIDGET_H
