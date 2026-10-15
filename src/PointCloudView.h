#pragma once

#include "MappingTypes.h"
#include <QMutex>
#include <QPointer>
#include <QQuickPaintedItem>
#include <memory>

class MappingClient;

// Native, bounded point-cloud rendering also works with Qt's software backend.
class PointCloudView : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(MappingClient* source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(bool follow READ follow WRITE setFollow NOTIFY changed)
    Q_PROPERTY(bool topDown READ topDown WRITE setTopDown NOTIFY changed)
    Q_PROPERTY(qreal distance READ distance WRITE setDistance NOTIFY changed)
    Q_PROPERTY(qreal yaw READ yaw WRITE setYaw NOTIFY changed)
    Q_PROPERTY(qreal pitch READ pitch WRITE setPitch NOTIFY changed)
    Q_PROPERTY(int colorMode READ colorMode WRITE setColorMode NOTIFY changed)
public:
    explicit PointCloudView(QQuickItem* parent = nullptr);
    ~PointCloudView() override;
    MappingClient* source() const;
    void setSource(MappingClient* source);
    bool follow() const;
    void setFollow(bool enabled);
    bool topDown() const;
    void setTopDown(bool enabled);
    qreal distance() const;
    void setDistance(qreal value);
    qreal yaw() const;
    void setYaw(qreal value);
    qreal pitch() const;
    void setPitch(qreal value);
    int colorMode() const;
    void setColorMode(int value);
    Q_INVOKABLE void orbit(qreal dx, qreal dy);
    Q_INVOKABLE void pan(qreal dx, qreal dy);
    Q_INVOKABLE void zoom(qreal steps);
    Q_INVOKABLE void fitMap();
    Q_INVOKABLE void resetView();
    void paint(QPainter* painter) override;
signals:
    void changed();
protected:
    void geometryChanged(const QRectF& next, const QRectF& previous) override;
private:
    struct State {
        std::shared_ptr<const MappingSnapshot> snapshot;
        QVector3D target;
        qreal distance = 18.0;
        qreal yaw = -45.0;
        qreal pitch = 50.0;
        bool follow = true;
        bool topDown = false;
        int colorMode = 0;
        quint64 revision = 0;
    };
    struct RenderCache;
    void refreshSnapshot();
    void viewChanged();
    State state() const;
    mutable QMutex m_mutex;
    State m_state;
    QPointer<MappingClient> m_source;
    QMetaObject::Connection m_snapshotConnection;
    QMetaObject::Connection m_destroyedConnection;
    std::unique_ptr<RenderCache> m_cache;
};
