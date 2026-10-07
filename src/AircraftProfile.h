#pragma once

#include <QObject>
#include <QString>

class AircraftProfile : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString model READ model NOTIFY changed)
    Q_PROPERTY(QString autopilot READ autopilot NOTIFY changed)

public:
    explicit AircraftProfile(QObject *parent = nullptr);

    QString name() const { return m_name; }
    QString model() const { return m_model; }
    QString autopilot() const { return m_autopilot; }
    Q_INVOKABLE void reload();
    Q_INVOKABLE void applyConfiguration(const QString &configuration);

signals:
    void changed();

private:
    QString m_name;
    QString m_model;
    QString m_autopilot;
};
