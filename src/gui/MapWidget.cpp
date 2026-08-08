#include "MapWidget.h"

#include <algorithm>
#include <cmath>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QPainter>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>
#include <QtMath>

namespace {
constexpr double kPi        = 3.14159265358979323846;
constexpr int    kTileSize  = 256;
constexpr int    kMinZoom   = 3;
constexpr int    kMaxZoom   = 18;

// Default view: Fuzhou (福州), Fujian.
constexpr double kFuzhouLat = 26.0745;
constexpr double kFuzhouLng = 119.2965;
constexpr int    kDefaultZoom = 12;

// ── WGS-84 → GCJ-02 (国测局 "火星坐标") ────────────────────────────────────────
// AutoNavi/高德 raster tiles are rendered in the GCJ-02 datum, while GPS reports
// WGS-84. Plotting a raw GPS fix on GCJ-02 tiles is off by ~50-500 m across
// China, so every geographic coordinate is converted before it is projected to
// a tile pixel. Standard "eviltransform" implementation.
bool outOfChina(double lat, double lng)
{
    return !(lng >= 72.004 && lng <= 137.8347 && lat >= 0.8293 && lat <= 55.8271);
}

double transformLatOffset(double x, double y)
{
    double ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * std::sqrt(std::fabs(x));
    ret += (20.0 * std::sin(6.0 * x * kPi) + 20.0 * std::sin(2.0 * x * kPi)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(y * kPi) + 40.0 * std::sin(y / 3.0 * kPi)) * 2.0 / 3.0;
    ret += (160.0 * std::sin(y / 12.0 * kPi) + 320.0 * std::sin(y * kPi / 30.0)) * 2.0 / 3.0;
    return ret;
}

double transformLngOffset(double x, double y)
{
    double ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * std::sqrt(std::fabs(x));
    ret += (20.0 * std::sin(6.0 * x * kPi) + 20.0 * std::sin(2.0 * x * kPi)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(x * kPi) + 40.0 * std::sin(x / 3.0 * kPi)) * 2.0 / 3.0;
    ret += (150.0 * std::sin(x / 12.0 * kPi) + 300.0 * std::sin(x / 30.0 * kPi)) * 2.0 / 3.0;
    return ret;
}

void wgs84ToGcj02(double lat, double lng, double &outLat, double &outLng)
{
    if (outOfChina(lat, lng)) { outLat = lat; outLng = lng; return; }
    constexpr double a  = 6378245.0;             // Krasovsky 1940 semi-major axis
    constexpr double ee = 0.00669342162296594323; // eccentricity squared
    double dLat = transformLatOffset(lng - 105.0, lat - 35.0);
    double dLng = transformLngOffset(lng - 105.0, lat - 35.0);
    const double radLat = lat / 180.0 * kPi;
    double magic = std::sin(radLat);
    magic = 1.0 - ee * magic * magic;
    const double sqrtMagic = std::sqrt(magic);
    dLat = (dLat * 180.0) / ((a * (1.0 - ee)) / (magic * sqrtMagic) * kPi);
    dLng = (dLng * 180.0) / (a / sqrtMagic * std::cos(radLat) * kPi);
    outLat = lat + dLat;
    outLng = lng + dLng;
}

// Distinct colours per drone node so overlapping tracks stay readable.
QColor nodeColor(int octet)
{
    static const QColor kPalette[] = {
        QColor("#22d3ee"), QColor("#f97316"), QColor("#a3e635"),
        QColor("#e879f9"), QColor("#facc15"), QColor("#60a5fa"),
    };
    int idx = octet - 101;
    if (idx < 0) idx = 0;
    return kPalette[idx % 6];
}

// Draw a small gray quadcopter (top-view) centred at `c`. `s` is the overall
// diameter in pixels — a body disc with four diagonal arms + rotor rings, plus
// a soft halo so it reads on both bright streets and dark water.
void drawDroneIcon(QPainter &p, const QPointF &c, qreal s, const QColor &accent)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal r    = s * 0.42;   // rotor-centre offset from body
    const qreal rot  = s * 0.24;   // rotor ring radius
    const QColor bodyCol(0xc7, 0xcd, 0xd7);   // light gray
    const QColor rotorCol(0xa7, 0xaf, 0xbd);  // mid gray
    const QColor edgeCol(0x2b, 0x31, 0x3e);   // dark outline for contrast

