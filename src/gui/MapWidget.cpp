#include "MapWidget.h"

#include <algorithm>
#include <cmath>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QFontMetrics>
#include <QComboBox>
#include <QTimer>
#include <QStringList>
#include <QSignalBlocker>
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
void drawDroneIcon(QPainter &p, const QPointF &c, qreal s, const QColor &accent,
                   double heading_deg = 0.0, bool has_heading = false)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(c);
    if (has_heading) p.rotate(heading_deg);   // nose follows the flight path

    // Proportions chosen so the silhouette reads as a quadcopter at ~30px:
    // the motors sit well clear of a SMALL body, and each carries visible
    // propeller blades. The previous version had a large body with faint
    // blur discs overlapping it, which merged into an unreadable blob.
    const qreal d   = s * 0.32;    // motor offset on each axis (45° arms)
    const qreal rot = s * 0.165;   // propeller radius

    const QPointF motors[4] = {
        QPointF(-d, -d), QPointF(d, -d), QPointF(d, d), QPointF(-d, d)
    };

    // ── Ground shadow ────────────────────────────────────────────────
    // Offset down-right: gives the icon lift off the map without a hard
    // outline, which is what made the old flat version look pasted on.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 55));
    p.drawEllipse(QPointF(s * 0.05, s * 0.08), s * 0.50, s * 0.47);

    // ── Arms: tapered, wide at the hub, narrow at the motor ──────────
    const QColor armDark(0x39, 0x41, 0x52);
    const QColor armLite(0x6b, 0x76, 0x8c);
    for (const QPointF &m : motors) {
        const qreal len = std::hypot(m.x(), m.y());
        const qreal ux = m.x() / len, uy = m.y() / len;
        const qreal px = -uy, py = ux;                 // perpendicular
        const qreal wHub = s * 0.075, wTip = s * 0.038;
        QPainterPath arm;
        arm.moveTo(px * wHub, py * wHub);
        arm.lineTo(m.x() + px * wTip, m.y() + py * wTip);
        arm.lineTo(m.x() - px * wTip, m.y() - py * wTip);
        arm.lineTo(-px * wHub, -py * wHub);
        arm.closeSubpath();
        QLinearGradient g(0, 0, m.x(), m.y());
        g.setColorAt(0.0, armLite);
        g.setColorAt(1.0, armDark);
        p.setBrush(g);
        p.setPen(Qt::NoPen);
        p.drawPath(arm);
    }

    // ── Propellers: two visible blades per motor + a faint sweep disc ─
    // Actual blade shapes are what make this read as a quadcopter; a pure
    // blur disc just looks like a dot at map scale.
    for (int i = 0; i < 4; ++i) {
        const QPointF &m = motors[i];
        p.save();
        p.translate(m);

        // Faint swept-disc, so it still suggests rotation.
        p.setPen(QPen(QColor(255, 255, 255, 90), std::max(0.8, s * 0.018)));
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 38));
        p.drawEllipse(QPointF(0, 0), rot, rot);

        // Two blades, offset per motor so the four don't look stamped.
        p.rotate(i * 34.0);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x3c, 0x44, 0x55, 235));
        for (int b = 0; b < 2; ++b) {
            p.drawEllipse(QPointF(0, 0), rot * 0.98, rot * 0.26);
            p.rotate(90.0);
        }
        p.restore();

        // Motor hub on top of the blades.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x22, 0x28, 0x34));
        p.drawEllipse(m, s * 0.055, s * 0.055);
        p.setBrush(accent);
        p.drawEllipse(m, s * 0.028, s * 0.028);
    }

    // ── Body: small rounded shell with a top-lit gradient ────────────
    const qreal bw = s * 0.135, bh = s * 0.175;
    QLinearGradient bg(0, -bh, 0, bh);
    bg.setColorAt(0.0, QColor(0xe8, 0xed, 0xf5));
    bg.setColorAt(0.5, QColor(0xb4, 0xbd, 0xcd));
    bg.setColorAt(1.0, QColor(0x6f, 0x7a, 0x8d));
    p.setPen(QPen(QColor(0x22, 0x28, 0x34), std::max(1.0, s * 0.035)));
    p.setBrush(bg);
    QPainterPath body;
    body.addRoundedRect(QRectF(-bw, -bh, bw * 2, bh * 2), bw * 0.75, bw * 0.75);
    p.drawPath(body);

    // Accent stripe across the shell — the per-node identity colour.
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawRoundedRect(QRectF(-bw * 0.78, -s * 0.026, bw * 1.56, s * 0.052),
                      s * 0.026, s * 0.026);

    // ── Nose marker: forward direction ───────────────────────────────
    QPainterPath nose;
    nose.moveTo(0, -bh - s * 0.085);
    nose.lineTo(-s * 0.058, -bh + s * 0.015);
    nose.lineTo(s * 0.058, -bh + s * 0.015);
    nose.closeSubpath();
    p.setBrush(QColor(0xff, 0x3b, 0x30));
    p.drawPath(nose);

    // Camera/gimbal bump under the nose, sells the top-down read.
    p.setBrush(QColor(0x33, 0x3a, 0x48));
    p.drawEllipse(QPointF(0, bh * 0.55), s * 0.045, s * 0.045);

    p.restore();
}
// 大疆机场 (DJI Dock), top view: a squat weatherproof enclosure with its
// bi-fold roof open and the landing pad exposed.
void drawDockIcon(QPainter &p, const QPointF &c, qreal s, const QColor &accent)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(c);

    const qreal h = s * 0.5;

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 55));
    p.drawRoundedRect(QRectF(-h + s * 0.05, -h + s * 0.09, s, s), s * 0.14, s * 0.14);

    // Enclosure shell
    QLinearGradient bg(0, -h, 0, h);
    bg.setColorAt(0.0, QColor(0xf0, 0xf3, 0xf8));
    bg.setColorAt(1.0, QColor(0x8c, 0x96, 0xa8));
    p.setPen(QPen(QColor(0x22, 0x28, 0x34), std::max(1.0, s * 0.05)));
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(-h, -h, s, s), s * 0.14, s * 0.14);

    // Opened roof halves, hinged left and right.
    p.setBrush(QColor(0x5b, 0x66, 0x7a));
    p.drawRoundedRect(QRectF(-h, -h, s * 0.22, s), s * 0.06, s * 0.06);
    p.drawRoundedRect(QRectF(h - s * 0.22, -h, s * 0.22, s), s * 0.06, s * 0.06);

    // Landing pad: dark disc with the accent "H".
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x1b, 0x22, 0x30));
    p.drawEllipse(QPointF(0, 0), s * 0.26, s * 0.26);
    QPen hp(accent, std::max(1.4, s * 0.075));
    hp.setCapStyle(Qt::RoundCap);
    p.setPen(hp);
    p.drawLine(QPointF(-s * 0.10, -s * 0.13), QPointF(-s * 0.10, s * 0.13));
    p.drawLine(QPointF(s * 0.10, -s * 0.13), QPointF(s * 0.10, s * 0.13));
    p.drawLine(QPointF(-s * 0.10, 0), QPointF(s * 0.10, 0));

    p.restore();
}

