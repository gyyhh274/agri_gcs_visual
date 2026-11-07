#include "RoundTripMission.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
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
        if ((m_pendingOperation == "clear" || m_pendingOperation == "initialize") && accepted) {
            // 任务初始化：把地面站侧也恢复成"下一次任务开始前"的状态，
            // 起降点、目标点、上传标记全部作废，必须重新确认。
            m_uploaded = false;
            m_homeValid = false;
            m_hasTarget = false;
            m_home = QVector3D();
            m_target = QVector3D();
            m_groundOrigin = QVector3D();
        }
        m_status = accepted ? (m_pendingOperation == "upload"
            ? QStringLiteral("自动任务已上传；确认起降区域空旷后点击开始")
            : m_pendingOperation == "cancel" ? QStringLiteral("终止请求已接受，请检查悬停并人工接管")
            : m_pendingOperation == "initialize" ? QStringLiteral("初始化请求已接受，等待机载准备完成")
            : m_pendingOperation == "clear" ? QStringLiteral("环境已初始化：请重新确认起降点，再选目标并上传")
            : QStringLiteral("开始请求已接受，等待机载执行状态")) : QStringLiteral("请求未确认：%1").arg(reason);
        m_pendingId.clear(); m_pendingOperation.clear();
        emit changed();
    });
    m_tick.setInterval(250);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (contextReady() && m_link->mission().value("takeoff_ready").toBool() && !m_link->armed() && !active()) {
        }
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
    if (!m_link->connected()) return QStringLiteral("未满足：机载任务网关未连接");
    if (active()) return QStringLiteral("任务执行中，可终止并接管");
    QStringList missing;
    const auto add = [&missing](const QString &value) { if (!missing.contains(value)) missing.append(value); };
    const auto task = m_link->mission();
    if (m_link->protocolVersion() != 2) add(QStringLiteral("机载任务协议版本不支持自动往返"));
    if (!task.value("automatic_supported").toBool()) add(QStringLiteral("机载未启用自动往返任务"));
    if (!m_link->odomFresh()) add(QStringLiteral("EKF 定位数据未更新"));
    if (m_link->flightBounds().size() != 6) add(QStringLiteral("本次飞行围栏未设置或无效"));
    if (!m_link->mapCoordinatesValid()) {
        const QString reason = task.value("map_gate_reason").toString();
        const QString reject = task.value("map_last_reject").toString();
        const QString detail = task.value("map_last_reject_detail").toString();
        if (reason == "no_fence") add(QStringLiteral("本次飞行围栏未设置或无效"));
        else if (reason == "no_map" || reason == "map_not_arriving") add(QStringLiteral("机载 LIO 地图定位未更新"));
        else if (reason == "map_stamp_ahead" || reason == "map_stamp_late" || reason == "map_stamp_invalid") add(QStringLiteral("机载 LIO 定位时间异常或过期"));
        else if (reject == "distance" || reject == "angle") add(QStringLiteral("LIO 与 EKF 坐标不一致：%1").arg(detail));
        else if (task.contains("map_planner") && task.value("map_planner").toInt() == 0) add(QStringLiteral("尚未收到 EKF 定位，无法核对地图坐标"));
        else add(QStringLiteral("LIO 与 EKF 坐标核对尚未通过"));
    }
    if (!m_mapping->connected()) add(QStringLiteral("LIVE 点云地图未连接"));
    else {
        if (m_mapping->demo()) add(QStringLiteral("当前为演示地图，请切换 LIVE 地图"));
        if (m_mapping->paused()) add(QStringLiteral("LIVE 地图已暂停"));
        if (m_mapping->stale()) add(QStringLiteral("LIVE 地图数据未更新"));
        if (!m_mapping->hasPose()) add(QStringLiteral("LIVE 地图位姿未更新"));
        if (m_mapping->frameId() != "world") add(QStringLiteral("LIVE 地图坐标系不是 world"));
        if (m_mapping->sessionId().isEmpty()) add(QStringLiteral("LIVE 地图会话未建立"));
        if (m_mapping->host() != "127.0.0.1" || m_mapping->port() != m_mappingPort) add(QStringLiteral("LIVE 地图连接地址不匹配"));
        const double now = QDateTime::currentMSecsSinceEpoch()/1000.;
        if (m_mapping->hasPose() && (std::abs(m_mapping->poseStamp()-m_link->mapOdomStamp()) > .5 ||
            now-m_mapping->poseStamp() >= 1.5 || now-m_mapping->poseStamp() < -.1))
            add(QStringLiteral("LIVE 地图位姿时间与机载定位不匹配或已过期"));
    }
    if (!m_link->fcuConnected()) add(QStringLiteral("飞控未连接或状态已过期"));
    if (m_link->armed()) add(QStringLiteral("飞控已解锁，请先落地锁定"));
    if (!task.value("takeoff_ready").toBool()) {
        static const QHash<QString, QString> labels = {
            {"auto_config", QStringLiteral("机载自动起降配置未启用")},
            {"fcu_link", QStringLiteral("飞控未连接或状态已过期")},
            {"landed_status_stale", QStringLiteral("飞控着陆状态未更新")},
            {"odom_stale", QStringLiteral("EKF 定位数据未更新")},
            {"odom_frame", QStringLiteral("EKF 定位坐标系不是 world")},
            {"odom_stamp", QStringLiteral("EKF 定位时间异常或过期")},
            {"odom_invalid", QStringLiteral("EKF 位置、速度或姿态数据无效")},
            {"imu_stale", QStringLiteral("飞控 IMU 数据未更新")},
            {"imu_stamp", QStringLiteral("IMU 数据时间异常或过期")},
            {"imu_invalid", QStringLiteral("IMU 姿态或加速度数据无效")},
            {"rc_stale", QStringLiteral("遥控器信号未更新")},
            {"rc_hover", QStringLiteral("遥控器未切到悬停模式")},
            {"rc_command", QStringLiteral("遥控器未切到指令控制模式")},
            {"rc_sticks", QStringLiteral("遥控器摇杆未回中")},
            {"controller_state", QStringLiteral("飞行控制器未处于手动待机状态")},
            {"armed", QStringLiteral("飞控已解锁，请先落地锁定")},
            {"command_active", QStringLiteral("控制器仍在收到轨迹指令")},
            {"land_detector", QStringLiteral("控制器未确认飞机已落地")},
            {"not_on_ground", QStringLiteral("飞控未确认飞机已落地")},
            {"moving", QStringLiteral("飞机未静止（定位速度超过 0.1 m/s）")},
            {"battery_stale", QStringLiteral("电池电压数据未更新或无效")},
            {"battery_low", QStringLiteral("电池电压低于控制器阈值")},
            {"height_invalid", QStringLiteral("起飞悬停高度无效或尚未收到")},
            {"trajectory_not_ready", QStringLiteral("轨迹服务器未就绪或已停止输出")},
            {"map_not_ready", QStringLiteral("机载地图与围栏校验未就绪")},
            {"landing_latched", QStringLiteral("上次降落状态尚未复位，请进行任务初始化")},
            {"mission_fault", QStringLiteral("任务故障尚未复位，请进行任务初始化")},
            {"planner_heartbeat", QStringLiteral("规划器心跳未更新")},
            {"planner_goal", QStringLiteral("规划器未接收目标点")},
            {"controller_stale", QStringLiteral("飞行控制器状态未更新")},
            {"controller_auto_stale", QStringLiteral("飞行控制器自动控制状态未更新")},
            {"controller_preflight_stale", QStringLiteral("飞行控制器起飞检查状态未更新")},
            {"controller_no_commands", QStringLiteral("飞行控制器未接收起降指令")}
        };
        const QVariant controller = task.value("preflight_controller_reasons");
        const QVariant executor = task.value("preflight_mission_reasons");
        const bool haveController = controller.isValid() && !controller.isNull();
        const bool haveExecutor = executor.isValid() && !executor.isNull();
        const auto controllerReasons = controller.toList();
        const auto process = [&](const QVariantList &reasons) {
            for (const QVariant &item : reasons) {
                const QString code = item.toString();
                if (code == "controller_auto_check" || code == "controller_preflight_check") {
                    if (!haveController || controllerReasons.isEmpty()) add(QStringLiteral("飞行控制器检查未通过，正在等待具体原因"));
                } else if (labels.contains(code)) add(labels.value(code));
                else add(QStringLiteral("机载起飞检查未通过（%1）").arg(code));
            }
        };
        if (haveController) process(controllerReasons);
        if (haveExecutor) process(executor.toList());
        if (!haveExecutor) add(QStringLiteral("机载任务起飞检查详情未更新"));
        if (!haveController) add(QStringLiteral("飞行控制器检查详情未更新"));
        if (missing.isEmpty()) add(QStringLiteral("机载起飞检查未通过，等待具体原因"));
    }
    if (!canEdit() && !active()) {
        if (m_link->busy()) add(QStringLiteral("机载正在处理上一条请求"));
        else if (task.value("state") == "FAILED") add(QStringLiteral("任务处于失败状态，请先初始化"));
    }
    if (missing.isEmpty() && !m_homeValid && !canRecord()) {
        const auto detail = recordHint();
        if (!detail.isEmpty()) add(detail);
    }
    if (missing.isEmpty() && m_homeValid && !originReady()) add(QStringLiteral("当前位置或起飞高度变化，请重新确认起降点"));
    if (!missing.isEmpty()) return QStringLiteral("未满足的条件：\n• ") + missing.join(QStringLiteral("\n• "));
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
        // 点云桥实测位姿刷新约 0.6 s 一次、偶有 1 s 空洞，窗口取 1.5 s 与之匹配；
        // 这里只用于地面标点的"LIVE 地图"判据，不参与飞行决策。
        && now-m_mapping->poseStamp() < 1.5 && now-m_mapping->poseStamp() >= -.1;
}
bool RoundTripMission::active() const {
    const QString state = m_link->mission().value("state").toString();
    return state == "TAKING_OFF" || state == "FLYING" || state == "HOVERING" || state == "LANDING";
}
bool RoundTripMission::canEdit() const { return !active() && !m_link->busy() && m_link->mission().value("state") != "FAILED"; }
QString RoundTripMission::recordHint() const {
    // 逐项说明"① 确认起降点"为什么点不了，避免只给一句笼统提示。
    if (!m_link->connected()) return QStringLiteral("未连接任务网关");
    if (!m_link->mission().value("automatic_supported").toBool())
        return QStringLiteral("机载未启用自动往返配置");
    if (!contextReady()) return QStringLiteral("LIVE 地图/围栏/定位上下文未就绪");
    if (!canEdit()) {
        if (m_link->busy()) return QStringLiteral("机载正忙（等上一请求返回）");
        if (m_link->mission().value("state") == "FAILED") return QStringLiteral("机载任务处于 FAILED，请先清理任务");
        return QStringLiteral("任务执行中");
    }
    if (!m_link->mission().value("takeoff_ready").toBool())
        return QStringLiteral("机载地面起飞前检查未通过");
    if (m_link->armed()) return QStringLiteral("飞控已解锁");
    // 不再要求"静止 2 秒"：操作员确认后立即取当前位姿作起降点。
    const auto ground = QVector3D(m_link->worldX(), m_link->worldY(), m_link->worldZ());
    const auto b = m_link->flightBounds();
    for (int i = 0; i < 3; ++i)
        if (ground[i] <= b[i].toDouble() || ground[i] >= b[i+3].toDouble())
            return QStringLiteral("起降点在地图围栏外");
    const double height = m_link->mission().value("takeoff_height").toDouble();
    if (!(height > .1)) return QStringLiteral("起飞高度未知（%1 m）").arg(height);
    if (!inside(ground + QVector3D(0, 0, height)))
        return QStringLiteral("起飞上升后的位置在围栏外");
    return QString();
}
bool RoundTripMission::canRecord() const {
    if (!contextReady() || !canEdit() || !m_link->mission().value("takeoff_ready").toBool() || m_link->armed()
) return false;
    const auto ground = QVector3D(m_link->worldX(), m_link->worldY(), m_link->worldZ());
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
bool RoundTripMission::canClear() const {
    if (!m_link->connected() || m_link->protocolVersion() != 2 || m_link->busy()) return false;
    const auto task = m_link->mission();
    const QString state = task.value("state").toString();
    // 安全限制：飞行中（含起飞/降落）一律不允许清理。
    if (m_link->armed() || state == "TAKING_OFF" || state == "FLYING"
        || state == "HOVERING" || state == "LANDING") return false;
    // 初始化是"把环境恢复到可开始下一次任务"的动作，只要在地面、未解锁、
    // 不在飞行中就应该可用，不再要求机载一定处于 READY/COMPLETED/FAILED。
    return !task.value("map_session_id").toString().isEmpty();
}
bool RoundTripMission::canInitialize() const {
    // 已连接时沿用 canClear 的全部安全条件；未连接时允许点击——它的用途正是
    // 把机载适配器拉起来，此时拿不到飞行状态，只能依赖连接恢复后的 clear 校验。
    const QString phase = m_link->mission().value("init_phase").toString();
    if (!phase.isEmpty() && phase != "idle" && phase != "ready" && phase != "failed") return false;
    if (m_link->connected()) return canClear();
    return !m_link->busy();
}

void RoundTripMission::startAdapterBootstrap() {
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + "/../deploy/remote_exec.py",
        QCoreApplication::applicationDirPath() + "/deploy/remote_exec.py",
        QStringLiteral("/home/cat/projects/agri_gcs_visual/deploy/remote_exec.py")};
    QString script;
    for (const auto &path : candidates)
        if (QFileInfo::exists(path)) { script = path; break; }
    if (script.isEmpty()) {
        m_status = QStringLiteral("找不到 deploy/remote_exec.py，无法启动机载适配器");
        m_pendingInitBounds.clear();
        emit changed();
        return;
    }
    if (m_bootstrap) m_bootstrap->deleteLater();
    m_bootstrap = new QProcess(this);
    connect(m_bootstrap, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
        if (code != 0) {
            m_status = QStringLiteral("启动机载适配器失败（退出码 %1），请检查机载是否通电联网").arg(code);
            m_pendingInitBounds.clear();
        } else {
            m_status = QStringLiteral("机载适配器已启动，等待连接后自动继续初始化…");
        }
        emit changed();
    });
    m_status = QStringLiteral("正在启动机载任务适配器…");
    m_bootstrap->start(QStringLiteral("python3"), {script});
    emit changed();
}

