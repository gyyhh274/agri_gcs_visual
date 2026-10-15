#include "PointCloudView.h"
#include "MappingClient.h"

#include <QLineF>
#include <QMutexLocker>
#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
constexpr qreal pi = 3.14159265358979323846;
constexpr qreal halfFov = 25.0 * pi / 180.0;
constexpr int colorCount = 32;
constexpr int pointBudget = 80000;
bool finite(const QVector3D& p)
{
    return std::isfinite(p.x()) && std::isfinite(p.y()) && std::isfinite(p.z());
}
QColor pointColor(int index)
{
    const qreal t = index / qreal(colorCount - 1);
    if (t < .5)
        return QColor::fromRgbF(.12 + .05 * t, .35 + 1.1 * t, .98 - .25 * t);
    return QColor::fromRgbF(.15 + .45 * (t - .5), .9, .85 - 1.25 * (t - .5));
}
struct Camera {
    QVector3D position, right, up, forward;
    QSizeF size;
    qreal focal = 1, nearPlane = .02;
    Camera(const QVector3D& target, qreal distance, qreal yaw, qreal pitch, QSizeF viewport)
        : size(viewport)
    {
        const qreal y = yaw * pi / 180.0, p = pitch * pi / 180.0;
        forward = QVector3D(-std::cos(p) * std::cos(y), -std::cos(p) * std::sin(y), -std::sin(p));
        right = QVector3D(-std::sin(y), std::cos(y), 0);
        up = QVector3D::crossProduct(right, forward).normalized();
        position = target - forward * distance;
        focal = size.height() * .5 / std::tan(halfFov);
        nearPlane = std::max(.02, distance * .001);
    }
    qreal depth(const QVector3D& point) const { return QVector3D::dotProduct(point - position, forward); }
    QPointF projectUnchecked(const QVector3D& point) const
    {
        const auto delta = point - position;
        const qreal scale = focal / QVector3D::dotProduct(delta, forward);
        return QPointF(size.width() * .5 + QVector3D::dotProduct(delta, right) * scale,
                       size.height() * .5 - QVector3D::dotProduct(delta, up) * scale);
    }
    bool project(const QVector3D& point, QPointF& screen) const
    {
        if (!finite(point) || depth(point) <= nearPlane) return false;
        screen = projectUnchecked(point);
        return screen.x() >= -4 && screen.x() <= size.width() + 4
            && screen.y() >= -4 && screen.y() <= size.height() + 4;
    }
    bool line(const QVector3D& a, const QVector3D& b, QLineF& screen) const
    {
        if (!finite(a) || !finite(b)) return false;
        qreal da = depth(a), db = depth(b);
        if (da <= nearPlane && db <= nearPlane) return false;
        // Clip in camera coordinates; interpolating a world-space float vector
        // near the camera can round its depth to zero on long trajectories.
        const auto ra = a - position, rb = b - position;
        qreal ax = QVector3D::dotProduct(ra, right), ay = QVector3D::dotProduct(ra, up);
        qreal bx = QVector3D::dotProduct(rb, right), by = QVector3D::dotProduct(rb, up);
        if (da <= nearPlane) {
            const qreal t = (nearPlane - da) / (db - da);
            ax += (bx - ax) * t; ay += (by - ay) * t; da = nearPlane;
        } else if (db <= nearPlane) {
            const qreal t = (nearPlane - db) / (da - db);
            bx += (ax - bx) * t; by += (ay - by) * t; db = nearPlane;
        }
        const QPointF first(size.width() * .5 + ax * focal / da, size.height() * .5 - ay * focal / da);
        const QPointF last(size.width() * .5 + bx * focal / db, size.height() * .5 - by * focal / db);
        const qreal dx = last.x() - first.x(), dy = last.y() - first.y();
        if (!std::isfinite(dx) || !std::isfinite(dy)) return false;
        qreal start = 0, end = 1;
        const auto clip = [&](qreal p, qreal q) {
            if (qFuzzyIsNull(p)) return q >= 0;
            const qreal t = q / p;
            if (p < 0) { if (t > end) return false; start = std::max(start, t); }
            else { if (t < start) return false; end = std::min(end, t); }
            return true;
        };
        if (!clip(-dx, first.x() + 2) || !clip(dx, size.width() + 2 - first.x())
            || !clip(-dy, first.y() + 2) || !clip(dy, size.height() + 2 - first.y())) return false;
        screen = QLineF(first + QPointF(dx, dy) * start, first + QPointF(dx, dy) * end);
        return true;
    }
};
qreal gridStep(qreal desired)
{
    const qreal power = std::pow(10.0, std::floor(std::log10(std::max(.01, desired))));
    const qreal scaled = desired / power;
    return power * (scaled < 2 ? 1 : scaled < 5 ? 2 : 5);
}
}

