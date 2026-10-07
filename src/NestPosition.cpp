#include "NestPosition.h"
#include "OfflineMapSource.h"
#include <QSettings>
#include <cmath>

namespace {
bool coordinate(const QString &latText, const QString &lonText, double &lat, double &lon)
{
    bool latOk = false, lonOk = false;
    lat = latText.trimmed().toDouble(&latOk);
    lon = lonText.trimmed().toDouble(&lonOk);
    return latOk && lonOk && std::isfinite(lat) && std::isfinite(lon)
        && std::abs(lat) <= 85.05112878 && std::abs(lon) <= 180;
}
}

NestPosition::NestPosition(OfflineMapSource *source, QObject *parent)
    : QObject(parent), m_source(source)
{
    QSettings settings;
    m_configured = settings.value("NestPosition/configured", false).toBool()
        && coordinate(settings.value("NestPosition/latitude").toString(),
                      settings.value("NestPosition/longitude").toString(), m_latitude, m_longitude);
    m_name = settings.value("NestPosition/name", "机巢 01").toString().trimmed().left(64);
    if (m_name.isEmpty()) m_name = QStringLiteral("机巢 01");
    connect(source, &OfflineMapSource::changed, this, [this]() { if (!m_configured) emit changed(); });
}

double NestPosition::latitude() const { return m_configured ? m_latitude : m_source->centerLatitude(); }
double NestPosition::longitude() const { return m_configured ? m_longitude : m_source->centerLongitude(); }

bool NestPosition::configure(const QString &latText, const QString &lonText, const QString &name)
{
    double lat, lon;
    if (!coordinate(latText, lonText, lat, lon)) return false;
    m_latitude = lat; m_longitude = lon;
    m_name = name.trimmed().left(64);
    if (m_name.isEmpty()) m_name = QStringLiteral("机巢 01");
    m_configured = true;
    QSettings settings;
    settings.setValue("NestPosition/configured", true);
    settings.setValue("NestPosition/latitude", lat);
    settings.setValue("NestPosition/longitude", lon);
    settings.setValue("NestPosition/name", m_name);
    settings.sync();
    emit changed();
    return true;
}

void NestPosition::resetDemo()
{
    QSettings settings;
    settings.remove("NestPosition"); settings.sync();
    m_configured = false;
    emit changed();
}