// 无人小车 (UGV), top view: body, wheels, sensor mast.
void drawCarIcon(QPainter &p, const QPointF &c, qreal s, const QColor &accent,
                 double heading_deg, bool has_heading)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(c);
    if (has_heading) p.rotate(heading_deg);

    const qreal bw = s * 0.30;   // half width
    const qreal bl = s * 0.46;   // half length

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 55));
    p.drawRoundedRect(QRectF(-bw + s * 0.05, -bl + s * 0.08, bw * 2, bl * 2),
                      s * 0.10, s * 0.10);

    // Wheels first so the body overlaps them.
    p.setBrush(QColor(0x24, 0x29, 0x33));
    const qreal ww = s * 0.10, wl = s * 0.16;
    for (int sx : {-1, 1}) {
        for (int sy : {-1, 1}) {
            p.drawRoundedRect(QRectF(sx * bw - (sx > 0 ? 0 : ww), sy * bl * 0.55 - wl / 2,
                                     ww, wl), s * 0.04, s * 0.04);
        }
    }

    QLinearGradient bg(0, -bl, 0, bl);
    bg.setColorAt(0.0, QColor(0xe8, 0xed, 0xf5));
    bg.setColorAt(1.0, QColor(0x77, 0x82, 0x95));
    p.setPen(QPen(QColor(0x22, 0x28, 0x34), std::max(1.0, s * 0.045)));
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(-bw, -bl, bw * 2, bl * 2), s * 0.11, s * 0.11);

    // Windscreen band + accent stripe
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x3d, 0x4a, 0x60));
    p.drawRoundedRect(QRectF(-bw * 0.78, -bl * 0.62, bw * 1.56, bl * 0.42),
                      s * 0.04, s * 0.04);
    p.setBrush(accent);
    p.drawRoundedRect(QRectF(-bw * 0.78, bl * 0.18, bw * 1.56, s * 0.08),
                      s * 0.04, s * 0.04);

    // LiDAR mast
    p.setBrush(QColor(0x1b, 0x22, 0x30));
    p.drawEllipse(QPointF(0, -bl * 0.12), s * 0.085, s * 0.085);
    p.setBrush(accent);
    p.drawEllipse(QPointF(0, -bl * 0.12), s * 0.04, s * 0.04);

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

    title_label_ = new QLabel("无人机地图", bar);
    title_label_->setStyleSheet("color:#00c8d7; font-weight:bold;");

    // 城市切换. Index order must match kCities below.
    city_combo_ = new QComboBox(bar);
    city_combo_->addItem(QStringLiteral("福州"));
    city_combo_->addItem(QStringLiteral("泉州"));
    city_combo_->setFixedWidth(74);
    city_combo_->setStyleSheet(
        "QComboBox { color:#e5eaf2; background:#16203a; border:1px solid #2b3550;"
        "            border-radius:3px; padding:1px 6px; }");
    connect(city_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MapWidget::setCity);

    phase_label_ = new QLabel(QString(), bar);
    phase_label_->setStyleSheet("color:#7f8aa3; font-family: Consolas;");

    sim_check_ = new QCheckBox(QStringLiteral("仿真"), bar);
    sim_check_->setStyleSheet("color:#9fb0d0;");
    sim_check_->setToolTip(QStringLiteral(
        "在当前城市上空模拟 3 台无人机飞行。\n"
        "仿真位置走的是和真实遥测完全相同的通路 (setDronePosition)，\n"
        "所以 GCJ-02 纠偏、轨迹累积、绘制都是真的在跑。"));
    connect(sim_check_, &QCheckBox::toggled, this, &MapWidget::setSimulation);

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

    barLayout->addWidget(title_label_);
    barLayout->addWidget(city_combo_);
    barLayout->addStretch();
    barLayout->addWidget(phase_label_);
    barLayout->addWidget(coord_label_);
    barLayout->addWidget(sim_check_);
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
    latest_wgs_[octet] = QPointF(lng, lat);   // true GPS, for the HUD label
    last_octet_ = octet;

    if (follow_) {
        center_lat_ = glat;
        center_lng_ = glng;
    }
    updateCoordLabel(octet, lat, lng);   // display true WGS-84 GPS
    requestVisibleTiles();
    update();
}

