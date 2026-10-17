#pragma once
#include <QObject>
#include <QVector3D>
#include <QTimer>
#include <QVariantList>
class GroundLink;
class MappingClient;

class RoundTripMission : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool contextReady READ contextReady NOTIFY changed)
    Q_PROPERTY(bool canRecord READ canRecord NOTIFY changed)
    Q_PROPERTY(bool homeValid READ homeValid NOTIFY changed)
    Q_PROPERTY(bool hasTarget READ hasTarget NOTIFY changed)
    Q_PROPERTY(bool canUpload READ canUpload NOTIFY changed)
    Q_PROPERTY(bool canStart READ canStart NOTIFY changed)
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool canCancel READ canCancel NOTIFY changed)
    Q_PROPERTY(double height READ height NOTIFY changed)
    Q_PROPERTY(QVector3D home READ home NOTIFY changed)
    Q_PROPERTY(QVector3D target READ target NOTIFY changed)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
public:
    RoundTripMission(GroundLink *link, MappingClient *mapping, int mappingPort = 37262, QObject *parent = nullptr);
    bool contextReady() const;
    bool canRecord() const;
    bool homeValid() const { return m_homeValid; }
    bool hasTarget() const { return m_hasTarget; }
    bool canUpload() const;
    bool canStart() const;
    bool canEdit() const;
    bool active() const;
    bool canCancel() const;
    double height() const { return m_height; }
    QVector3D home() const { return m_home; }
    QVector3D target() const { return m_target; }
    QVariantList markers() const;
    QString status() const { return m_status; }
    QString phase() const;
    void enableLocalLink();
    Q_INVOKABLE void recordHome();
    Q_INVOKABLE void setTarget(double x, double y);
    Q_INVOKABLE void setHeight(double z);
    Q_INVOKABLE void upload(double hoverSeconds);
    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();
signals:
    void changed();
private:
    void update();
    bool inside(const QVector3D &point) const;
    GroundLink *m_link;
    MappingClient *m_mapping;
    QTimer m_tick, m_reconnect;
    QList<QVector3D> m_hoverSamples;
    QVector3D m_home, m_target;
    QString m_mapSession, m_taskSession, m_ownedId, m_pendingId, m_pendingOperation;
    QString m_status = QStringLiteral("请连接任务网关，等待坐标核对与场地围栏");
    QVariantList m_fence;
    bool m_homeValid = false, m_hasTarget = false, m_uploaded = false;
    double m_height = 1.;
    int m_mappingPort;
};