    // Soft white halo.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 70));
    p.drawEllipse(c, s * 0.72, s * 0.72);

    const QPointF rotors[4] = {
        c + QPointF(-r, -r), c + QPointF(r, -r),
        c + QPointF(r,  r),  c + QPointF(-r,  r),
    };

    // Arms (X).
    QPen armPen(edgeCol, std::max(2.0, s * 0.10));
    armPen.setCapStyle(Qt::RoundCap);
    p.setPen(armPen);
    p.drawLine(rotors[0], rotors[2]);
    p.drawLine(rotors[1], rotors[3]);

    // Rotor rings.
    p.setPen(QPen(edgeCol, std::max(1.2, s * 0.045)));
    p.setBrush(rotorCol);
    for (const QPointF &a : rotors)
        p.drawEllipse(a, rot, rot);

    // Body — accent-tinted ring so different nodes stay distinguishable.
    p.setPen(QPen(accent, std::max(1.6, s * 0.07)));
    p.setBrush(bodyCol);
    p.drawEllipse(c, s * 0.22, s * 0.22);

    p.restore();
}
} // namespace

MapWidget::MapWidget(QWidget *parent)
    : QWidget(parent)
    , nam_(new QNetworkAccessManager(this))
    , center_lat_(kFuzhouLat)
    , center_lng_(kFuzhouLng)
    , zoom_(kDefaultZoom)
{
    setMinimumHeight(160);
    setMouseTracking(false);
    setStyleSheet("");

    // Tiles are in the GCJ-02 datum — keep the view centre in GCJ-02 too so the
    // default Fuzhou view and the projected drone markers share one coordinate
    // space. The app forces QNetworkProxy::NoProxy globally (so LAN RPC/video
    // sockets stay direct); AutoNavi tiles are domestic and reachable directly,
    // so the map needs no proxy either.
    wgs84ToGcj02(kFuzhouLat, kFuzhouLng, center_lat_, center_lng_);

    connect(nam_, &QNetworkAccessManager::finished,
            this, &MapWidget::onTileDownloaded);

    // ── Top control bar (sits on top of the painted map) ─────────────────
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *bar = new QWidget(this);
    bar->setAutoFillBackground(true);
    bar->setStyleSheet(
        "background-color: rgba(14,20,34,220);"
        "border-bottom: 1px solid #222c44;");
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(8, 4, 8, 4);
    barLayout->setSpacing(10);

    auto *title = new QLabel("无人机地图 · 福州", bar);
    title->setStyleSheet("color:#00c8d7; font-weight:bold;");

    coord_label_ = new QLabel("经纬度: 等待 GPS…", bar);
    coord_label_->setStyleSheet("color:#9fb0d0; font-family: Consolas;");

    follow_check_ = new QCheckBox("跟随", bar);
    follow_check_->setChecked(true);
    follow_check_->setStyleSheet("color:#9fb0d0;");
    connect(follow_check_, &QCheckBox::toggled, this, [this](bool on) {
        follow_ = on;
        if (follow_ && last_octet_ >= 0 && latest_.contains(last_octet_)) {
            const QPointF p = latest_.value(last_octet_);
            center_lng_ = p.x();
            center_lat_ = p.y();
            requestVisibleTiles();
            update();
        }
    });

    auto *btnClear = new QPushButton("清空轨迹", bar);
    btnClear->setFixedHeight(24);
    connect(btnClear, &QPushButton::clicked, this, &MapWidget::clearTracks);

    barLayout->addWidget(title);
    barLayout->addStretch();
    barLayout->addWidget(coord_label_);
    barLayout->addWidget(follow_check_);
    barLayout->addWidget(btnClear);

    outer->addWidget(bar, 0, Qt::AlignTop);
    outer->addStretch();

    requestVisibleTiles();
}

