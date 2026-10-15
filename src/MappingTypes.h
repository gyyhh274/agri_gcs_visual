#pragma once

#include <QQuaternion>
#include <QVector>
#include <QVector3D>
#include <QtGlobal>

struct MapPoint {
    float x = 0;
    float y = 0;
    float z = 0;
    float intensity = 0;
};

// Immutable copies handed to the scene graph; never share GUI-thread containers.
struct MappingSnapshot {
    QVector<MapPoint> points;
    QVector<QVector3D> trajectory;
    QVector3D position;
    QQuaternion orientation;
    bool hasPose = false;
    quint64 revision = 0;
};
