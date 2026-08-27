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
    // Recentre on a named city. Also becomes the origin for simulated flights.
    void setCity(int index);
    // 仿真: fly three drones around the current city on a timer.
    void setSimulation(bool on);

private slots:
    void onSimTick();

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
    QLabel    *title_label_  = nullptr;
    class QComboBox *city_combo_ = nullptr;
    QCheckBox *sim_check_    = nullptr;

    // ── 仿真 ────────────────────────────────────────────────────────────
    // Three drones flown on independent closed paths around the selected
    // city. Positions are generated in WGS-84 and pushed through the SAME
    // setDronePosition() the real telemetry uses, so the GCJ-02 conversion,
    // track accumulation and rendering are all exercised for real rather
    // than short-circuited into a special "demo" drawing path.
    enum AssetKind { AssetDrone = 0, AssetDock = 1, AssetCar = 2 };

    // Assets fly WAYPOINT ROUTES, not circles. A perfect circle is not a
    // shape any real mission flies — surveys are lawnmower passes, patrols
    // are straight legs with turns. Positions are interpolated along a
    // polyline at constant ground speed, in metres relative to HOME.
    struct SimDrone {
        int     octet;
        int     kind = AssetDrone;
        QString name;
        QVector<QPointF> route;    // metres, x=east y=north, closed loop
        double  s = 0.0;           // arclength travelled along route, m
        double  speed_mps = 12.0;
        double  alt_m = 0.0;
        int     battery_pct = 100;
        QPointF form_offset;       // metres, in the leader's frame
        double  last_lat = 0.0, last_lng = 0.0;
        bool    has_last = false;
    };
    QVector<SimDrone> sim_drones_;

    // Formation state. The three aircraft alternate between flying a shared
    // leader route in a V and splitting to their own survey boxes.
    QVector<QPointF> leader_route_;   // metres
    double  leader_s_ = 0.0;
    double  home_lat_ = 0.0, home_lng_ = 0.0;
    double  sim_clock_s_ = 0.0;
    bool    formation_ = true;
    QLabel *phase_label_ = nullptr;

    // Fixed ground infrastructure (大疆机场). Static, so it is kept separate
    // from the moving assets and drawn straight from WGS-84 each frame.
    struct SiteMarker { QString name; double lat, lng; };
    QVector<SiteMarker> sites_;
    QHash<int, int>     kind_of_;   // octet → AssetKind, for the renderer
    class QTimer *sim_timer_ = nullptr;
    bool   sim_on_ = false;
    // Extra per-node facts the map shows in its red HUD labels. Real
    // telemetry has no source for these yet, so they are only populated in
    // simulation and the labels degrade to position-only without them.
    QHash<int, double> info_alt_;
    QHash<int, double> info_spd_;
    QHash<int, int>    info_bat_;
    QHash<int, double> info_hdg_;
    // True WGS-84 fix per node, kept for the label — latest_ holds GCJ-02
    // (needed for drawing) and printing that as "GPS" would be wrong by a
    // few hundred metres.
    QHash<int, QPointF> latest_wgs_;
};

#endif // MAPWIDGET_H
