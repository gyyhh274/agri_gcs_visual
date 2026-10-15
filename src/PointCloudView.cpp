#include "PointCloudView.h"
#include "MappingClient.h"
#include "PointCloudGpuItem.h"
#include "PointCloudGeometry.h"
#include <QGuiApplication>
#include <QQuickWindow>

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
    const QColor low = QColor::fromRgbF(.015, .14, .48);
    const QColor mid = QColor::fromRgbF(.025, .49, .94);
    const QColor high = QColor::fromRgbF(.025, .90, .79);
    const QColor a = t < .6 ? low : mid, b = t < .6 ? mid : high;
    const qreal f = t < .6 ? t/.6 : (t-.6)/.4;
    return QColor::fromRgbF(a.redF()+(b.redF()-a.redF())*f,
                            a.greenF()+(b.greenF()-a.greenF())*f,
                            a.blueF()+(b.blueF()-a.blueF())*f);
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
    m_followTimer.setInterval(16);
    connect(&m_followTimer, &QTimer::timeout, this, [this] {
        const auto view = state();
        if (!view.follow || !view.snapshot || !view.snapshot->hasPose) { m_followTimer.stop(); return; }
        const auto delta = view.snapshot->position - view.target;
        if (delta.lengthSquared() < .000001f) { m_followTimer.stop(); return; }
        { QMutexLocker lock(&m_mutex); m_state.target += delta * .14f; }
        viewChanged();
    });
    if (QGuiApplication::platformName() != QStringLiteral("offscreen")
            && QQuickWindow::sceneGraphBackend() != QStringLiteral("software")
            && qgetenv("QT_QUICK_BACKEND") != "software") {
        m_gpu = new PointCloudGpuItem(this);
        m_gpuActive = true;
    }
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
    if (snapshot && snapshot->hasPose && follow() && !m_followTimer.isActive()) m_followTimer.start();
    update();
    if (m_gpu) m_gpu->update();
}
void PointCloudView::viewChanged()
{
    { QMutexLocker lock(&m_mutex); ++m_state.revision; }
    update();
    if (m_gpu) m_gpu->update();
    emit changed();
}
bool PointCloudView::depthShading() const { return state().depthShading; }
bool PointCloudView::glow() const { return state().glow; }
qreal PointCloudView::pointSize() const { return state().pointSize; }
void PointCloudView::setGlow(bool enabled) {
    if (glow() == enabled) return;
    { QMutexLocker lock(&m_mutex); m_state.glow = enabled; }
    viewChanged();
}
void PointCloudView::setPointSize(qreal value) {
    if (!std::isfinite(value)) return;
    value = std::clamp(value, .8, 4.0);
    if (qFuzzyCompare(value, pointSize())) return;
    { QMutexLocker lock(&m_mutex); m_state.pointSize = value; }
    viewChanged();
}
bool PointCloudView::cutaway() const { return state().cutaway; }
void PointCloudView::setCutaway(bool value) {
    if (cutaway() == value) return;
    { QMutexLocker lock(&m_mutex); m_state.cutaway = value; }
    viewChanged();
}
void PointCloudView::setDepthShading(bool value) {
    if (depthShading() == value) return;
    { QMutexLocker lock(&m_mutex); m_state.depthShading = value; }
    viewChanged();
}
void PointCloudView::disableGpu() {
    if (!m_gpuActive) return;
    m_gpuActive = false;
    if (m_gpu) m_gpu->setVisible(false);
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
      m_state.follow = enabled; }
    if (enabled) m_followTimer.start();
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
void PointCloudView::focusVehicle()
{
    const auto view = state();
    if (!view.snapshot || !view.snapshot->hasPose) { fitMap(); return; }
    { QMutexLocker lock(&m_mutex);
      m_state.target = view.snapshot->position;
      m_state.follow = true;
      m_state.distance = std::min(m_state.distance, 12.0); }
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
    // Fit the central 98% of samples so isolated range returns do not make
    // the useful room occupy only a few pixels. All points remain rendered.
    const auto bounds = cloudBounds(snapshot->points);
    if (bounds.valid) { low = bounds.low; high = bounds.high; any = true; }
    const auto current = state();
    if (current.cutaway && any) high.setZ(low.z()+(high.z()-low.z())*.65f);
    for (const auto& p : snapshot->trajectory) include(p);
    if (snapshot->hasPose) include(snapshot->position);
    if (!any) { resetView(); return; }
    const qreal aspect = std::max(.2, width() / std::max(1.0, height()));
    const auto view = state();
    const QVector3D center = (low + high) * .5;
    const Camera camera(center, 1, view.yaw, view.topDown ? 89.5 : view.pitch, QSizeF(width(), height()));
    qreal fittedDistance = 0;
    for (int corner = 0; corner < 8; ++corner) {
        const QVector3D delta((corner&1 ? high.x() : low.x())-center.x(),
                              (corner&2 ? high.y() : low.y())-center.y(),
                              (corner&4 ? high.z() : low.z())-center.z());
        const qreal depth = QVector3D::dotProduct(delta, camera.forward);
        fittedDistance = std::max(fittedDistance,
            std::max(std::abs(QVector3D::dotProduct(delta, camera.right))/(std::tan(halfFov)*aspect),
                     std::abs(QVector3D::dotProduct(delta, camera.up))/std::tan(halfFov))*1.1-depth);
    }
    { QMutexLocker lock(&m_mutex);
      m_state.target = (low + high) * .5;
      m_state.follow = false;
      m_state.distance = std::clamp(fittedDistance, 3.0, 20000.0); }
    viewChanged();
}
void PointCloudView::geometryChanged(const QRectF& next, const QRectF& previous)
{
    QQuickPaintedItem::geometryChanged(next, previous);
    if (m_gpu) m_gpu->setSize(next.size());
    if (next.size() != previous.size()) update();
}

