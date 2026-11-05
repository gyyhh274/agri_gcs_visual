#pragma once
#include "ModbusTcpClient.h"
#include <QElapsedTimer>
#include <QVariantMap>

class NestPlc : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap configuration READ configuration NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool connecting READ connecting NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool protocolVerified READ protocolVerified NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QStringList log READ log NOTIFY changed)
    Q_PROPERTY(QString readResult READ readResult NOTIFY changed)
    Q_PROPERTY(bool monitoring READ monitoring NOTIFY changed)
    Q_PROPERTY(bool feedbackFresh READ feedbackFresh NOTIFY changed)
    Q_PROPERTY(bool writeEnabled READ writeEnabled NOTIFY changed)
    Q_PROPERTY(bool canMove READ canMove NOTIFY changed)
    Q_PROPERTY(bool canStop READ canStop NOTIFY changed)
    Q_PROPERTY(bool uncertain READ uncertain NOTIFY changed)
    Q_PROPERTY(QString doorState READ doorState NOTIFY changed)
    Q_PROPERTY(QString commandStatus READ commandStatus NOTIFY changed)
    Q_PROPERTY(int flags READ flags NOTIFY changed)
    Q_PROPERTY(int faultCode READ faultCode NOTIFY changed)
    Q_PROPERTY(int ackSequence READ ackSequence NOTIFY changed)
    Q_PROPERTY(int heartbeat READ heartbeat NOTIFY changed)
public:
    explicit NestPlc(QObject *parent = nullptr);
    QVariantMap configuration() const { return m_config; }
    bool connected() const { return m_client.connected(); }
    bool connecting() const { return m_client.connecting(); }
    bool busy() const { return m_client.busy(); }
    bool protocolVerified() const { return m_client.verified(); }
    QString status() const { return m_status.isEmpty() ? m_client.status() : m_status; }
    QStringList log() const { return m_client.log(); }
    QString readResult() const { return m_readResult; }
    bool monitoring() const { return m_monitoring; }
    bool feedbackFresh() const;
    bool writeEnabled() const { return m_writeEnabled; }
    bool canMove() const;
    bool canStop() const;
    bool uncertain() const { return m_uncertain; }
    QString doorState() const;
    QString commandStatus() const { return m_commandStatus; }
    int flags() const { return m_flags; }
    int faultCode() const { return m_fault; }
    int ackSequence() const { return m_ack; }
    int heartbeat() const { return m_heartbeat; }
    Q_INVOKABLE bool configure(const QVariantMap &configuration);
    Q_INVOKABLE void connectPlc();
    Q_INVOKABLE void disconnectPlc();
    Q_INVOKABLE bool identify();
    Q_INVOKABLE bool readRegisters(int function, int address, int count);
    Q_INVOKABLE bool setMonitoring(bool enabled);
    Q_INVOKABLE bool setWriteEnabled(bool enabled);
    Q_INVOKABLE bool sendCommand(int command); // 1=open, 2=close, 3=stop
    Q_INVOKABLE bool acknowledgeStopped();
signals:
    void changed();
    void event(const QString &message);
private:
    bool reject(const QString &reason);
    void tick();
    void invalidate();
    void complete(int function, const QByteArray &payload, int exception);
    ModbusTcpClient m_client;
    QTimer m_tick;
    QElapsedTimer m_clock;
    QVariantMap m_config;
    QString m_status, m_readResult = QStringLiteral("尚未读取寄存器"), m_commandStatus = QStringLiteral("无待执行命令");
    enum Purpose { None, Identification, ManualRead, Feedback, Command } m_purpose = None;
    bool m_monitoring = false, m_writeEnabled = false, m_snapshot = false, m_heartbeatAdvanced = false, m_uncertain = false;
    int m_readCount = 0;
    int m_flags = 0, m_fault = 0, m_ack = 0, m_heartbeat = 0, m_pendingCommand = 0, m_pendingSequence = 0;
    qint64 m_feedbackAt = -1, m_heartbeatAt = -1, m_lastPoll = -1000, m_commandAt = 0;
};