// ── Web-Mercator projection ─────────────────────────────────────────────────
double MapWidget::lonToTileX(double lon, int z)
{
    return (lon + 180.0) / 360.0 * double(1 << z);
}

double MapWidget::latToTileY(double lat, int z)
{
    const double r = lat * kPi / 180.0;
    return (1.0 - std::log(std::tan(r) + 1.0 / std::cos(r)) / kPi) / 2.0 * double(1 << z);
}

QString MapWidget::tileKey(int z, int x, int y)
{
    return QStringLiteral("%1/%2/%3").arg(z).arg(x).arg(y);
}

QString MapWidget::tileDiskPath(int z, int x, int y) const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return QStringLiteral("%1/amap_tiles/%2/%3/%4.png").arg(base).arg(z).arg(x).arg(y);
}

// ── Data feed ───────────────────────────────────────────────────────────────
void MapWidget::setDronePosition(int octet, double lat, double lng)
{
    if (!std::isfinite(lat) || !std::isfinite(lng))
        return;
    if (lat < -90.0 || lat > 90.0 || lng < -180.0 || lng > 180.0)
        return;
    // Reject the null-island cluster (0,0) a drone reports when it has no GPS
    // lock — otherwise 跟随 yanks the view out into the Atlantic.
    if (std::fabs(lat) < 0.001 && std::fabs(lng) < 0.001)
        return;

    // Convert the raw WGS-84 fix to GCJ-02 for plotting on AutoNavi tiles; the
    // HUD still shows the true GPS value below.
    double glat, glng;
    wgs84ToGcj02(lat, lng, glat, glng);

    const QPointF p(glng, glat);
    QVector<QPointF> &track = tracks_[octet];
    // Skip near-duplicate samples so a stationary drone doesn't bloat the path.
    if (track.isEmpty() ||
        std::hypot(track.last().x() - glng, track.last().y() - glat) > 1e-6) {
        track.append(p);
        if (track.size() > 5000)
            track.remove(0, track.size() - 5000);
    }
    latest_[octet] = p;
    last_octet_ = octet;

    if (follow_) {
        center_lat_ = glat;
        center_lng_ = glng;
    }
    updateCoordLabel(octet, lat, lng);   // display true WGS-84 GPS
    requestVisibleTiles();
    update();
}

void MapWidget::clearTracks()
{
    tracks_.clear();
    latest_.clear();
    last_octet_ = -1;
    if (coord_label_)
        coord_label_->setText("经纬度: 等待 GPS…");
    update();
}

void MapWidget::updateCoordLabel(int octet, double lat, double lng)
{
    if (!coord_label_)
        return;
    coord_label_->setText(QStringLiteral(".%1  %2°N, %3°E")
                              .arg(octet)
                              .arg(lat, 0, 'f', 6)
                              .arg(lng, 0, 'f', 6));
}

