#pragma once

#include <QObject>
#include <QString>

// Shared, disk-only map source. No HTTP server or online fallback is used.
class OfflineMapSource : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString directory READ directory NOTIFY changed)
    Q_PROPERTY(bool tms READ tms NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString attribution READ attribution NOTIFY changed)
    Q_PROPERTY(int minimumZoom READ minimumZoom NOTIFY changed)
    Q_PROPERTY(int maximumZoom READ maximumZoom NOTIFY changed)
    Q_PROPERTY(double north READ north NOTIFY changed)
    Q_PROPERTY(double south READ south NOTIFY changed)
    Q_PROPERTY(double west READ west NOTIFY changed)
    Q_PROPERTY(double east READ east NOTIFY changed)
    Q_PROPERTY(double centerLatitude READ centerLatitude NOTIFY changed)
    Q_PROPERTY(double centerLongitude READ centerLongitude NOTIFY changed)
public:
    explicit OfflineMapSource(QObject *parent = nullptr);
    QString directory() const { return m_directory; }
    bool tms() const { return m_tms; }
    bool available() const { return m_available; }
    bool scanning() const { return m_scanning; }
    QString status() const { return m_status; }
    QString attribution() const { return m_attribution; }
    int minimumZoom() const { return m_minZoom; }
    int maximumZoom() const { return m_maxZoom; }
    double north() const { return m_north; }
    double south() const { return m_south; }
    double west() const { return m_west; }
    double east() const { return m_east; }
    double centerLatitude() const { return m_centerLatitude; }
    double centerLongitude() const { return m_centerLongitude; }
    Q_INVOKABLE void configure(const QString &directory, bool tms, bool persist = true);
signals:
    void changed();
private:
    QString m_directory;
    QString m_status = QStringLiteral("未配置离线地图 · 点击地图目录设置");
    QString m_attribution = QStringLiteral("本地瓦片 · 请核对地图数据许可");
    bool m_tms = false;
    bool m_available = false;
    bool m_scanning = false;
    int m_generation = 0;
    int m_minZoom = 0, m_maxZoom = 19;
    double m_north = 0, m_south = 0, m_west = 0, m_east = 0;
    double m_centerLatitude = 29.13678, m_centerLongitude = 119.63764;
};
