#pragma once
#include <QObject>
#include <QString>
class OfflineMapSource;

// Manually configured WGS84 position, never device telemetry.
class NestPosition : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured NOTIFY changed)
    Q_PROPERTY(double latitude READ latitude NOTIFY changed)
    Q_PROPERTY(double longitude READ longitude NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
public:
    explicit NestPosition(OfflineMapSource *source, QObject *parent = nullptr);
    bool configured() const { return m_configured; }
    double latitude() const;
    double longitude() const;
    QString name() const { return m_configured ? m_name : QStringLiteral("示例机巢"); }
    Q_INVOKABLE bool configure(const QString &latitude, const QString &longitude, const QString &name);
    Q_INVOKABLE void resetDemo();
signals:
    void changed();
private:
    OfflineMapSource *m_source;
    bool m_configured = false;
    double m_latitude = 0, m_longitude = 0;
    QString m_name;
};
