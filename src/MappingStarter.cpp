#include "MappingStarter.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QSettings>
#include <QUrl>

MappingStarter::MappingStarter(QObject *parent) : QObject(parent) {
    m_autoStart = QSettings().value("RealtimeMapping/autoStart", true).toBool();
    m_network.setProxy(QNetworkProxy::NoProxy);
    m_poll.setSingleShot(true);
    connect(&m_poll, &QTimer::timeout, this, &MappingStarter::request);
}
MappingStarter::~MappingStarter() { cancel(); }

void MappingStarter::cancel() {
    m_poll.stop();
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_running = false;
}
void MappingStarter::setAutoStart(bool enabled) {
    if (m_autoStart == enabled) return;
    m_autoStart = enabled;
    QSettings().setValue("RealtimeMapping/autoStart", enabled);
    if (enabled) startIfEnabled();
    else { cancel(); m_status = QStringLiteral("自动启动建图已关闭"); }
    emit changed();
}
void MappingStarter::setSuspended(bool suspended) {
    if (m_suspended == suspended) return;
    m_suspended = suspended;
    if (suspended) { cancel(); m_status.clear(); emit changed(); }
    else startIfEnabled();
}
void MappingStarter::setEndpoint(const QString &host, int port) {
    // Startup is only exposed through the authenticated local SSH tunnel.
    if ((host != "127.0.0.1" && host != "localhost") || port < 1 || port > 65535) return;
    cancel();
    m_host = host; m_port = port;
}
void MappingStarter::startIfEnabled() {
    if (m_autoStart && !m_suspended) start();
}
void MappingStarter::start() {
    if (m_suspended || m_running) return;
    m_running = true;
    m_ensureNext = true;
    m_status = QStringLiteral("正在请求机载启动建图");
    emit changed();
    request();
}
void MappingStarter::request() {
    if (!m_running || m_suspended || m_reply) return;
    QUrl url;
    url.setScheme("http"); url.setHost(m_host); url.setPort(m_port);
    url.setPath(m_ensureNext ? "/ensure" : "/status");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply *reply = m_ensureNext
        ? m_network.post(req, QByteArrayLiteral("{\"protocol\":1,\"operation\":\"ensure_mapping\"}"))
        : m_network.get(req);
    m_reply = reply;
    reply->setReadBufferSize(4096);
    QTimer::singleShot(5000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_reply = nullptr;
        const QByteArray raw = reply->readAll();
        const bool transportError = reply->error() != QNetworkReply::NoError;
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (transportError && http == 0) {
            m_status = QStringLiteral("建图启动服务未连接，正在重试");
            m_poll.start(3000);
            emit changed();
            return;
        }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(raw, &error);
        const auto obj = doc.object();
        const QString state = obj.value("state").toString();
        if (http != 200 || error.error != QJsonParseError::NoError || !doc.isObject()
                || obj.value("protocol").toInt() != 1 || obj.value("service") != "agri-mapping-start"
                || (state != "idle" && state != "starting" && state != "ready" && state != "error")) {
            m_status = QStringLiteral("建图启动接口不匹配，请检查机载服务");
            m_running = false;
        } else {
            m_status = obj.value("message").toString().left(240);
            if (state == "starting" || state == "idle") {
                m_ensureNext = state == "idle";
                m_poll.start(1500);
            } else {
                m_running = false;
                if (state == "error") m_status.prepend(QStringLiteral("建图启动失败："));
            }
        }
        emit changed();
    });
}