void PointCloudView::paint(QPainter* painter)
{
    if (m_gpuActive) { painter->fillRect(boundingRect(), QColor("#071522")); return; }
    const auto view = state();
    const QSizeF viewport(width(), height());
    if (viewport.width() < 1 || viewport.height() < 1) return;
    auto& cache = *m_cache;
    const auto snapshot = view.snapshot;
    QVector3D target = view.target;
    const Camera camera(target, view.distance, view.yaw, view.topDown ? 89.5 : view.pitch, viewport);
    if (cache.revision != view.revision || cache.size != viewport) {
        cache.revision = view.revision;
        cache.size = viewport;
        for (auto& batch : cache.points) batch.clear();
        cache.grid.clear(); cache.majorGrid.clear(); cache.trajectory.clear();
        cache.aircraft.clear(); cache.hasAircraft = false;
        qreal low = 0, high = 1;
        if (snapshot && !snapshot->points.isEmpty()) {
            const auto bounds = cloudBounds(snapshot->points);
            if (bounds.valid) { low = bounds.low.z(); high = bounds.high.z(); }
            const qreal clipZ = view.cutaway ? low + (high-low)*.65 : 1.0e7;
            const qreal range = std::max(.25, (view.cutaway ? clipZ : high) - low);
            const int stride = std::max(1, (snapshot->points.size() + pointBudget - 1) / pointBudget);
            for (int index = 0; index < snapshot->points.size(); index += stride) {
                const auto& p = snapshot->points[index];
                if (p.z > clipZ) continue;
                QPointF screen;
                if (!camera.project(QVector3D(p.x, p.y, p.z), screen)) continue;
                const qreal value = view.colorMode == 1
                    ? (std::isfinite(p.intensity) ? p.intensity / 150.0 : 0.0) : (p.z - low) / range;
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
    if ((!snapshot || snapshot->points.isEmpty()) && !cache.grid.isEmpty()) painter->drawLines(cache.grid.constData(), cache.grid.size());
    painter->setPen(QPen(QColor(QStringLiteral("#203e4b")), 1));
    if ((!snapshot || snapshot->points.isEmpty()) && !cache.majorGrid.isEmpty()) painter->drawLines(cache.majorGrid.constData(), cache.majorGrid.size());
    for (int color = 0; color < colorCount; ++color) {
        const auto& points = cache.points[color];
        if (points.isEmpty()) continue;
        QPen pen(pointColor(color), view.pointSize, Qt::SolidLine, Qt::RoundCap);
        painter->setPen(pen);
        painter->drawPoints(points.constData(), points.size());
    }
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(QColor(QStringLiteral("#e0f0fa")), 1.5));
    if (!cache.trajectory.isEmpty()) painter->drawLines(cache.trajectory.constData(), cache.trajectory.size());
    if (cache.hasAircraft) {
        painter->setBrush(QColor(13, 233, 225, 30));
        painter->setPen(QPen(QColor(QStringLiteral("#1be0d5")), 1));
        painter->drawEllipse(cache.aircraftCenter, 20, 20);
        painter->setBrush(QColor(QStringLiteral("#ff4934")));
        painter->setPen(QPen(QColor(QStringLiteral("#ff4934")), 2));
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