namespace {
// WGS-84 operating sites. Index order matches the 城市 combo.
//
// 泉州 is not the city centre but the actual deployment site — 惠安县溪东
// 工业区 — because that is where the hardware lives and the whole fleet
// (2 docks, 3 aircraft, 1 UGV) is co-located there within a few hundred
// metres. Zoom is correspondingly tighter so a 300 m operating radius
// fills the view instead of being a dot.
struct CityDef { const char *name; const char *site; double lat, lng; int zoom; };
const CityDef kCities[] = {
    { "福州", "福州市区",             26.0745, 119.2965, 12 },
    { "泉州", "惠安县溪东工业区",      24.973997, 118.755872, 16 },
};
const int kCityCount = int(sizeof(kCities) / sizeof(kCities[0]));

// Metre → degree helpers at a given latitude.
inline double mToLat(double m)             { return m / 111320.0; }
inline double mToLng(double m, double lat) { return m / (111320.0 * std::cos(lat * kPi / 180.0)); }

// Total length of a closed polyline.
double routeLength(const QVector<QPointF> &r)
{
    double L = 0;
    for (int i = 0; i < r.size(); ++i) {
        const QPointF &a = r[i], &b = r[(i + 1) % r.size()];
        L += std::hypot(b.x() - a.x(), b.y() - a.y());
    }
    return L;
}

// Point at arclength `s` along a closed polyline, plus the leg heading.
// Straight legs with hard corners are what a flight controller actually
// flies between waypoints; interpolating a circle would hide that.
QPointF routePoint(const QVector<QPointF> &r, double s, double *heading_deg = nullptr)
{
    if (r.isEmpty()) return QPointF(0, 0);
    const double L = routeLength(r);
    if (L <= 0) return r.first();
    s = std::fmod(std::fmod(s, L) + L, L);
    for (int i = 0; i < r.size(); ++i) {
        const QPointF &a = r[i], &b = r[(i + 1) % r.size()];
        const double seg = std::hypot(b.x() - a.x(), b.y() - a.y());
        if (s <= seg || i == r.size() - 1) {
            const double t = (seg > 0) ? (s / seg) : 0.0;
            if (heading_deg) {
                // Screen/compass heading: 0 = north, clockwise.
                *heading_deg = std::fmod(
                    std::atan2(b.x() - a.x(), b.y() - a.y()) * 180.0 / kPi + 360.0, 360.0);
            }
            return QPointF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t);
        }
        s -= seg;
    }
    return r.first();
}

