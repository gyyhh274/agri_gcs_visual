#pragma once
#include <QQuickFramebufferObject>

class PointCloudView;

class PointCloudGpuItem : public QQuickFramebufferObject {
public:
    explicit PointCloudGpuItem(PointCloudView *owner);
    Renderer *createRenderer() const override;
    PointCloudView *owner() const { return m_owner; }
private:
    PointCloudView *m_owner;
};