void RoundTripMission::sendInitialize() {
    const auto task = m_link->mission();
    const QVariantList bounds = m_pendingInitBounds;
    m_pendingInitBounds.clear(); // sendCommand 发出 changed 时避免重复发送。
    m_pendingOperation = "initialize";
    m_pendingId = m_link->sendCommand("initialize", task.value("map_session_id").toString(), QString(), 0,
                                      {}, {}, {{"bounds", bounds},
                                               {"project_dir", m_pendingInitProject},
                                               {"takeoff_height", m_pendingInitHeight}});
    m_status = m_pendingId.isEmpty()
        ? QStringLiteral("未能发送初始化请求")
        : QStringLiteral("正在初始化：清理机载任务并按新围栏重启前置链路…");
    emit changed();
}

void RoundTripMission::initialize(const QVariantList &bounds, const QString &projectDir, double takeoffHeight) {
    if (!canInitialize() || bounds.size() != 6 || projectDir.trimmed().isEmpty()) return;
    double fence[6];
    for (int i = 0; i < 6; ++i) {
        fence[i] = bounds.at(i).toDouble();
        if (!std::isfinite(fence[i])) return;
    }
    if (!(fence[0] < fence[3] && fence[1] < fence[4] && fence[2] < fence[5])) {
        m_status = QStringLiteral("围栏无效：每个轴必须 min < max"); emit changed(); return;
    }
    if (!std::isfinite(takeoffHeight) || takeoffHeight <= .1 || takeoffHeight >= fence[5] - fence[2]) {
        m_status = QStringLiteral("起飞高度须大于 0.1 m，且小于围栏高度"); emit changed(); return;
    }
    if (m_link->connected() && !m_link->mission().value("init_height_supported").toBool()) {
        m_status = QStringLiteral("机载网关版本不支持设置起飞高度，请更新机载程序"); emit changed(); return;
    }
    m_pendingInitHeight = takeoffHeight;
    if (!m_link->connected()) {
        m_pendingInitBounds = bounds;
        m_pendingInitProject = projectDir;
        m_initDeadline = QDateTime::currentMSecsSinceEpoch() + 90000;
        startAdapterBootstrap();
        return;
    }
    m_pendingInitBounds = bounds;
    m_pendingInitProject = projectDir;
    sendInitialize();
}

