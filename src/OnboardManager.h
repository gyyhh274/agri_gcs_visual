#pragma once
#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QTimer>
#include <QVariantMap>
#include <QVariantList>

class OnboardManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString logs READ logs NOTIFY changed)
    Q_PROPERTY(QVariantList modules READ modules NOTIFY changed)
    Q_PROPERTY(int tunnelPort READ tunnelPort NOTIFY changed)
public:
    explicit OnboardManager(QObject *parent = nullptr, const QString &sshProgram = QStringLiteral("ssh"));
    ~OnboardManager() override;
    bool connected() const { return m_connected; }
    bool busy() const { return m_process.state() != QProcess::NotRunning; }
    QString message() const { return m_message; }
    QString logs() const { return m_logs; }
    QVariantList modules() const { return m_modules; }
    int tunnelPort() const { return m_tunnelPort; }
    Q_INVOKABLE void connectHost(const QString &host, const QString &user, const QString &workspace);
    Q_INVOKABLE void disconnectHost();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void startSelected(const QVariantMap &options);
    Q_INVOKABLE void stopModule(const QString &module);
    Q_INVOKABLE void readLogs(const QString &module);
signals:
    void changed();
    void gatewayAvailable(int localPort);
private:
    void request(const QJsonObject &object);
    void startTunnel();
    void announceGateway();
    QStringList sshArguments() const;
    QString m_sshProgram, m_host, m_user, m_workspace, m_message, m_logs;
    QString m_action;
    QVariantList m_modules;
    QProcess m_process, m_tunnel;
    QTimer m_poll, m_timeout;
    bool m_connected = false, m_tunnelReady = false;
    int m_tunnelPort = 0;
};
