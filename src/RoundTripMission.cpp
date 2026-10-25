#include "RoundTripMission.h"
#include "GroundLink.h"
#include "MappingClient.h"
#include <QDateTime>
#include <QUuid>
#include <cmath>
#include <QVector2D>

RoundTripMission::RoundTripMission(GroundLink *link, MappingClient *mapping, int mappingPort, QObject *parent)
    : QObject(parent), m_link(link), m_mapping(mapping), m_mappingPort(mappingPort) {
    connect(link, &GroundLink::changed, this, &RoundTripMission::update);
    connect(mapping, &MappingClient::changed, this, &RoundTripMission::update);
    connect(link, &GroundLink::commandFinished, this,
            [this](const QString &id, bool accepted, const QString &reason, const QVariantMap &mission) {
        if (id != m_pendingId) return;
        if (m_pendingOperation == "upload")
            m_uploaded = accepted && mission.value("mission_id").toString() == m_ownedId
                && mission.value("map_session_id").toString() == m_taskSession && mission.value("revision").toInt() == 1
                && mission.value("total").toInt() == 2 && mission.value("state").toString() == "READY"
                && mission.value("automatic").toBool();
        m_status = accepted ? (m_pendingOperation == "upload"
            ? QStringLiteral("自动任务已上传；确认起降区域空旷后点击开始")
            : m_pendingOperation == "cancel" ? QStringLiteral("终止请求已接受，请检查悬停并人工接管")
            : QStringLiteral("开始请求已接受，等待机载执行状态")) : QStringLiteral("请求未确认：%1").arg(reason);
        m_pendingId.clear(); m_pendingOperation.clear();
        emit changed();
    });
    m_tick.setInterval(250);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (contextReady() && m_link->mission().value("takeoff_ready").toBool() && !m_link->armed() && !active()) {
            m_hoverSamples.append(QVector3D(m_link->worldX(), m_link->worldY(), m_link->worldZ()));
            while (m_hoverSamples.size() > 8) m_hoverSamples.removeFirst();
        } else m_hoverSamples.clear();
        update();
    });
    m_tick.start();
    m_reconnect.setInterval(5000);
    connect(&m_reconnect, &QTimer::timeout, this, [this] {
        if (!m_link->connected() && !m_link->connecting()
            && !m_link->status().contains(QStringLiteral("手动断开"))
            && (m_link->endpoint().isEmpty() || m_link->endpoint() == "127.0.0.1:8765"))
            m_link->connectToGateway("127.0.0.1", 8765);
    });
}
void RoundTripMission::enableLocalLink() { m_reconnect.start(); m_link->connectToGateway("127.0.0.1", 8765); }
QString RoundTripMission::readiness() const {
    if (!m_link->connected()) return QStringLiteral("等待机载自动任务服务连接");
    if (!m_link->mission().value("automatic_supported").toBool()) return QStringLiteral("机载尚未启用自动往返任务配置");
    if (!contextReady()) return QStringLiteral("等待本次 LIVE 坐标核对、场地围栏及新鲜定位");
    if (active()) return QStringLiteral("任务执行中，可查询状态或终止并接管");
    if (!m_link->mission().value("takeoff_ready").toBool()) return QStringLiteral("等待自动起降控制器、遥控器、电量及地面起飞前检查就绪");
    if (!m_homeValid && !canRecord()) return QStringLiteral("请在地面静止约两秒，起降位置及上方须在本次围栏内");
    if (m_homeValid && !originReady()) return QStringLiteral("当前位置或起飞高度变化，请重新确认起降点");
    return QStringLiteral("自动起飞前检查通过；上传后仍须单独确认开始");
}
bool RoundTripMission::contextReady() const {
    const double now = QDateTime::currentMSecsSinceEpoch()/1000.;
    return m_link->connected() && m_link->protocolVersion() == 2 && m_link->odomFresh()
        && m_link->mission().value("automatic_supported").toBool()
        && m_link->mapCoordinatesValid() && m_link->flightBounds().size() == 6
        && m_mapping->connected() && !m_mapping->demo() && !m_mapping->paused() && !m_mapping->stale()
        && m_mapping->hasPose() && m_mapping->frameId() == "world" && !m_mapping->sessionId().isEmpty()
        && m_mapping->host() == "127.0.0.1" && m_mapping->port() == m_mappingPort
        && std::abs(m_mapping->poseStamp()-m_link->mapOdomStamp()) <= .5
        && now-m_mapping->poseStamp() < .8 && now-m_mapping->poseStamp() >= -.1;
}
bool RoundTripMission::active() const {
    const QString state = m_link->mission().value("state").toString();
    return state == "TAKING_OFF" || state == "FLYING" || state == "HOVERING" || state == "LANDING";
}
bool RoundTripMission::canEdit() const { return !active() && !m_link->busy() && m_link->mission().value("state") != "FAILED"; }
bool RoundTripMission::canRecord() const {
    if (!contextReady() || !canEdit() || !m_link->mission().value("takeoff_ready").toBool() || m_link->armed()
        || m_hoverSamples.size() < 8) return false;
    for (const auto &p : m_hoverSamples) if ((p-m_hoverSamples.last()).length() > .1f) return false;
    const auto ground = m_hoverSamples.last();
    const double h = m_link->mission().value("takeoff_height").toDouble();
    const auto b = m_link->flightBounds();
    for (int i=0; i<3; ++i) if (ground[i] <= b[i].toDouble() || ground[i] >= b[i+3].toDouble()) return false;
    return h > .1 && inside(ground + QVector3D(0,0,h));
}
bool RoundTripMission::inside(const QVector3D &p) const {
    const auto b = m_link->flightBounds();
    if (b.size() != 6) return false;
    for (int i = 0; i < 3; ++i) if (!std::isfinite(p[i]) || p[i] <= b[i].toDouble() || p[i] >= b[i+3].toDouble()) return false;
    return p.z() >= m_link->mission().value("min_z").toDouble()
        && p.z() <= m_link->mission().value("max_z").toDouble();
}
bool RoundTripMission::originReady() const {
    return !m_link->armed() && m_link->mission().value("takeoff_ready").toBool()
        && (QVector3D(m_link->worldX(),m_link->worldY(),m_link->worldZ())-m_groundOrigin).length() <= .1f
        && std::abs(m_home.z()-m_groundOrigin.z()-m_link->mission().value("takeoff_height").toDouble()) <= .03;
}
bool RoundTripMission::canUpload() const {
    return contextReady() && canEdit() && m_homeValid && m_hasTarget && originReady() && inside(m_home) && inside(m_target)
        && QVector2D(m_target.x()-m_home.x(),m_target.y()-m_home.y()).length() >= .3f;
}
bool RoundTripMission::canStart() const {
    const auto mission = m_link->mission();
    return canUpload() && m_uploaded && m_link->mission().value("automatic").toBool()
        && mission.value("state") == "READY" && mission.value("mission_id") == m_ownedId
        && mission.value("map_session_id") == m_taskSession && mission.value("revision").toInt() == 1;
}
bool RoundTripMission::canCancel() const {
    const auto task = m_link->mission();
    return m_link->connected() && m_link->protocolVersion() == 2 && !m_link->busy()
        && !task.value("mission_id").toString().isEmpty() && !task.value("map_session_id").toString().isEmpty()
        && (task.value("state") == "READY" || task.value("state") == "TAKING_OFF" || task.value("state") == "LANDING"
            || task.value("state") == "FLYING" || task.value("state") == "HOVERING");
}
void RoundTripMission::update() {
    if (m_homeValid && (!m_link->connected() || m_mapping->demo()
        || m_mapping->sessionId() != m_mapSession || m_link->mission().value("map_session_id") != m_taskSession
        || m_link->flightBounds() != m_fence)) {
        m_homeValid = m_hasTarget = m_uploaded = false;
        m_hoverSamples.clear();
        m_status = QStringLiteral("连接、地图会话或围栏变化，旧标点已失效；不会自动重新执行任务");
    }
    emit changed();
}
void RoundTripMission::recordHome() {
    if (!canRecord()) return;
    m_groundOrigin = QVector3D(m_link->worldX(),m_link->worldY(),m_link->worldZ());
    m_home = m_groundOrigin + QVector3D(0,0,m_link->mission().value("takeoff_height").toDouble());
    m_homeValid = true; m_hasTarget = m_uploaded = false;
    m_height = m_home.z();
    m_mapSession = m_mapping->sessionId();
    m_taskSession = m_link->mission().value("map_session_id").toString();
    m_fence = m_link->flightBounds();
    m_status = QStringLiteral("起飞位置已记录，返回后在此自动降落；切换俯视图选择目标");
    emit changed();
}
void RoundTripMission::setHeight(double z) {
    if (!canEdit() || !m_homeValid || !std::isfinite(z)) return;
    m_height = z;
    if (m_hasTarget) m_target.setZ(z);
    m_uploaded = false;
    emit changed();
}
void RoundTripMission::setTarget(double x, double y) {
    if (!contextReady() || !canEdit() || !m_homeValid || !std::isfinite(x) || !std::isfinite(y)) return;
    const QVector3D next(x,y,m_height);
    if (!inside(next)) { m_status = QStringLiteral("目标超出机载确认的飞行范围或高度限制"); emit changed(); return; }
    m_target = next; m_hasTarget = true; m_uploaded = false;
    m_status = QStringLiteral("目标已选择，上传时由机载检查障碍物和已知空间");
    emit changed();
}
void RoundTripMission::upload(double hover) {
    if (!canUpload() || !std::isfinite(hover) || hover < 0 || hover > 60) return;
    m_ownedId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto point = [](int id,const QVector3D &p,double wait) { return QVariantMap{{"id",id},{"x",p.x()},{"y",p.y()},{"z",p.z()},{"hover_sec",wait}}; };
    m_pendingOperation = "upload";
    m_pendingId = m_link->sendCommand("upload",m_taskSession,m_ownedId,1,{point(1,m_target,hover),point(2,m_home,0)},
        {{"automatic",true},{"ground_origin",QVariantMap{{"x",m_groundOrigin.x()},{"y",m_groundOrigin.y()},{"z",m_groundOrigin.z()}}}});
    m_uploaded = false;
    m_status = m_pendingId.isEmpty() ? QStringLiteral("未能发送上传请求") : QStringLiteral("正在上传自动起飞、目标、返航与降落任务；尚未开始飞行");
    emit changed();
}
void RoundTripMission::start() {
    if (!canStart()) return;
    m_pendingOperation = "start";
    m_pendingId = m_link->sendCommand("start",m_taskSession,m_ownedId,1);
    m_status = QStringLiteral("开始请求已发送，等待机载确认"); emit changed();
}
void RoundTripMission::cancel() {
    if (!canCancel()) return;
    m_pendingOperation = "cancel";
    const auto task = m_link->mission();
    m_pendingId = m_link->sendCommand("cancel",task.value("map_session_id").toString(),task.value("mission_id").toString(),task.value("revision").toInt());
    m_status = QStringLiteral("终止请求已发送；不自动降落，请准备人工接管"); emit changed();
}
QString RoundTripMission::phase() const {
    const auto m = m_link->mission();
    if (!m_link->connected()) return QStringLiteral("未连接任务网关，执行状态未知");
    if (m.value("mission_id") != m_ownedId || m_ownedId.isEmpty()) {
        if (m.value("mission_id").toString().isEmpty()) return QStringLiteral("单点往返任务");
        return QStringLiteral("机载已有任务：%1 · %2/%3；不会自动重发或开始")
            .arg(m.value("state").toString()).arg(m.value("completed").toInt()).arg(m.value("total").toInt());
    }
    const QString s = m.value("state").toString();
    if (s == "COMPLETED") return QStringLiteral("往返完成，已自动降落并锁定电机");
    if (s == "TAKING_OFF") return QStringLiteral("正在自动起飞，等待稳定悬停后前往目标");
    if (s == "LANDING") return QStringLiteral("已返回，正在自动降落；等待落地并锁定电机");
    if (s == "FAILED") return QStringLiteral("任务已停止：%1").arg(m.value("reason").toString());
    if (s == "FLYING") return m.value("completed").toInt() == 0 ? QStringLiteral("正在前往目标") : QStringLiteral("正在返回起飞点上方");
    if (s == "HOVERING") return QStringLiteral("已到点，正在悬停");
    if (s == "READY") return QStringLiteral("任务已上传，等待确认开始");
    return s;
}
QVariantList RoundTripMission::markers() const {
    QVariantList result;
    if (m_homeValid) result.append(QVariantMap{{"x",m_home.x()},{"y",m_home.y()},{"z",m_home.z()},{"label",QStringLiteral("R 返航 / 降落")}});
    if (m_homeValid && m_hasTarget) result.append(QVariantMap{{"x",m_target.x()},{"y",m_target.y()},{"z",m_target.z()},{"label",QStringLiteral("P 目标")}});
    return result;
}