struct PointCloudView::RenderCache {
    quint64 revision = std::numeric_limits<quint64>::max();
    QSizeF size;
    std::array<QVector<QPointF>, colorCount> points;
    QVector<QLineF> grid, majorGrid, trajectory;
    QPolygonF aircraft;
    QPointF aircraftCenter;
    bool hasAircraft = false;
    qreal step = 1;
    std::array<QLineF, 3> compass;
};

PointCloudView::PointCloudView(QQuickItem* parent)
    : QQuickPaintedItem(parent), m_cache(new RenderCache)
{
    setAntialiasing(false);
    setOpaquePainting(true);
    setRenderTarget(QQuickPaintedItem::Image);
    setMipmap(false);
    setFillColor(QColor(QStringLiteral("#071522")));
    setClip(true);
}
PointCloudView::~PointCloudView() = default;
PointCloudView::State PointCloudView::state() const { QMutexLocker lock(&m_mutex); return m_state; }
MappingClient* PointCloudView::source() const { return m_source.data(); }
void PointCloudView::setSource(MappingClient* source)
{
    if (m_source == source) return;
    disconnect(m_snapshotConnection);
    disconnect(m_destroyedConnection);
    m_source = source;
    if (source) {
        m_snapshotConnection = connect(source, &MappingClient::snapshotChanged, this, &PointCloudView::refreshSnapshot);
        m_destroyedConnection = connect(source, &QObject::destroyed, this, [this] {
            m_source = nullptr;
            refreshSnapshot();
            emit changed();
        });
    }
    refreshSnapshot();
    emit changed();
}
void PointCloudView::refreshSnapshot()
{
    const auto snapshot = m_source ? m_source->snapshot() : std::shared_ptr<const MappingSnapshot>();
    { QMutexLocker lock(&m_mutex); m_state.snapshot = snapshot; ++m_state.revision; }
    update();
}
void PointCloudView::viewChanged()
{
    { QMutexLocker lock(&m_mutex); ++m_state.revision; }
    update();
    emit changed();
}
bool PointCloudView::follow() const { return state().follow; }
bool PointCloudView::topDown() const { return state().topDown; }
qreal PointCloudView::distance() const { return state().distance; }
qreal PointCloudView::yaw() const { return state().yaw; }
qreal PointCloudView::pitch() const { return state().pitch; }
int PointCloudView::colorMode() const { return state().colorMode; }
void PointCloudView::setFollow(bool enabled)
{
    if (follow() == enabled) return;
    { QMutexLocker lock(&m_mutex);
      if (!enabled && m_state.snapshot && m_state.snapshot->hasPose)
          m_state.target = m_state.snapshot->position;
      m_state.follow = enabled; }
    viewChanged();
}
void PointCloudView::setTopDown(bool enabled)
{
    if (topDown() == enabled) return;
    { QMutexLocker lock(&m_mutex); m_state.topDown = enabled; }
    viewChanged();
}
void PointCloudView::setDistance(qreal value)
{
    if (!std::isfinite(value)) return;
    value = std::clamp(value, .5, 20000.0);
    if (qFuzzyCompare(distance(), value)) return;
    { QMutexLocker lock(&m_mutex); m_state.distance = value; }
    viewChanged();
}
void PointCloudView::setYaw(qreal value)
{
    if (!std::isfinite(value)) return;
    value = std::remainder(value, 360.0);
    if (qFuzzyCompare(yaw(), value)) return;
    { QMutexLocker lock(&m_mutex); m_state.yaw = value; }
    viewChanged();
}
void PointCloudView::setPitch(qreal value)
{
    if (!std::isfinite(value)) return;
    value = std::clamp(value, 5.0, 89.5);
    if (qFuzzyCompare(pitch(), value)) return;
    { QMutexLocker lock(&m_mutex); m_state.pitch = value; }
    viewChanged();
}
void PointCloudView::setColorMode(int value)
{
    value = std::clamp(value, 0, 1);
    if (colorMode() == value) return;
    { QMutexLocker lock(&m_mutex); m_state.colorMode = value; }
    viewChanged();
}
void PointCloudView::orbit(qreal dx, qreal dy)
{
    if (!std::isfinite(dx) || !std::isfinite(dy)) return;
    { QMutexLocker lock(&m_mutex);
      m_state.yaw = std::remainder(m_state.yaw - dx * .35, 360.0);
      m_state.pitch = std::clamp(m_state.pitch + dy * .35, 5.0, 89.5);
      m_state.topDown = false; }
    viewChanged();
}
void PointCloudView::pan(qreal dx, qreal dy)
{
    if (!std::isfinite(dx) || !std::isfinite(dy) || height() < 1) return;
    { QMutexLocker lock(&m_mutex);
      if (m_state.follow && m_state.snapshot && m_state.snapshot->hasPose)
          m_state.target = m_state.snapshot->position;
      m_state.follow = false;
      const qreal y = m_state.yaw * pi / 180.0;
      const qreal scale = 2 * m_state.distance * std::tan(halfFov) / height();
      const QVector3D right(-std::sin(y), std::cos(y), 0);
      const QVector3D up(-std::cos(y), -std::sin(y), 0);
      m_state.target += (right * (-dx) + up * dy) * scale; }
    viewChanged();
}
void PointCloudView::zoom(qreal steps)
{
    if (std::isfinite(steps)) setDistance(distance() * std::pow(.82, std::clamp(steps, -20.0, 20.0)));
}
void PointCloudView::resetView()
{
    { QMutexLocker lock(&m_mutex);
      const auto snapshot = m_state.snapshot;
      const auto revision = m_state.revision;
      m_state = State(); m_state.snapshot = snapshot; m_state.revision = revision; }
    viewChanged();
}
void PointCloudView::fitMap()
{
    const auto snapshot = state().snapshot;
    if (!snapshot) { resetView(); return; }
    QVector3D low, high;
    bool any = false;
    const auto include = [&](const QVector3D& point) {
        if (!finite(point)) return;
        if (!any) { low = high = point; any = true; return; }
        for (int axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], point[axis]);
            high[axis] = std::max(high[axis], point[axis]);
        }
    };
    for (const auto& p : snapshot->points) include(QVector3D(p.x, p.y, p.z));
    for (const auto& p : snapshot->trajectory) include(p);
    if (snapshot->hasPose) include(snapshot->position);
    if (!any) { resetView(); return; }
    const qreal aspect = std::max(.2, width() / std::max(1.0, height()));
    const qreal angle = std::atan(std::tan(halfFov) * std::min(1.0, aspect));
    { QMutexLocker lock(&m_mutex);
      m_state.target = (low + high) * .5;
      m_state.follow = false;
      m_state.distance = std::clamp(qreal((high - low).length()) * .6 / std::sin(angle), 3.0, 20000.0); }
    viewChanged();
}
void PointCloudView::geometryChanged(const QRectF& next, const QRectF& previous)
{
    QQuickPaintedItem::geometryChanged(next, previous);
    if (next.size() != previous.size()) update();
}

