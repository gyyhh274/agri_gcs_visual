#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantMap>
#include <QVariantList>
#include <QHash>
#include <QJsonObject>

// TCP JSON-lines telemetry and versioned local-coordinate mission commands.
class GroundLink final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap mission READ mission NOTIFY changed)
    Q_PROPERTY(bool remoteAvailable READ remoteAvailable NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool connecting READ connecting NOTIFY changed)
    Q_PROPERTY(bool fcuConnected READ fcuConnected NOTIFY changed)
    Q_PROPERTY(bool armed READ armed NOTIFY changed)
    Q_PROPERTY(bool missionReady READ missionReady NOTIFY changed)
    Q_PROPERTY(bool odomFresh READ odomFresh NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY changed)
    Q_PROPERTY(double batteryPercent READ batteryPercent NOTIFY changed)
    Q_PROPERTY(double worldX READ worldX NOTIFY changed)
    Q_PROPERTY(double worldY READ worldY NOTIFY changed)
    Q_PROPERTY(double worldZ READ worldZ NOTIFY changed)
public:
    explicit GroundLink(QObject *parent = nullptr);
    QVariantMap mission() const { return m_mission; }
    bool remoteAvailable() const { return m_connected && m_protocol == 2 && !m_mission.isEmpty(); }
    bool busy() const { return !m_pending.isEmpty(); }
    Q_INVOKABLE QString uploadLocalRoute(const QVariantList &waypoints, const QString &session);
    Q_INVOKABLE QString startUploadedMission(const QString &missionId, int revision, const QString &session);
    Q_INVOKABLE void queryMission();
    bool connected() const { return m_connected; }
    bool connecting() const { return m_connecting; }
    bool fcuConnected() const { return m_fcuConnected; }
    bool armed() const { return m_armed; }
    bool missionReady() const { return m_missionReady; }
    bool odomFresh() const { return m_odomFresh; }
    QString mode() const { return m_mode; }
    QString status() const { return m_status; }
    QString endpoint() const { return m_endpoint; }
    double batteryPercent() const { return m_batteryPercent; }
    double worldX() const { return m_worldX; }
    double worldY() const { return m_worldY; }
    double worldZ() const { return m_worldZ; }

    Q_INVOKABLE void connectToGateway(const QString &host, int port);
    Q_INVOKABLE void disconnectFromGateway();
signals:
    void changed();
    void notified(const QString &text);
    void commandFinished(const QString &requestId, const QString &operation, bool accepted, const QString &reason);
private:
    QString sendCommand(const QString &operation, const QString &session, const QString &missionId,
                        int revision, const QVariantList &waypoints = {});
    bool consumeMission(const QJsonObject &object);
    void consumeLine(const QByteArray &line);
    void resetTelemetry();
    QVariantMap m_mission;
    int m_protocol = 0;
    struct Pending { QString operation; qint64 sentMs; };
    QHash<QString, Pending> m_pending;
    QTcpSocket m_socket;
    QTimer m_watchdog;
    QByteArray m_buffer;
    qint64 m_lastFrameMs{0};
    bool m_connected{false};
    bool m_connecting{false};
    bool m_fcuConnected{false};
    bool m_armed{false};
    bool m_missionReady{false};
    bool m_odomFresh{false};
    QString m_mode;
    QString m_status{QStringLiteral("未连接机载电脑")};
    QString m_endpoint;
    double m_batteryPercent{-1.0};
    double m_worldX{0.0};
    double m_worldY{0.0};
    double m_worldZ{0.0};
};