// Aircraft do not fly the polyline they are given. Two things bend it, and
// both are modelled here because without them the track looks CAD-drawn:
//
//  1. Corners. A multirotor decelerates and arcs through a waypoint rather
//     than pivoting on it. Averaging several samples spread along the route
//     rounds the corner by roughly the sample span, which is exactly the
//     turn radius a real controller produces — and it costs nothing on the
//     straight legs, where the samples all lie on the same line.
QPointF routePointSmoothed(const QVector<QPointF> &r, double s, double span,
                           double *heading_deg = nullptr)
{
    constexpr int N = 7;
    double sx = 0, sy = 0;
    for (int i = 0; i < N; ++i) {
        const double off = span * (double(i) / (N - 1) - 0.5);
        const QPointF q = routePoint(r, s + off);
        sx += q.x(); sy += q.y();
    }
    const QPointF now(sx / N, sy / N);
    if (heading_deg) {
        // Take the heading from the smoothed path too, else the icon snaps
        // round at each corner while the track curves.
        double ax = 0, ay = 0;
        for (int i = 0; i < N; ++i) {
            const double off = span * (double(i) / (N - 1) - 0.5);
            const QPointF q = routePoint(r, s + off + 6.0);
            ax += q.x(); ay += q.y();
        }
        const QPointF nxt(ax / N, ay / N);
        *heading_deg = std::fmod(
            std::atan2(nxt.x() - now.x(), nxt.y() - now.y()) * 180.0 / kPi + 360.0, 360.0);
    }
    return now;
}

//  2. Wander. Wind, station-keeping corrections and GPS noise push the
//     aircraft off the commanded line by a few metres, continuously. Built
//     from incommensurate sines rather than random draws so the motion is
//     smooth and repeatable — random jitter at 2 Hz reads as a glitching
//     marker, not as flight.
QPointF flightWander(double t, double seed)
{
    const double a = 4.2 * std::sin(t * 0.21 + seed)
                   + 2.1 * std::sin(t * 0.53 + seed * 2.7)
                   + 0.8 * std::sin(t * 1.31 + seed * 4.1);
    const double b = 3.8 * std::sin(t * 0.17 + seed * 1.9)
                   + 1.9 * std::sin(t * 0.61 + seed * 3.3)
                   + 0.7 * std::sin(t * 1.19 + seed * 5.7);
    return QPointF(a, b);
}

// Lawnmower (boustrophedon) survey box — the standard mapping pattern:
// parallel passes joined by short cross-legs at alternating ends.
QVector<QPointF> lawnmower(double cx, double cy, double w, double h, int passes)
{
    QVector<QPointF> r;
    const double step = (passes > 1) ? h / (passes - 1) : 0.0;
    for (int i = 0; i < passes; ++i) {
        const double y = cy - h / 2 + step * i;
        if (i % 2 == 0) { r << QPointF(cx - w / 2, y) << QPointF(cx + w / 2, y); }
        else            { r << QPointF(cx + w / 2, y) << QPointF(cx - w / 2, y); }
    }
    // Return leg down the side so the loop closes without retracing passes.
    r << QPointF(cx + w / 2 + 18, cy + h / 2 + 18)
      << QPointF(cx + w / 2 + 18, cy - h / 2 - 18);
    return r;
}
} // namespace

