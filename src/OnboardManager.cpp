#include "OnboardManager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTcpServer>
#include <QHostAddress>

OnboardManager::OnboardManager(QObject *parent, const QString &sshProgram)
    : QObject(parent), m_sshProgram(sshProgram)
{
    m_message = QStringLiteral("未连接机载管理；连接不会启动飞行");
    m_poll.setInterval(5000);
    connect(&m_poll, &QTimer::timeout, this, &OnboardManager::refresh);
    m_timeout.setSingleShot(true); m_timeout.setInterval(30000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        m_process.kill(); m_message = QStringLiteral("管理请求超时，结果未知；重新查询状态后再操作"); emit changed();
    });
    connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
        m_timeout.stop();
        const auto doc = QJsonDocument::fromJson(m_process.readAllStandardOutput());
        const auto result = doc.object();
        if (!doc.isObject() || !result.value("ok").isBool()) {
            m_connected = false;
            m_message = QStringLiteral("SSH/管理请求失败：") + QString::fromUtf8(m_process.readAllStandardError()).right(1200);
            emit changed(); return;
        }
        m_connected = true;
        const auto status = result.value("status").toObject();
        if (status.value("modules").isArray()) m_modules = status.value("modules").toArray().toVariantList();
        if (result.value("logs").isString()) m_logs = result.value("logs").toString();
        if (!result.value("ok").toBool() || code != 0)
            m_message = result.value("message").toString(QStringLiteral("部分模块启动失败，请查看结果"));
        else if (m_action != "status") m_message = result.value("message").toString(QStringLiteral("请求已处理；业务就绪以遥测和日志为准"));
        else if (m_tunnel.state() == QProcess::Running)
            m_message = QStringLiteral("机载管理已连接；任务隧道 127.0.0.1:%1").arg(m_tunnelPort);
        else m_message = QStringLiteral("机载管理已连接；飞行状态以任务遥测为准");
        const auto results = result.value("results").toArray();
        if (!results.isEmpty()) {
            QStringList messages;
            for (const auto &value : results) {
                const auto row = value.toObject();
                messages << row.value("module").toString() + ": " + row.value("message").toString();
            }
            m_message = messages.join("\n");
        }
        if (m_tunnel.state() == QProcess::NotRunning) startTunnel();
        announceGateway(); emit changed();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_timeout.stop(); m_connected = false; m_message = m_process.errorString(); emit changed();
        }
    });
    connect(&m_tunnel, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_tunnelReady = false; m_message = QStringLiteral("无法启动 SSH 任务隧道：") + m_tunnel.errorString(); emit changed();
        }
    });
    connect(&m_tunnel, &QProcess::started, this, [this] { m_tunnelReady = true; emit changed(); });
    connect(&m_tunnel, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
        m_tunnelReady = false;
        m_message = QStringLiteral("任务隧道已断开：") + QString::fromUtf8(m_tunnel.readAllStandardError()).right(600);
        emit changed();
    });
}
OnboardManager::~OnboardManager()
{
    m_poll.stop(); m_timeout.stop();
    m_process.kill(); m_process.waitForFinished(1000);
    m_tunnel.kill(); m_tunnel.waitForFinished(1000);
}
QStringList OnboardManager::sshArguments() const
{
    return {"-F", "/dev/null", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5",
            "-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=3"};
}
void OnboardManager::connectHost(const QString &host, const QString &user, const QString &workspace)
{
    if (busy()) return;
    static const QRegularExpression hostname("^[A-Za-z0-9][A-Za-z0-9.-]{0,252}$");
    static const QRegularExpression username("^[A-Za-z_][A-Za-z0-9_-]{0,63}$");
    static const QRegularExpression path("^/[A-Za-z0-9_./-]+$");
    if (!hostname.match(host).hasMatch() || !username.match(user).hasMatch()
            || !path.match(workspace).hasMatch() || workspace.contains("/../")) {
        m_message = QStringLiteral("请填写有效主机、SSH 用户和不含空格的绝对工程路径"); emit changed(); return;
    }
    if (m_host != host || m_user != user || m_workspace != workspace) {
        disconnectHost(); m_modules.clear();
    }
    m_host = host; m_user = user; m_workspace = workspace;
    m_poll.start(); request({{"action", "status"}});
}
void OnboardManager::disconnectHost()
{
    m_poll.stop(); m_timeout.stop();
    m_process.blockSignals(true); m_process.kill(); m_process.waitForFinished(1000); m_process.blockSignals(false);
    m_tunnel.blockSignals(true); m_tunnel.kill(); m_tunnel.waitForFinished(1000); m_tunnel.blockSignals(false);
    m_connected = m_tunnelReady = false; m_tunnelPort = 0; m_modules.clear();
    m_message = QStringLiteral("管理连接已断开；已启动的机载模块继续运行"); emit changed();
}
void OnboardManager::request(const QJsonObject &object)
{
    if (busy() || m_host.isEmpty()) return;
    m_action = object.value("action").toString();
    auto args = sshArguments();
    args << m_user + "@" + m_host << ("/usr/bin/python3 '" + m_workspace + "/sh_files/module_manager.py' --request");
    m_process.start(m_sshProgram, args);
    m_timeout.start();
    m_process.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
    m_process.closeWriteChannel(); emit changed();
}
void OnboardManager::refresh() { if (!m_host.isEmpty()) request({{"action", "status"}}); }
void OnboardManager::startSelected(const QVariantMap &options)
{
    if (m_connected) request({{"action", "start"}, {"options", QJsonObject::fromVariantMap(options)}});
}
void OnboardManager::stopModule(const QString &module)
{
    if (m_connected) request({{"action", "stop"}, {"module", module}});
}
void OnboardManager::readLogs(const QString &module)
{
    if (m_connected) request({{"action", "logs"}, {"module", module}});
}
void OnboardManager::startTunnel()
{
    // Reserve a free loopback port instead of adopting or killing somebody else's SSH forward.
    QTcpServer reserve;
    if (!reserve.listen(QHostAddress::LocalHost)) return;
    m_tunnelPort = reserve.serverPort(); reserve.close();
    auto args = sshArguments();
    args << "-o" << "ExitOnForwardFailure=yes" << "-N" << "-L"
         << QStringLiteral("127.0.0.1:%1:127.0.0.1:8765").arg(m_tunnelPort) << m_user + "@" + m_host;
    m_tunnel.start(m_sshProgram, args);
}
void OnboardManager::announceGateway()
{
    if (!m_tunnelReady || !m_connected) return;
    for (const auto &value : m_modules) {
        const auto module = value.toMap();
        if (module.value("name") == "flight" &&
                (module.value("state") == "running" || module.value("state") == "external"))
            emit gatewayAvailable(m_tunnelPort);
    }
}
