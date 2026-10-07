#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

// Read-only link to the onboard ROS gateway. No flight command is exposed here.
class GroundLink final : public QObject {
    Q_OBJECT
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
private:
    void consumeLine(const QByteArray &line);
    void resetTelemetry();
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