void MapWidget::setCity(int index)
{
    if (index < 0 || index >= kCityCount) return;
    const CityDef &c = kCities[index];
    center_lat_ = c.lat;
    center_lng_ = c.lng;
    zoom_       = c.zoom;
    if (title_label_)
        title_label_->setText(QStringLiteral("无人机地图 · %1")
                                  .arg(QString::fromUtf8(c.site)));

    // Old tracks belong to the previous city; leaving them would draw
    // polylines stretching hundreds of km across the new view.
    tracks_.clear();
    latest_.clear();
    latest_wgs_.clear();
    last_octet_ = -1;

    if (sim_on_) {
        setSimulation(false);       // rebuild orbits around the new centre
        setSimulation(true);
    }
    requestVisibleTiles();
    update();
}

void MapWidget::setSimulation(bool on)
{
    sim_on_ = on;
    if (sim_check_ && sim_check_->isChecked() != on) {
        const QSignalBlocker b(sim_check_);
        sim_check_->setChecked(on);
    }

    if (!on) {
        if (sim_timer_) sim_timer_->stop();
        sim_drones_.clear();
        sites_.clear();
        kind_of_.clear();
        leader_route_.clear();
        if (phase_label_) phase_label_->clear();
        info_alt_.clear(); info_spd_.clear(); info_bat_.clear(); info_hdg_.clear();
        return;
    }

    const int ci = city_combo_ ? city_combo_->currentIndex() : 0;
    const CityDef &c = kCities[qBound(0, ci, kCityCount - 1)];

    home_lat_ = c.lat;
    home_lng_ = c.lng;
    sim_clock_s_ = 0.0;
    leader_s_ = 0.0;
    formation_ = true;

    // Operating radius. Everything below is in metres from HOME and stays
    // inside this, per the site brief.
    const double R = 300.0;

    // Leader patrol: an octagonal circuit, i.e. straight legs and turns —
    // the shape a waypoint mission actually produces.
    leader_route_ = {
        {-0.62 * R, -0.48 * R}, { 0.62 * R, -0.48 * R}, { 0.86 * R, -0.10 * R},
        { 0.86 * R,  0.30 * R}, { 0.40 * R,  0.72 * R}, {-0.40 * R,  0.72 * R},
        {-0.86 * R,  0.30 * R}, {-0.86 * R, -0.10 * R},
    };

    // Each aircraft's own survey box for the dispersed phase — three
    // non-overlapping sectors so the tracks stay separable.
    sim_drones_.clear();
    // Search sectors: irregular hexagons, NOT lawnmower boxes. A lawnmower
    // folds back through 180° at the end of every pass, and even after corner
    // smoothing those hairpins read as machine-drawn. These were generated
    // and checked so that no course change exceeds 72° and the edge lengths
    // differ by 2-3×, which is what an actual patrol track looks like.
    struct Spec { int octet; const char *name; QVector<QPointF> route;
                  double spd; double alt; int bat; QPointF form; };
    const Spec kD[] = {
        { 102, "无人机 .102",
          {{-74,165},{-130,194},{-184,152},{-182,32},{-130,2},{-51,70}},
          3.25, 118.0, 92, QPointF(0, 0) },          // leader
        { 103, "无人机 .103",
          {{208,111},{144,175},{93,157},{86,61},{150,24},{194,62}},
          3.25,  96.0, 87, QPointF(-38, -46) },      // left wing
        { 104, "无人机 .104",
          {{67,-95},{15,-56},{-74,-116},{-78,-160},{-31,-222},{30,-210}},
          3.25, 143.0, 95, QPointF( 38, -46) },      // right wing
    };
    for (const Spec &sp : kD) {
        SimDrone d;
        d.octet = sp.octet;
        d.kind  = AssetDrone;
        d.name  = QString::fromUtf8(sp.name);
        d.route = sp.route;
        d.speed_mps = sp.spd;
        d.alt_m = sp.alt;
        d.battery_pct = sp.bat;
        d.form_offset = sp.form;
        sim_drones_ << d;
    }

    // 无人小车 .101 — ground vehicle, slow, on a short service loop between
    // the two docks. Not part of the formation.
    {
        SimDrone car;
        car.octet = 101;
        car.kind  = AssetCar;
        car.name  = QStringLiteral("无人小车 .101");
        // Six unequal legs, every course change ≤ 70° — verified, so the
        // track never shows a right angle. Real roads are neither a smooth
        // loop nor a grid; equal-length right-angle legs read as machine-made.
        //   leg lengths ≈ 127 / 152 / 66 / 124 / 155 / 109 m
        car.route = {
            {131, 94}, {4, 107}, {-91, -12}, {-80, -77}, {39, -110}, {159, -11},
        };
        car.speed_mps = 0.5;                     // 半步行速度
        car.alt_m = 0.0;
        car.battery_pct = 74;
        sim_drones_ << car;
    }

    // 2 × 大疆机场 (.105 / .106) — the fixed sites the aircraft launch from.
    sites_ = {
        { QStringLiteral("大疆机场 .105"),
          c.lat + mToLat(-0.55 * R), c.lng + mToLng(-0.62 * R, c.lat) },
        { QStringLiteral("大疆机场 .106"),
          c.lat + mToLat( 0.48 * R), c.lng + mToLng( 0.66 * R, c.lat) },
    };
    kind_of_.clear();
    for (const SimDrone &d : sim_drones_) kind_of_[d.octet] = d.kind;

    if (!sim_timer_) {
        sim_timer_ = new QTimer(this);
        sim_timer_->setInterval(500);           // 2 Hz — smooth without churn
        connect(sim_timer_, &QTimer::timeout, this, &MapWidget::onSimTick);
    }
    // Follow would chase one drone around its orbit and make the map lurch;
    // a fixed city view is what actually shows "distribution".
    if (follow_check_) follow_check_->setChecked(false);
    center_lat_ = c.lat;
    center_lng_ = c.lng;
    requestVisibleTiles();
    sim_timer_->start();
    onSimTick();                                 // paint immediately
}