// ── Tile fetching ───────────────────────────────────────────────────────────
void MapWidget::requestVisibleTiles()
{
    const int W = width(), H = height();
    if (W <= 0 || H <= 0)
        return;

    const double cx = lonToTileX(center_lng_, zoom_) * kTileSize;
    const double cy = latToTileY(center_lat_, zoom_) * kTileSize;
    const double originX = cx - W / 2.0;
    const double originY = cy - H / 2.0;
    const int n = 1 << zoom_;

    const int xStart = int(std::floor(originX / kTileSize));
    const int yStart = int(std::floor(originY / kTileSize));
    const int xEnd   = int(std::floor((originX + W) / kTileSize));
    const int yEnd   = int(std::floor((originY + H) / kTileSize));

    for (int tx = xStart; tx <= xEnd; ++tx) {
        for (int ty = yStart; ty <= yEnd; ++ty) {
            if (ty < 0 || ty >= n)
                continue;
            const int wx = ((tx % n) + n) % n;   // wrap longitude
            const QString key = tileKey(zoom_, wx, ty);
            if (tile_cache_.contains(key) || pending_tiles_.contains(key))
                continue;

            // Disk cache first — avoids re-downloading and works offline.
            const QString disk = tileDiskPath(zoom_, wx, ty);
            if (QFileInfo::exists(disk)) {
                QPixmap pm;
                if (pm.load(disk)) {
                    tile_cache_.insert(key, pm);
                    continue;
                }
            }

            pending_tiles_.insert(key);
            // AutoNavi/高德 raster road tiles (GCJ-02, style=7). Domestic and
            // reachable without the system proxy. Rotate over 4 subdomains.
            const int sub = ((wx + ty) & 3) + 1;
            QUrl url(QStringLiteral(
                         "https://wprd0%1.is.autonavi.com/appmaptile?"
                         "x=%2&y=%3&z=%4&lang=zh_cn&size=1&scl=1&style=7")
                         .arg(sub).arg(wx).arg(ty).arg(zoom_));
            QNetworkRequest req(url);
            req.setRawHeader("User-Agent", "HostGUI/1.0 (UAV control panel)");
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
            QNetworkReply *reply = nam_->get(req);
            reply->setProperty("tileKey", key);
            reply->setProperty("tileZ", zoom_);
            reply->setProperty("tileX", wx);
            reply->setProperty("tileY", ty);
        }
    }
}

void MapWidget::onTileDownloaded(QNetworkReply *reply)
{
    reply->deleteLater();
    const QString key = reply->property("tileKey").toString();
    pending_tiles_.remove(key);

    if (reply->error() != QNetworkReply::NoError)
        return;

    const QByteArray data = reply->readAll();
    QPixmap pm;
    if (!pm.loadFromData(data))
        return;

    tile_cache_.insert(key, pm);

    // Persist to disk cache.
    const int z = reply->property("tileZ").toInt();
    const int x = reply->property("tileX").toInt();
    const int y = reply->property("tileY").toInt();
    const QString disk = tileDiskPath(z, x, y);
    QDir().mkpath(QFileInfo(disk).absolutePath());
    QFile f(disk);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(data);
        f.close();
    }

    update();
}