void RoundTripMission::clearMission() {
    if (!canClear()) return;
    m_pendingOperation = "clear";
    const auto task = m_link->mission();
    m_pendingId = m_link->sendCommand("clear", task.value("map_session_id").toString(), QString(), 0);
    m_status = QStringLiteral("清理请求已发送；仅在地面且飞控未解锁时生效"); emit changed();
}
void RoundTripMission::update() {
    if (m_homeValid && (!m_link->connected() || m_mapping->demo()
        || m_mapping->sessionId() != m_mapSession || m_link->mission().value("map_session_id") != m_taskSession
        || m_link->flightBounds() != m_fence)) {
        m_homeValid = m_hasTarget = m_uploaded = false;
        m_status = QStringLiteral("连接、地图会话或围栏变化，旧标点已失效；不会自动重新执行任务");
    }
    // 机载初始化是异步的：忙碌阶段持续刷新文案，就绪/失败只在跃变时提示一次，
    // 避免把操作员后续看到的其它提示覆盖掉。
    const QString phase = m_link->mission().value("init_phase").toString();
    const QString detail = m_link->mission().value("init_detail").toString();
    if (phase != m_lastInitPhase) {
        m_lastInitPhase = phase;
        if (phase == "ready") m_status = QStringLiteral("初始化完成：%1").arg(detail);
        else if (phase == "failed") m_status = QStringLiteral("初始化失败：%1").arg(detail);
    }
    if (phase != "idle" && phase != "ready" && phase != "failed" && !detail.isEmpty())
        m_status = QStringLiteral("初始化中：%1").arg(detail);

    // 引导流程：适配器起来、链路恢复后自动补发 initialize
    if (!m_pendingInitBounds.isEmpty()) {
        if (m_link->connected()) {
            if (canClear() && m_link->mission().value("init_height_supported").toBool()) sendInitialize();
            else {
                m_pendingInitBounds.clear();
                m_status = m_link->mission().value("init_height_supported").toBool()
                    ? QStringLiteral("初始化未发送：请确认飞控未解锁且任务未在执行")
                    : QStringLiteral("机载网关版本不支持设置起飞高度，请更新机载程序");
            }
        } else if (QDateTime::currentMSecsSinceEpoch() > m_initDeadline) {
            m_pendingInitBounds.clear();
            m_status = QStringLiteral("机载适配器启动后 90 秒内未连上，请检查网络与机载进程");
        }
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
bool RoundTripMission::setTarget(double x, double y) {
    const auto reject = [this](const QString &reason) {
        m_status = reason; emit changed(); return false;
    };
    if (!contextReady()) return reject(QStringLiteral("LIVE 地图或定位暂未就绪；恢复后可继续点击选点"));
    if (!canEdit()) return reject(QStringLiteral("当前任务状态或未结束的请求不允许修改目标"));
    if (!m_homeValid) return reject(QStringLiteral("请先确认本次当前位置为起降点，再选择目标"));
    if (!std::isfinite(x) || !std::isfinite(y)) return reject(QStringLiteral("目标坐标无效，请重新点击地图"));
    const QVector3D next(x,y,m_height);
    if (!inside(next)) return reject(QStringLiteral("目标 (%1, %2, Z=%3) 超出机载确认的飞行范围或高度限制；请在绿色范围内选点并核对目标高度")
        .arg(x,0,'f',2).arg(y,0,'f',2).arg(m_height,0,'f',2));
    m_target = next; m_hasTarget = true; m_uploaded = false;
    m_status = QStringLiteral("目标已选择，上传时由机载检查障碍物和已知空间");
    emit changed();
    return true;
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