void MapWidget::onSimTick()
{
    const double dt = sim_timer_ ? sim_timer_->interval() / 1000.0 : 0.5;
    sim_clock_s_ += dt;

    // ── 编队 / 散开 循环 ──────────────────────────────────────────────
    // 40 s flying the shared leader route in a V, then 60 s split to
    // individual survey boxes, repeating. The transition is a hard switch
    // by design: aircraft peel off to their own tasks, they do not morph.
    constexpr double kFormS = 40.0, kDispS = 60.0;
    const double cyc = std::fmod(sim_clock_s_, kFormS + kDispS);
    const bool was_formation = formation_;
    formation_ = (cyc < kFormS);
    if (formation_ != was_formation) {
        // Re-seed each aircraft's own-route position from where it actually
        // is, so rejoining/leaving does not teleport it across the site.
        for (SimDrone &d : sim_drones_)
            if (d.kind == AssetDrone) d.s = std::fmod(leader_s_, routeLength(d.route));
    }
    if (phase_label_) {
        phase_label_->setText(formation_ ? QStringLiteral("编队巡航")
                                         : QStringLiteral("分区搜索"));
        phase_label_->setStyleSheet(
            formation_ ? "color:#22d3ee; font-family: Consolas; font-weight:bold;"
                       : "color:#facc15; font-family: Consolas; font-weight:bold;");
    }

    // Leader advances regardless — it is the formation's reference path.
    double lead_hdg = 0.0;
    leader_s_ += 3.25 * dt;  // 与三机航速一致
    // 40 m smoothing span ≈ the turn radius a multirotor flies at 6.5 m/s.
    const QPointF lead = routePointSmoothed(leader_route_, leader_s_, 40.0, &lead_hdg);
    const double ch = std::cos(lead_hdg * kPi / 180.0);
    const double sh = std::sin(lead_hdg * kPi / 180.0);

    for (SimDrone &d : sim_drones_) {
        QPointF pos;       // metres from HOME
        double  hdg = 0.0;

        if (d.kind == AssetDrone && formation_) {
            // Offset is expressed in the leader's frame, so the V stays
            // oriented with the flight direction through every turn.
            const QPointF &o = d.form_offset;
            pos = QPointF(lead.x() + o.x() * ch + o.y() * sh,
                          lead.y() - o.x() * sh + o.y() * ch);
            hdg = lead_hdg;
        } else {
            d.s += d.speed_mps * dt;
            // The car is on roads: keep its corners sharp. Aircraft get the
            // rounded-corner treatment.
            pos = (d.kind == AssetCar)
                      ? routePoint(d.route, d.s, &hdg)
                      : routePointSmoothed(d.route, d.s, 40.0, &hdg);
        }

        // Wind / station-keeping / GPS wander — aircraft only. A ground
        // vehicle on a road does not drift sideways by metres.
        if (d.kind != AssetCar) {
            const QPointF w = flightWander(sim_clock_s_, d.octet * 1.7);
            pos += w;
        }

        const double lat = home_lat_ + mToLat(pos.y());
        const double lng = home_lng_ + mToLng(pos.x(), home_lat_);

        // Ground speed from the actual step so the HUD matches the marker.
        if (d.has_last) {
            const double dy = (lat - d.last_lat) * 111320.0;
            const double dx = (lng - d.last_lng) * 111320.0
                              * std::cos(lat * kPi / 180.0);
            info_spd_[d.octet] = std::hypot(dx, dy) / dt;
        } else {
            info_spd_[d.octet] = d.speed_mps;
        }
        d.last_lat = lat; d.last_lng = lng; d.has_last = true;

        info_hdg_[d.octet] = hdg;
        info_alt_[d.octet] = (d.kind == AssetCar)
                                 ? 0.0
                                 : d.alt_m + 6.0 * std::sin(sim_clock_s_ * 0.35
                                                            + d.octet);
        // ~1% per 90 s so a long demo shows the number moving.
        info_bat_[d.octet] = qMax(15, d.battery_pct - int(sim_clock_s_ / 90.0));

        setDronePosition(d.octet, lat, lng);
    }
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

    // ── 控制中心 ──────────────────────────────────────────────────────
    // A fixed red beacon at the city centre — the ground station the drones
    // are dispatched from. Drawn before the drones so their icons and labels
    // stay on top when a flight path passes over it.
    {
        const int ci = city_combo_ ? city_combo_->currentIndex() : 0;
        const CityDef &cc = kCities[qBound(0, ci, kCityCount - 1)];
        double glat, glng;
        wgs84ToGcj02(cc.lat, cc.lng, glat, glng);
        const QPointF cs = toScreen(glng, glat);

        p.setRenderHint(QPainter::Antialiasing, true);
        // Halo rings — reads as a coverage/dispatch origin rather than just
        // another waypoint dot.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xff, 0x3b, 0x30, 34));
        p.drawEllipse(cs, 26.0, 26.0);
        p.setBrush(QColor(0xff, 0x3b, 0x30, 60));
        p.drawEllipse(cs, 15.0, 15.0);
        // Solid core with a white rim so it survives on dark map areas.
        p.setPen(QPen(QColor(255, 255, 255, 235), 2.0));
        p.setBrush(QColor(0xff, 0x3b, 0x30));
        p.drawEllipse(cs, 6.5, 6.5);

        const QString ctext = QStringLiteral("控制中心 · %1").arg(QString::fromUtf8(cc.name));
        QFont cf = p.font();
        cf.setBold(true);
        p.setFont(cf);
        const QFontMetrics cfm(cf);
        const qreal cw = cfm.horizontalAdvance(ctext) + 12;
        const qreal ch = cfm.height() + 4;
        const QRectF cpill(qBound(2.0, cs.x() - cw / 2.0, double(width()) - cw - 2.0),
                           cs.y() - 34 - ch, cw, ch);
        p.setPen(QPen(QColor(0xff, 0x3b, 0x30), 1.2));
        p.setBrush(QColor(0x0e, 0x14, 0x22, 225));
        p.drawRoundedRect(cpill, 5, 5);
        p.setPen(QColor(0xff, 0x3b, 0x30));
        p.drawText(cpill, Qt::AlignCenter, ctext);
    }

    // ── 大疆机场 ──────────────────────────────────────────────────────
    // Static sites, drawn under the moving assets.
    for (const SiteMarker &st : sites_) {
        double glat, glng;
        wgs84ToGcj02(st.lat, st.lng, glat, glng);
        const QPointF ss = toScreen(glng, glat);
        drawDockIcon(p, ss, 30.0, QColor("#facc15"));

        const QString t = QStringLiteral("%1\nN %2  E %3")
                              .arg(st.name)
                              .arg(st.lat, 0, 'f', 6).arg(st.lng, 0, 'f', 6);
        QFont sf = p.font();
        sf.setFamily(QStringLiteral("Consolas"));
        sf.setBold(true);
        sf.setPointSizeF(qMax(7.5, sf.pointSizeF() - 0.5));
        p.setFont(sf);
        const QFontMetrics sfm(sf);
        const QStringList sl = t.split(QChar('\n'));
        qreal sw = 0;
        for (const QString &l : sl) sw = qMax(sw, qreal(sfm.horizontalAdvance(l)));
        sw += 12;
        const qreal sh = sfm.height() * sl.size() + 6;
        qreal sx = qBound(2.0, ss.x() - sw / 2.0, double(width()) - sw - 2.0);
        qreal sy = ss.y() + 20;
        if (sy + sh > height() - 2) sy = ss.y() - 20 - sh;
        const QRectF spill(sx, sy, sw, sh);
        p.setPen(QPen(QColor("#facc15"), 1.2));
        p.setBrush(QColor(0x0e, 0x14, 0x22, 225));
        p.drawRoundedRect(spill, 5, 5);
        p.setPen(QColor(0xff, 0x3b, 0x30));
        p.drawText(spill.adjusted(6, 3, -6, -3), Qt::AlignLeft | Qt::AlignTop, t);
    }

    // Current-position markers — a gray drone icon + node-id label pill.
    for (auto it = latest_.constBegin(); it != latest_.constEnd(); ++it) {
        const QColor col = nodeColor(it.key());
        const QPointF s = toScreen(it.value().x(), it.value().y());

        const int kind = kind_of_.value(it.key(), AssetDrone);
        const double hdg = info_hdg_.value(it.key(), 0.0);
        const bool has_hdg = info_hdg_.contains(it.key());
        if (kind == AssetCar) drawCarIcon(p, s, 30.0, col, hdg, has_hdg);
        else                  drawDroneIcon(p, s, 34.0, col, hdg, has_hdg);

        // Multi-line red HUD: node id, true WGS-84 fix, and whatever
        // telemetry we have. The coordinates printed are the RAW GPS values,
        // not the GCJ-02 ones used to place the marker — those differ by a
        // few hundred metres and quoting them as "GPS" would be a lie.
        const int octet = it.key();
        const QPointF wgs = latest_wgs_.value(octet, it.value());
        QStringList lines;
        lines << (kind == AssetCar ? QStringLiteral("无人小车 .%1").arg(octet)
                                   : QStringLiteral("无人机 .%1").arg(octet));
        lines << QStringLiteral("N %1  E %2")
                     .arg(wgs.y(), 0, 'f', 6).arg(wgs.x(), 0, 'f', 6);
        QStringList tel;
        if (info_alt_.contains(octet))
            tel << QStringLiteral("H %1m").arg(info_alt_.value(octet), 0, 'f', 1);
        if (info_spd_.contains(octet))
            tel << QStringLiteral("V %1m/s").arg(info_spd_.value(octet), 0, 'f', 1);
        if (info_hdg_.contains(octet))
            tel << QStringLiteral("%1°").arg(info_hdg_.value(octet), 0, 'f', 0);
        if (info_bat_.contains(octet))
            tel << QStringLiteral("%1%").arg(info_bat_.value(octet));
        if (!tel.isEmpty()) lines << tel.join(QStringLiteral("  "));

        QFont hud = p.font();
        hud.setFamily(QStringLiteral("Consolas"));
        hud.setBold(true);
        hud.setPointSizeF(qMax(7.5, hud.pointSizeF() - 0.5));
        p.setFont(hud);
        const QFontMetrics fm(hud);

        qreal pw = 0;
        for (const QString &l : lines) pw = qMax(pw, qreal(fm.horizontalAdvance(l)));
        pw += 12;
        const qreal ph = fm.height() * lines.size() + 6;
        qreal px = qBound(2.0, s.x() - pw / 2.0, double(width()) - pw - 2.0);
        qreal py = s.y() + 22;                       // below the icon
        if (py + ph > height() - 2) py = s.y() - 22 - ph;   // flip if clipped
        const QRectF pill(px, py, pw, ph);

        // Dark backing plate: red on aerial imagery is unreadable without it.
        p.setPen(QPen(col, 1.2));
        p.setBrush(QColor(0x0e, 0x14, 0x22, 225));
        p.drawRoundedRect(pill, 5, 5);

        p.setPen(QColor(0xff, 0x3b, 0x30));          // 红色
        p.drawText(pill.adjusted(6, 3, -6, -3),
                   Qt::AlignLeft | Qt::AlignTop, lines.join(QChar('\n')));
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