void PointCloudView::paint(QPainter* painter)
{
    const auto view = state();
    const QSizeF viewport(width(), height());
    if (viewport.width() < 1 || viewport.height() < 1) return;
    auto& cache = *m_cache;
    const auto snapshot = view.snapshot;
    QVector3D target = view.target;
    if (view.follow && snapshot && snapshot->hasPose && finite(snapshot->position)) target = snapshot->position;
    const Camera camera(target, view.distance, view.yaw, view.topDown ? 89.5 : view.pitch, viewport);
    if (cache.revision != view.revision || cache.size != viewport) {
        cache.revision = view.revision;
        cache.size = viewport;
        for (auto& batch : cache.points) batch.clear();
        cache.grid.clear(); cache.majorGrid.clear(); cache.trajectory.clear();
        cache.aircraft.clear(); cache.hasAircraft = false;
        qreal low = 0, high = 1;
        if (snapshot && !snapshot->points.isEmpty()) {
            low = std::numeric_limits<qreal>::max();
            high = std::numeric_limits<qreal>::lowest();
            for (const auto& p : snapshot->points) {
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) continue;
                low = std::min(low, qreal(p.z)); high = std::max(high, qreal(p.z));
            }
            if (low > high) { low = 0; high = 1; }
            const qreal range = std::max(.25, high - low);
            const int stride = std::max(1, (snapshot->points.size() + pointBudget - 1) / pointBudget);
            for (int index = 0; index < snapshot->points.size(); index += stride) {
                const auto& p = snapshot->points[index];
                QPointF screen;
                if (!camera.project(QVector3D(p.x, p.y, p.z), screen)) continue;
                const qreal value = view.colorMode == 1
                    ? (std::isfinite(p.intensity) ? p.intensity / 255.0 : 0.0) : (p.z - low) / range;
                const int color = std::clamp(int(value * (colorCount - 1)), 0, colorCount - 1);
                cache.points[color].append(screen);
            }
        }
        cache.step = gridStep(view.distance / 12.0);
        const qreal cx = std::round(target.x() / cache.step) * cache.step;
        const qreal cy = std::round(target.y() / cache.step) * cache.step;
        const qreal extent = cache.step * 24;
        const qreal floor = std::min(0.0, low) - .08;
        for (int i = -24; i <= 24; ++i) {
            const qreal x = cx + i * cache.step, y = cy + i * cache.step;
            QLineF line;
            auto& lines = (i % 5 == 0) ? cache.majorGrid : cache.grid;
            if (camera.line(QVector3D(x, cy - extent, floor), QVector3D(x, cy + extent, floor), line)) lines.append(line);
            if (camera.line(QVector3D(cx - extent, y, floor), QVector3D(cx + extent, y, floor), line)) lines.append(line);
        }
        if (snapshot) {
            const auto& trajectory = snapshot->trajectory;
            const int stride = std::max(1, (trajectory.size() + 4999) / 5000);
            for (int i = stride; i < trajectory.size(); i += stride) {
                QLineF line;
                if (camera.line(trajectory[i - stride], trajectory[i], line)) cache.trajectory.append(line);
            }
            if (trajectory.size() > 1 && (trajectory.size() - 1) % stride != 0) {
                QLineF line;
                const int previous = ((trajectory.size() - 1) / stride) * stride;
                if (camera.line(trajectory[previous], trajectory.last(), line)) cache.trajectory.append(line);
            }
            QPointF center;
            if (snapshot->hasPose && camera.project(snapshot->position, center)) {
                cache.hasAircraft = true;
                cache.aircraftCenter = center;
                const auto forward = snapshot->orientation.rotatedVector(QVector3D(1, 0, 0));
                const QPointF nose = camera.projectUnchecked(snapshot->position + forward * qreal(view.distance * .03));
                QPointF direction = nose - center;
                const qreal length = std::hypot(direction.x(), direction.y());
                if (std::isfinite(length) && length > .01) direction /= length;
                else direction = QPointF(0, -1);
                const QPointF right(-direction.y(), direction.x());
                cache.aircraft << center + direction * 14 << center - direction * 8 + right * 8
                               << center - direction * 4 << center - direction * 8 - right * 8;
            }
        }
        const QPointF anchor(viewport.width() - 45, 65);
        for (int axis = 0; axis < 3; ++axis) {
            QVector3D direction; direction[axis] = 1;
            const QPointF offset(QVector3D::dotProduct(direction, camera.right) * 26,
                                 -QVector3D::dotProduct(direction, camera.up) * 26);
            cache.compass[axis] = QLineF(anchor, anchor + offset);
        }
    }
    painter->fillRect(QRectF(QPointF(0, 0), viewport), QColor(QStringLiteral("#071522")));
    painter->setClipRect(QRectF(QPointF(0, 0), viewport));
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QPen(QColor(QStringLiteral("#142a37")), 1));
    if (!cache.grid.isEmpty()) painter->drawLines(cache.grid.constData(), cache.grid.size());
    painter->setPen(QPen(QColor(QStringLiteral("#203e4b")), 1));
    if (!cache.majorGrid.isEmpty()) painter->drawLines(cache.majorGrid.constData(), cache.majorGrid.size());
    for (int color = 0; color < colorCount; ++color) {
        const auto& points = cache.points[color];
        if (points.isEmpty()) continue;
        QPen pen(pointColor(color), 1.8, Qt::SolidLine, Qt::SquareCap);
        painter->setPen(pen);
        painter->drawPoints(points.constData(), points.size());
    }
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(QColor(QStringLiteral("#f5cf6d")), 2));
    if (!cache.trajectory.isEmpty()) painter->drawLines(cache.trajectory.constData(), cache.trajectory.size());
    if (cache.hasAircraft) {
        painter->setBrush(QColor(13, 233, 225, 30));
        painter->setPen(QPen(QColor(QStringLiteral("#1be0d5")), 1));
        painter->drawEllipse(cache.aircraftCenter, 20, 20);
        painter->setBrush(QColor(QStringLiteral("#d9ffff")));
        painter->setPen(QPen(QColor(QStringLiteral("#0adacc")), 2));
        painter->drawPolygon(cache.aircraft);
    }
    const std::array<QColor, 3> axisColors = { QColor("#ef8080"), QColor("#70d99c"), QColor("#6bb8ff") };
    const std::array<QString, 3> axisLabels = { QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z") };
    for (int axis = 0; axis < 3; ++axis) {
        painter->setPen(QPen(axisColors[axis], 2));
        painter->drawLine(cache.compass[axis]);
        painter->drawText(cache.compass[axis].p2() + QPointF(3, -3), axisLabels[axis]);
    }
    painter->setPen(QColor(QStringLiteral("#83a6b3")));
    painter->drawText(QPointF(viewport.width() - 145, 20), QStringLiteral("%1 m / grid").arg(cache.step, 0, 'g', 3));
}
