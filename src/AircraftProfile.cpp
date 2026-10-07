#include "AircraftProfile.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace {
QString configuredText(const QJsonObject &configuration, const char *key, const QString &fallback)
{
    const QJsonValue value = configuration.value(QLatin1String(key));
    if (!value.isString()) return fallback;
    const QString text = value.toString().trimmed();
    return text.isEmpty() ? QStringLiteral("未设置") : text;
}
}

AircraftProfile::AircraftProfile(QObject *parent) : QObject(parent)
{
    reload();
}

void AircraftProfile::reload()
{
    QSettings settings;
    settings.sync();
    applyConfiguration(settings.value(QStringLiteral("GroundStationUi/configuration")).toString());
}

void AircraftProfile::applyConfiguration(const QString &serialized)
{
    const QJsonDocument document = QJsonDocument::fromJson(serialized.toUtf8());
    const QJsonObject configuration = document.isObject() ? document.object() : QJsonObject();
    const QString name = configuredText(configuration, "aircraftName", QStringLiteral("巡检无人机 01"));
    const QString model = configuredText(configuration, "aircraftModel", QStringLiteral("DJI M30T"));
    const QString autopilot = configuredText(configuration, "autopilot", QStringLiteral("PX4"));
    if (m_name == name && m_model == model && m_autopilot == autopilot) return;
    m_name = name;
    m_model = model;
    m_autopilot = autopilot;
    emit changed();
}
