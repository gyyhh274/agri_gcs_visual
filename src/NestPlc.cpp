#include "NestPlc.h"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {
int word(const QByteArray &data, int offset) { return (quint8(data.at(offset)) << 8) | quint8(data.at(offset + 1)); }
const QVariantMap defaults = {{"host", "192.168.2.8"}, {"port", 502}, {"unit", 1},
    {"statusFunction", 3}, {"statusAddress", -1}, {"commandAddress", -1}};
}
NestPlc::NestPlc(QObject *parent) : QObject(parent), m_config(defaults) {
    m_clock.start();
    QSettings settings;
    const auto saved = QJsonDocument::fromJson(settings.value("NestPlcV1/configuration").toByteArray()).object().toVariantMap();
    if (!saved.isEmpty()) configure(saved);
    m_status.clear();
    connect(&m_client, &ModbusTcpClient::changed, this, [this] {
        if (!connected() && !connecting()) {
            if (m_pendingCommand) {
                m_uncertain = true;
                m_commandStatus = QStringLiteral("连接已中断，命令执行结果未知；请检查现场");
            }
            invalidate();
        }
        emit changed();
    });
    connect(&m_client, &ModbusTcpClient::response, this, &NestPlc::complete);
    connect(&m_client, &ModbusTcpClient::failed, this, [this](const QString &reason) {
        if (m_pendingCommand) { m_uncertain = true; m_commandStatus = QStringLiteral("执行结果未知：") + reason; }
        m_purpose = None; invalidate(); m_status = reason; emit event(reason); emit changed();
    });
    connect(&m_tick, &QTimer::timeout, this, &NestPlc::tick); m_tick.start(250);
}
bool NestPlc::reject(const QString &reason) { m_status = reason; emit event(reason); emit changed(); return false; }
bool NestPlc::configure(const QVariantMap &configuration) {
    if (connected() || connecting() || m_pendingCommand) return reject(QStringLiteral("请先断开连接并处理待确认命令，再修改配置"));
    QVariantMap next = defaults;
    const auto host = configuration.value("host", defaults.value("host")).toString().trimmed();
    if (host.isEmpty() || host.contains(QRegularExpression("[\\s/:]"))) return reject(QStringLiteral("主机地址无效，请填写 IPv4 或主机名"));
    next["host"] = host;
    for (const auto &key : {"port", "unit", "statusFunction", "statusAddress", "commandAddress"}) {
        QString text = configuration.value(key, defaults.value(key)).toString().trimmed();
        if (text.isEmpty() && QString(key).endsWith("Address")) text = "-1";
        bool ok = false; const int value = text.toInt(&ok);
        const int min = QString(key).endsWith("Address") ? -1 : QString(key) == "unit" ? 0 : 1;
        const int max = QString(key) == "unit" ? 255 : QString(key) == "statusFunction" ? 4 : QString(key) == "statusAddress" ? 65532 : QString(key) == "commandAddress" ? 65534 : 65535;
        if (!ok || value < min || value > max || (QString(key) == "statusFunction" && value != 3 && value != 4))
            return reject(QStringLiteral("配置无效：%1").arg(key));
        next[key] = value;
    }
    const int feedback = next.value("statusAddress").toInt(), command = next.value("commandAddress").toInt();
    if (next.value("statusFunction").toInt() == 3 && feedback >= 0 && command >= 0 && command < feedback + 4 && feedback < command + 2)
        return reject(QStringLiteral("命令邮箱与反馈寄存器不能重叠"));
    m_config = next;
    QSettings settings; settings.setValue("NestPlcV1/configuration", QJsonDocument(QJsonObject::fromVariantMap(next)).toJson(QJsonDocument::Compact)); settings.sync();
    m_status = QStringLiteral("PLC 配置已保存到本机；地址为空表示尚未分配"); emit changed(); return true;
}
void NestPlc::invalidate() {
    m_snapshot = false; m_heartbeatAdvanced = false; m_feedbackAt = -1; m_heartbeatAt = -1;
    m_writeEnabled = false; m_monitoring = false;
}
void NestPlc::connectPlc() {
    if (connected() || connecting()) return;
    invalidate(); m_purpose = None; m_status.clear(); m_readResult = QStringLiteral("本次连接尚未读取寄存器");
    m_client.connectToHost(m_config.value("host").toString(), quint16(m_config.value("port").toInt()), quint8(m_config.value("unit").toInt()));
}
void NestPlc::disconnectPlc() {
    if (m_pendingCommand) { m_uncertain = true; m_commandStatus = QStringLiteral("连接已断开，命令执行结果未知；请检查现场"); }
    m_purpose = None; invalidate(); m_status.clear(); m_client.disconnectFromHost(); emit changed();
}
bool NestPlc::identify() {
    if (!connected() || busy()) return reject(QStringLiteral("请先建立 TCP 连接并等待当前请求结束"));
    m_purpose = Identification; m_status.clear(); return m_client.identify();
}
bool NestPlc::readRegisters(int function, int address, int count) {
    if (!connected() || busy()) return reject(QStringLiteral("未连接或当前有请求正在执行"));
    if (function < 1 || function > 4 || address < 0 || count < 1 || count > (function <= 2 ? 2000 : 125) || address > 65535 || count > 65536 - address)
        return reject(QStringLiteral("读取参数无效：功能码 01–04，地址 0–65535，位最多 2000 / 寄存器最多 125，范围不可越界"));
    m_readCount = count; m_purpose = ManualRead; m_status.clear(); m_readResult = QStringLiteral("读取中…"); return m_client.read(function, address, count);
}
bool NestPlc::setMonitoring(bool enabled) {
    if (!enabled) { m_monitoring = false; m_snapshot = false; m_writeEnabled = false; emit changed(); return true; }
    if (!connected() || m_config.value("statusAddress").toInt() < 0) return reject(QStringLiteral("请连接并填写已确认的四个连续反馈寄存器地址"));
    m_monitoring = true; m_lastPoll = -1000; emit changed(); return true;
}
bool NestPlc::feedbackFresh() const {
    return connected() && m_monitoring && m_snapshot && m_feedbackAt >= 0 && m_clock.elapsed() - m_feedbackAt < 2000
        && m_heartbeatAdvanced && m_clock.elapsed() - m_heartbeatAt < 5000;
}
bool NestPlc::canMove() const {
    return canStop() && !m_uncertain && !m_pendingCommand && !(m_flags & 0x7c) && m_fault == 0 && (m_flags & 0x80);
}
bool NestPlc::canStop() const {
    return m_writeEnabled && feedbackFresh() && !busy() && m_config.value("commandAddress").toInt() >= 0;
}
bool NestPlc::setWriteEnabled(bool enabled) {
    if (!enabled) { m_writeEnabled = false; emit changed(); return true; }
    if (!feedbackFresh() || m_uncertain || m_config.value("commandAddress").toInt() < 0)
        return reject(QStringLiteral("写入未启用：需要已确认的映射、连续有效心跳和无待确认结果"));
    m_writeEnabled = true; m_status = QStringLiteral("本次连接允许命令写入；断开或反馈失效后自动关闭"); emit changed(); return true;
}
QString NestPlc::doorState() const {
    if (!feedbackFresh()) return QStringLiteral("未知 / 反馈未就绪");
    if ((m_flags & 0x40)) return QStringLiteral("急停");
    if ((m_flags & 0x10) || m_fault) return QStringLiteral("故障");
    if (m_flags & 0x20) return QStringLiteral("联锁禁止");
    if (m_flags & 4) return QStringLiteral("正在开启");
    if (m_flags & 8) return QStringLiteral("正在关闭");
    if (m_flags & 1) return QStringLiteral("开启到位");
    if (m_flags & 2) return QStringLiteral("关闭到位");
    return QStringLiteral("中间位置 / 已停止");
}
bool NestPlc::sendCommand(int command) {
    if (command < 1 || command > 3 || (command == 3 ? !canStop() : !canMove())) return reject(QStringLiteral("命令被拒绝：检查写入许可、反馈、联锁、故障或待确认命令"));
    // Sequence is based on the PLC acknowledgement, including after a client restart.
    const int previous = m_pendingCommand ? m_pendingSequence : m_ack;
    m_pendingSequence = previous % 65535 + 1; m_pendingCommand = command; m_commandAt = m_clock.elapsed();
    m_commandStatus = QStringLiteral("命令 %1 / 序号 %2：等待写入响应与 PLC 回执").arg(command).arg(m_pendingSequence);
    m_purpose = Command; m_status.clear();
    const bool sent = m_client.writeMailbox(m_config.value("commandAddress").toInt(), command, quint16(m_pendingSequence));
    if (sent) { emit event(m_commandStatus); emit changed(); }
    return sent;
}
bool NestPlc::acknowledgeStopped() {
    if (!feedbackFresh() || busy() || (m_flags & 0x0c) || (m_pendingCommand && m_ack != m_pendingSequence))
        return reject(QStringLiteral("需要有效的静止反馈与匹配的命令回执；可先发送停止并等待回执"));
    m_pendingCommand = 0; m_uncertain = false; m_writeEnabled = false;
    m_commandStatus = QStringLiteral("已由操作员确认现场停止；需要重新启用写入"); emit event(m_commandStatus); emit changed(); return true;
}
void NestPlc::tick() {
    if (m_writeEnabled && !feedbackFresh()) { m_writeEnabled = false; m_status = QStringLiteral("反馈或 PLC 心跳失效，写入许可已关闭"); }
    if (m_pendingCommand && !m_uncertain) {
        const qint64 elapsed = m_clock.elapsed() - m_commandAt;
        if ((m_ack != m_pendingSequence && elapsed > 3000) || elapsed > 30000) {
            m_uncertain = true; m_commandStatus = QStringLiteral("PLC 回执或舱门到位超时，执行结果未知；检查现场，可发送停止"); emit event(m_commandStatus);
        }
    }
    if (m_monitoring && connected() && !busy() && m_clock.elapsed() - m_lastPoll >= 500) {
        m_lastPoll = m_clock.elapsed(); m_purpose = Feedback;
        m_client.read(m_config.value("statusFunction").toInt(), m_config.value("statusAddress").toInt(), 4);
    }
    emit changed();
}
void NestPlc::complete(int function, const QByteArray &payload, int exception) {
    const Purpose purpose = m_purpose; m_purpose = None; m_status.clear();
    if (exception) {
        const QString message = QStringLiteral("Modbus 有效异常响应 0x%1：%2").arg(exception, 2, 16, QChar('0'))
            .arg(exception == 1 ? QStringLiteral("不支持的功能码") : exception == 2 ? QStringLiteral("地址不存在") : exception == 3 ? QStringLiteral("数据值无效") : QStringLiteral("PLC 拒绝请求"));
        m_status = message;
        if (purpose == ManualRead) m_readResult = message;
        if (purpose == Feedback) { m_snapshot = false; m_writeEnabled = false; }
        if (purpose == Command) { m_uncertain = true; m_commandStatus = message + QStringLiteral("；需现场确认"); }
        emit event(message); emit changed(); return;
    }
    if (purpose == ManualRead) {
        QStringList values;
        if (function <= 2) {
            for (int i = 0; i < m_readCount; ++i) values << QString::number((quint8(payload.at(1 + i / 8)) >> (i % 8)) & 1);
            m_readResult = QStringLiteral("读取成功（位）：[") + values.join(", ") + "]";
        } else {
            for (int i = 1; i < payload.size(); i += 2) values << QString::number(word(payload, i));
            m_readResult = QStringLiteral("读取成功：[") + values.join(", ") + "]";
        }
    } else if (purpose == Feedback && m_monitoring) {
        const int flags = word(payload, 1), heartbeat = word(payload, 7);
        if ((flags & 3) == 3 || (flags & 12) == 12) {
            m_snapshot = false; m_writeEnabled = false; m_status = QStringLiteral("反馈冲突：双限位或双向运动同时有效"); emit changed(); return;
        }
        const auto now = m_clock.elapsed();
        if (m_feedbackAt < 0) { m_heartbeatAt = now; m_heartbeatAdvanced = false; }
        else if (heartbeat != m_heartbeat) { m_heartbeatAt = now; m_heartbeatAdvanced = true; }
        m_flags = flags; m_fault = word(payload, 3); m_ack = word(payload, 5); m_heartbeat = heartbeat;
        m_feedbackAt = now; m_snapshot = true;
        if (m_pendingCommand && m_ack == m_pendingSequence) {
            const bool done = !(flags & 12) && ((m_pendingCommand == 1 && (flags & 1)) || (m_pendingCommand == 2 && (flags & 2)) || m_pendingCommand == 3);
            if (done && !m_uncertain && !(flags & 0x70) && !m_fault) {
                m_pendingCommand = 0; m_commandStatus = QStringLiteral("PLC 已确认，动作完成（来自反馈）"); emit event(m_commandStatus);
            } else if (!m_uncertain) m_commandStatus = QStringLiteral("PLC 已确认命令 %1，等待到位反馈").arg(m_pendingSequence);
        }
    } else if (purpose == Command) {
        m_commandStatus = QStringLiteral("邮箱写入已响应，等待 PLC 业务回执与到位反馈");
    }
    emit changed();
}