// ── Painting ────────────────────────────────────────────────────────────────
void MapWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x0e, 0x14, 0x22));

    const int W = width(), H = height();
    const double cx = lonToTileX(center_lng_, zoom_) * kTileSize;
    const double cy = latToTileY(center_lat_, zoom_) * kTileSize;
    const double originX = cx - W / 2.0;
    const double originY = cy - H / 2.0;
    const int n = 1 << zoom_;

    const int xStart = int(std::floor(originX / kTileSize));
    const int yStart = int(std::floor(originY / kTileSize));
    const int xEnd   = int(std::floor((originX + W) / kTileSize));
    const int yEnd   = int(std::floor((originY + H) / kTileSize));

    for (int tx = xStart; tx <= xEnd; ++tx) {
        for (int ty = yStart; ty <= yEnd; ++ty) {
            const double sx = tx * kTileSize - originX;
            const double sy = ty * kTileSize - originY;
            if (ty < 0 || ty >= n) {
                p.fillRect(QRectF(sx, sy, kTileSize, kTileSize), QColor(0x0e, 0x14, 0x22));
                continue;
            }
            const int wx = ((tx % n) + n) % n;
            const auto it = tile_cache_.constFind(tileKey(zoom_, wx, ty));
            if (it != tile_cache_.constEnd()) {
                p.drawPixmap(int(sx), int(sy), it.value());
            } else {
                p.fillRect(QRectF(sx, sy, kTileSize, kTileSize), QColor(0x16, 0x1e, 0x30));
                p.setPen(QColor(0x22, 0x2c, 0x44));
                p.drawRect(QRectF(sx, sy, kTileSize - 1, kTileSize - 1));
            }
        }
    }

    // World (lng,lat) -> screen pixel.
    auto toScreen = [&](double lng, double lat) -> QPointF {
        const double px = lonToTileX(lng, zoom_) * kTileSize - originX;
        const double py = latToTileY(lat, zoom_) * kTileSize - originY;
        return QPointF(px, py);
    };

    p.setRenderHint(QPainter::Antialiasing, true);

    // Trajectories.
    for (auto it = tracks_.constBegin(); it != tracks_.constEnd(); ++it) {
        const QVector<QPointF> &pts = it.value();
        if (pts.size() < 2)
            continue;
        QPolygonF poly;
        poly.reserve(pts.size());
        for (const QPointF &q : pts)
            poly << toScreen(q.x(), q.y());
        QPen pen(nodeColor(it.key()), 2.0);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(poly);
    }

    // Current-position markers — a gray drone icon + node-id label pill.
    for (auto it = latest_.constBegin(); it != latest_.constEnd(); ++it) {
        const QColor col = nodeColor(it.key());
        const QPointF s = toScreen(it.value().x(), it.value().y());

        drawDroneIcon(p, s, 30.0, col);

        const QString label = QStringLiteral("无人机 .%1").arg(it.key());
        const QFontMetrics fm(p.font());
        const qreal pw = fm.horizontalAdvance(label) + 12;
        const qreal ph = fm.height() + 4;
        qreal px = qBound(2.0, s.x() - pw / 2.0, double(width()) - pw - 2.0);
        qreal py = s.y() + 22;   // sit just below the icon
        if (py + ph > height() - 2)
            py = s.y() - 22 - ph; // flip above if near the bottom edge
        const QRectF pill(px, py, pw, ph);
        p.setPen(QPen(col, 1.2));
        p.setBrush(QColor(0x0e, 0x14, 0x22, 210));
        p.drawRoundedRect(pill, 5, 5);
        p.setPen(QColor(0xe5, 0xea, 0xf2));
        p.drawText(pill, Qt::AlignCenter, label);
    }

    // Attribution.
    p.setPen(QColor(0x9f, 0xb0, 0xd0));
    p.drawText(rect().adjusted(6, 0, -6, -4),
               Qt::AlignRight | Qt::AlignBottom, "© 高德地图 AutoNavi");
}

// ── Interaction ─────────────────────────────────────────────────────────────
void MapWidget::wheelEvent(QWheelEvent *e)
{
    const int oldZoom = zoom_;
    if (e->angleDelta().y() > 0)
        zoom_ = qMin(kMaxZoom, zoom_ + 1);
    else
        zoom_ = qMax(kMinZoom, zoom_ - 1);
    if (zoom_ != oldZoom) {
        requestVisibleTiles();
        update();
    }
    e->accept();
}

void MapWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        dragging_ = true;
        drag_last_ = e->pos();
        // A manual pan means the user wants to look around — stop auto-follow.
        if (follow_check_)
            follow_check_->setChecked(false);
    }
}

void MapWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (!dragging_)
        return;
    const QPoint d = e->pos() - drag_last_;
    drag_last_ = e->pos();

    // Convert pixel delta to a lng/lat shift at the current zoom.
    const double worldPx = double(1 << zoom_) * kTileSize;
    center_lng_ -= d.x() / worldPx * 360.0;

    double cyPx = latToTileY(center_lat_, zoom_) * kTileSize - d.y();
    double yTile = cyPx / kTileSize / double(1 << zoom_);
    yTile = qBound(0.0, yTile, 1.0);
    const double ll = kPi * (1.0 - 2.0 * yTile);
    center_lat_ = std::atan(std::sinh(ll)) * 180.0 / kPi;
    center_lng_ = qBound(-180.0, center_lng_, 180.0);

    requestVisibleTiles();
    update();
}

void MapWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        dragging_ = false;
}

void MapWidget::resizeEvent(QResizeEvent *)
{
    requestVisibleTiles();
}
