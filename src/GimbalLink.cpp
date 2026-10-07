#include "GimbalLink.h"

#include <QDateTime>
#include <QHostAddress>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace {
constexpr quint8 kHeader0 = 0x55;
constexpr quint8 kHeader1 = 0x66;

// SDK 命令号（SIYI Gimbal Camera External SDK V0.1.1）
constexpr quint8 kCmdZoomStep = 0x05;
constexpr quint8 kCmdRotate = 0x07;
constexpr quint8 kCmdCenter = 0x08;
constexpr quint8 kCmdSystemInfo = 0x0A;
constexpr quint8 kCmdCameraFunction = 0x0C;
constexpr quint8 kCmdGetAttitude = 0x0D;
constexpr quint8 kCmdSetAttitude = 0x0E;
constexpr quint8 kCmdZoomAbsolute = 0x0F;
constexpr quint8 kCmdMaxZoom = 0x16;
constexpr quint8 kCmdCurrentZoom = 0x18;
constexpr quint8 kCmdGimbalMode = 0x19;
constexpr quint8 kCmdSoftReboot = 0x80;

// 0x0C 的功能码
constexpr quint8 kFuncPhoto = 0x00;
constexpr quint8 kFuncRecord = 0x02;
constexpr quint8 kFuncModeLock = 0x03;
constexpr quint8 kFuncModeFollow = 0x04;
constexpr quint8 kFuncModeFpv = 0x05;
constexpr quint8 kFuncLookDown = 0x09;

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

double clampValue(double value, double low, double high)
{
    return std::max(low, std::min(high, value));
}
} // namespace

GimbalLink::GimbalLink(QObject *parent) : QObject(parent)
{
    connect(&m_socket, &QUdpSocket::readyRead, this, &GimbalLink::onReadyRead);

    m_pollTimer.setInterval(1000);
    connect(&m_pollTimer, &QTimer::timeout, this, &GimbalLink::onPoll);

    m_maintenanceTimer.setInterval(400);
    connect(&m_maintenanceTimer, &QTimer::timeout, this, &GimbalLink::onMaintenance);
    m_maintenanceTimer.start();

    // 滑条拖动会高频触发 setAttitude，这里合并成约 12 Hz 下发，避免淹没相机
    m_attitudeFlushTimer.setInterval(80);
    m_attitudeFlushTimer.setSingleShot(true);
    connect(&m_attitudeFlushTimer, &QTimer::timeout, this, &GimbalLink::flushAttitudeTarget);

    // 拨杆是流式速度指令，合并到约 22 Hz
    m_rotateTimer.setInterval(kRotateFlushMs);
    m_rotateTimer.setSingleShot(true);
    connect(&m_rotateTimer, &QTimer::timeout, this, &GimbalLink::flushRotate);
}

QString GimbalLink::modeName() const
{
    switch (m_mode) {
    case 0:
        return QStringLiteral("锁定");
    case 1:
        return QStringLiteral("跟随");
    case 2:
        return QStringLiteral("FPV");
    default:
        return QStringLiteral("未知");
    }
}

QString GimbalLink::recordStatusName() const
{
    switch (m_recordStatus) {
    case 0:
        return QStringLiteral("未录像");
    case 1:
        return QStringLiteral("录像中");
    case 2:
        return QStringLiteral("无 TF 卡");
    case 3:
        return QStringLiteral("录像丢数据");
    default:
        return QStringLiteral("未知");
    }
}

quint16 GimbalLink::crc16Ccitt(const QByteArray &data)
{
    quint16 crc = 0;
    for (char byte : data) {
        crc ^= static_cast<quint16>(static_cast<quint8>(byte)) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? static_cast<quint16>((crc << 1) ^ 0x1021)
                                 : static_cast<quint16>(crc << 1);
    }
    return crc;
}

QByteArray GimbalLink::buildFrame(quint8 command, const QByteArray &payload, quint16 sequence)
{
    QByteArray frame;
    frame.reserve(8 + payload.size() + 2);
    frame.append(static_cast<char>(kHeader0));
    frame.append(static_cast<char>(kHeader1));
    frame.append(static_cast<char>(0x01)); // control: 需要应答
    frame.append(static_cast<char>(payload.size() & 0xFF));
    frame.append(static_cast<char>((payload.size() >> 8) & 0xFF));
    frame.append(static_cast<char>(sequence & 0xFF));
    frame.append(static_cast<char>((sequence >> 8) & 0xFF));
    frame.append(static_cast<char>(command));
    frame.append(payload);
    const quint16 crc = crc16Ccitt(frame);
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

bool GimbalLink::parseFrame(const QByteArray &datagram, quint8 *command, QByteArray *payload)
{
    if (datagram.size() < 10)
        return false;
    if (static_cast<quint8>(datagram.at(0)) != kHeader0 ||
        static_cast<quint8>(datagram.at(1)) != kHeader1)
        return false;

    const int payloadLength = static_cast<quint8>(datagram.at(3)) |
                              (static_cast<quint8>(datagram.at(4)) << 8);
    if (datagram.size() != 8 + payloadLength + 2)
        return false;

    const quint16 expected = static_cast<quint8>(datagram.at(datagram.size() - 2)) |
                             (static_cast<quint8>(datagram.at(datagram.size() - 1)) << 8);
    if (crc16Ccitt(datagram.left(datagram.size() - 2)) != expected)
        return false;

    *command = static_cast<quint8>(datagram.at(7));
    *payload = datagram.mid(8, payloadLength);
    return true;
}

void GimbalLink::setStatus(const QString &text)
{
    if (m_status == text)
        return;
    m_status = text;
    emit changed();
}

void GimbalLink::connectToGimbal(const QString &host, int port)
{
    if (host.trimmed().isEmpty() || port <= 0 || port > 65535) {
        setStatus(QStringLiteral("云台地址无效"));
        return;
    }
    // 单客户端模型：同一个 socket 一直用下去，不重连、不变更源端口
    if (!m_bound) {
        m_socket.bind(QHostAddress(QHostAddress::AnyIPv4), 0);
        m_bound = true;
    }
    m_socket.connectToHost(QHostAddress(host), static_cast<quint16>(port));
    // QUdpSocket::connectToHost 只设置默认目标；用 writeDatagram 发送更直观
    m_socket.disconnectFromHost();

    m_endpoint = QStringLiteral("%1:%2").arg(host).arg(port);
    m_pending.clear();
    m_pollTimer.start();
    setStatus(QStringLiteral("正在连接云台 %1").arg(m_endpoint));
    emit changed();
    refreshAttitude();
    refreshStatus();
}

void GimbalLink::disconnectFromGimbal()
{
    m_pollTimer.stop();
    m_pending.clear();
    m_attitudeFlushTimer.stop();
    m_targetDirty = false;
    // 断连前先把拨杆速度归零，避免云台继续转动
    m_pendingYawRate = 0;
    m_pendingPitchRate = 0;
    m_rotateDirty = false;
    m_rotateTimer.stop();
    resetState();
    m_status = QStringLiteral("已断开云台链路");
    emit changed();
}

void GimbalLink::resetState()
{
    m_connected = false;
    m_attitudeFresh = false;
    emit attitudeChanged();
}

void GimbalLink::sendCommand(quint8 command, const QByteArray &payload, bool expectAck)
{
    if (m_endpoint.isEmpty()) {
        setStatus(QStringLiteral("未设置云台地址"));
        return;
    }
    const quint16 sequence = m_sequence++;
    const QByteArray frame = buildFrame(command, payload, sequence);
    const qint64 written =
        m_socket.writeDatagram(frame, QHostAddress(m_endpoint.section(':', 0, 0)),
                               static_cast<quint16>(m_endpoint.section(':', 1).toUShort()));
    if (written != frame.size()) {
        setStatus(QStringLiteral("云台指令发送失败：%1").arg(m_socket.errorString()));
        return;
    }
    if (expectAck) {
        Pending pending;
        pending.payload = payload;   // 保存原载荷，重试时原样重发
        pending.sentAtMs = nowMs();
        pending.attempts = 1;
        m_pending.insert(command, pending);
        emit changed();
    }
}

void GimbalLink::onReadyRead()
{
    while (m_socket.hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(m_socket.pendingDatagramSize()));
        m_socket.readDatagram(datagram.data(), datagram.size());

        quint8 command = 0;
        QByteArray payload;
        if (!parseFrame(datagram, &command, &payload))
            continue;

        m_pending.remove(command);
        m_connected = true;
        decodePayload(command, payload);
        emit changed();
    }
}

void GimbalLink::decodePayload(quint8 command, const QByteArray &payload)
{
    switch (command) {
    case kCmdGetAttitude:
        // int16 yaw, pitch, roll, yawV, pitchV, rollV（0.1° 精度）
        // 注意 yaw 取反：相机回报的 yaw 正值是逆时针（向左），
        // 本类对外统一成「正值 = 向右」，与拨杆方向一致。
        if (payload.size() >= 6) {
            const auto raw = [&payload](int index) {
                return static_cast<qint16>(static_cast<quint8>(payload.at(index)) |
                                           (static_cast<quint8>(payload.at(index + 1)) << 8));
            };
            noteAttitude(-raw(0) / 10.0, raw(2) / 10.0, raw(4) / 10.0);
        }
        break;
    case kCmdSetAttitude:
        // 应答返回当前实际 yaw / pitch（0.1°），yaw 同样取反
        if (payload.size() >= 4) {
            const auto raw = [&payload](int index) {
                return static_cast<qint16>(static_cast<quint8>(payload.at(index)) |
                                           (static_cast<quint8>(payload.at(index + 1)) << 8));
            };
            noteAttitude(-raw(0) / 10.0, raw(2) / 10.0, m_roll);
        }
        break;
    case kCmdSystemInfo:
        // 字节[3] = 录像状态
        if (payload.size() >= 4) {
            const int status = static_cast<quint8>(payload.at(3));
            if (status != m_recordStatus) {
                m_recordStatus = status;
                emit changed();
            }
        }
        break;
    case kCmdGimbalMode:
        if (payload.size() >= 1) {
            const int mode = static_cast<quint8>(payload.at(0));
            if (mode != m_mode) {
                m_mode = mode;
                emit changed();
            }
        }
        break;
    case kCmdCurrentZoom:
    case kCmdMaxZoom:
        if (payload.size() >= 2) {
            const double value = static_cast<quint8>(payload.at(0)) +
                                 static_cast<quint8>(payload.at(1)) / 10.0;
            if (command == kCmdCurrentZoom && std::abs(value - m_zoom) > 0.001) {
                m_zoom = value;
                emit changed();
            } else if (command == kCmdMaxZoom && std::abs(value - m_zoomMax) > 0.001) {
                m_zoomMax = value;
                emit changed();
            }
        }
        break;
    default:
        break;
    }
}

void GimbalLink::noteAttitude(double yaw, double pitch, double roll)
{
    m_yaw = yaw;
    m_pitch = pitch;
    m_roll = roll;
    m_attitudeAtMs = nowMs();
    m_attitudeFresh = true;
    emit attitudeChanged();
}

void GimbalLink::onPoll()
{
    if (m_endpoint.isEmpty())
        return;
    // 姿态是最需要刷新的量；其余状态低频查询即可
    sendCommand(kCmdGetAttitude, QByteArray());
    static int tick = 0;
    if (++tick % 3 == 0)
        sendCommand(kCmdSystemInfo, QByteArray());
    if (tick % 5 == 0) {
        sendCommand(kCmdGimbalMode, QByteArray());
        sendCommand(kCmdCurrentZoom, QByteArray());
        sendCommand(kCmdMaxZoom, QByteArray());
    }
}

void GimbalLink::retryPending()
{
    const qint64 current = nowMs();
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        Pending &pending = it.value();
        if (current - pending.sentAtMs < kAckTimeoutMs) {
            ++it;
            continue;
        }
        if (pending.attempts >= kMaxAttempts) {
            m_connected = false;
            m_attitudeFresh = false;
            it = m_pending.erase(it);
            setStatus(QStringLiteral("云台无应答（%1）").arg(m_endpoint));
            emit attitudeChanged();
            continue;
        }
        pending.attempts += 1;
        pending.sentAtMs = current;
        // 原样重发原载荷。曾经这里发的是空载荷，导致：
        //   0x0E 重试 → 变成"回中"（yaw/pitch 都归 0），云台乱跑；
        //   0x0F 重试 → 变成"变倍 1.0x"。
        sendCommand(static_cast<quint8>(it.key()), pending.payload, false);
        ++it;
    }
}

void GimbalLink::setRotateRate(int yawRate, int pitchRate)
{
    const int yaw = std::max(-100, std::min(100, yawRate));
    const int pitch = std::max(-100, std::min(100, pitchRate));
    m_lastRotateInputMs = nowMs();
    m_rotateIdle = false;
    if (yaw == m_pendingYawRate && pitch == m_pendingPitchRate && m_rotateDirty)
        return;                       // 已经有同样的值在等待下发
    m_pendingYawRate = yaw;
    m_pendingPitchRate = pitch;
    m_rotateDirty = true;
    if (!m_rotateTimer.isActive())
        m_rotateTimer.start();
}

void GimbalLink::stopRotate()
{
    m_pendingYawRate = 0;
    m_pendingPitchRate = 0;
    m_rotateDirty = true;
    m_rotateIdle = true;
    m_lastRotateInputMs = nowMs();
    if (!m_rotateTimer.isActive())
        m_rotateTimer.start();
}

void GimbalLink::flushRotate()
{
    if (!m_rotateDirty)
        return;
    m_rotateDirty = false;
    m_lastYawRate = m_pendingYawRate;
    m_lastPitchRate = m_pendingPitchRate;

    // 0x07 用 int8 表示 -100~100 的转向速度；实时流式控制不等 ACK
    //
    // 方向约定（2026-10-07 两次实测后确认，勿再"修正"）：
    //   线上正值 = 相机向右转（顺时针俯视），与 SDK 文档「向右滑动 0~100」一致。
    //
    //   注意相机【回报的角度】是另一套约定：正值 = 逆时针（向左）。
    //   所以"发正值 → 回报 yaw 变小"是正常的，不代表方向反了。
    //   曾经因为只看回报角度就误判文档有误、加了取反，导致拨杆左右颠倒。
    //   判定方向要看的【唯一的真相是画面】：相机右转时画面内容向左移动。
    QByteArray payload;
    payload.append(static_cast<char>(static_cast<qint8>(m_lastYawRate)));
    payload.append(static_cast<char>(static_cast<qint8>(m_lastPitchRate)));
    sendCommand(kCmdRotate, payload, false);
    emit rotateChanged();
}

void GimbalLink::onMaintenance()
{
    retryPending();

    // 死手保护：拨杆停止上报杆量后自动归零，避免界面卡死/断连时云台持续转动
    if (!m_rotateIdle && nowMs() - m_lastRotateInputMs > kRotateDeadmanMs) {
        stopRotate();
    }

    if (m_attitudeFresh && nowMs() - m_attitudeAtMs > kAttitudeStaleMs) {
        m_attitudeFresh = false;
        emit attitudeChanged();
    }
}

void GimbalLink::flushAttitudeTarget()
{
    if (!m_targetDirty)
        return;
    m_targetDirty = false;
    QByteArray payload;
    // yaw 取反：对外「正值 = 向右」，而 0x0E 线上正值是逆时针（向左）
    const qint16 yaw = static_cast<qint16>(std::lround(-m_targetYaw * 10.0));
    const qint16 pitch = static_cast<qint16>(std::lround(m_targetPitch * 10.0));
    payload.append(static_cast<char>(yaw & 0xFF));
    payload.append(static_cast<char>((yaw >> 8) & 0xFF));
    payload.append(static_cast<char>(pitch & 0xFF));
    payload.append(static_cast<char>((pitch >> 8) & 0xFF));
    sendCommand(kCmdSetAttitude, payload);
}

void GimbalLink::setAttitude(double yaw, double pitch)
{
    m_targetYaw = clampValue(yaw, -kYawLimit, kYawLimit);
    m_targetPitch = clampValue(pitch, kPitchMin, kPitchMax);
    m_targetDirty = true;
    if (!m_attitudeFlushTimer.isActive())
        m_attitudeFlushTimer.start();
}

void GimbalLink::jog(double deltaYaw, double deltaPitch)
{
    const double baseYaw = m_attitudeFresh ? m_yaw : m_targetYaw;
    const double basePitch = m_attitudeFresh ? m_pitch : m_targetPitch;
    setAttitude(baseYaw + deltaYaw, basePitch + deltaPitch);
}

void GimbalLink::centerGimbal()
{
    sendCommand(kCmdCenter, QByteArray(1, static_cast<char>(0x01)), false);
    emit notified(QStringLiteral("已发送云台回中指令"));
    refreshAttitude();
}

void GimbalLink::lookDown()
{
    sendCommand(kCmdCameraFunction, QByteArray(1, static_cast<char>(kFuncLookDown)), false);
    emit notified(QStringLiteral("已发送一键朝下指令"));
    refreshAttitude();
}

void GimbalLink::setMode(int mode)
{
    if (mode < 0 || mode > 2) {
        emit notified(QStringLiteral("云台模式无效"));
        return;
    }
    const quint8 function = mode == 0 ? kFuncModeLock
                          : mode == 1 ? kFuncModeFollow
                                      : kFuncModeFpv;
    sendCommand(kCmdCameraFunction, QByteArray(1, static_cast<char>(function)), false);
    m_mode = mode;
    emit changed();
    emit notified(QStringLiteral("云台模式已切换为 %1").arg(modeName()));
}

void GimbalLink::takePhoto()
{
    // SDK: 0x0C 系列无 ACK，成败靠后续状态/照片文件判断，不能等应答
    sendCommand(kCmdCameraFunction, QByteArray(1, static_cast<char>(kFuncPhoto)), false);
    emit notified(QStringLiteral("已发送拍照指令"));
}

void GimbalLink::toggleRecording()
{
    // 同理：0x0C/02 是切换式且无 ACK，随后主动查一次录像状态确认
    sendCommand(kCmdCameraFunction, QByteArray(1, static_cast<char>(kFuncRecord)), false);
    emit notified(m_recordStatus == 1 ? QStringLiteral("已发送停止录像指令")
                                      : QStringLiteral("已发送开始录像指令"));
    QTimer::singleShot(1200, this, [this]() { sendCommand(kCmdSystemInfo, QByteArray()); });
}

void GimbalLink::zoomStep(int direction)
{
    const char step = direction >= 0 ? static_cast<char>(0x01) : static_cast<char>(0xFF);
    sendCommand(kCmdZoomStep, QByteArray(1, step), false);
    QTimer::singleShot(600, this, [this]() { sendCommand(kCmdCurrentZoom, QByteArray()); });
}

void GimbalLink::setZoomAbsolute(double multiple)
{
    const double clamped = clampValue(multiple, 1.0, m_zoomMax);
    QByteArray payload;
    payload.append(static_cast<char>(static_cast<int>(clamped) & 0xFF));
    payload.append(static_cast<char>(std::lround((clamped - std::floor(clamped)) * 10.0)));
    sendCommand(kCmdZoomAbsolute, payload);
    m_zoom = clamped;
    emit changed();
}

void GimbalLink::refreshStatus()
{
    sendCommand(kCmdSystemInfo, QByteArray());
    sendCommand(kCmdGimbalMode, QByteArray());
    sendCommand(kCmdCurrentZoom, QByteArray());
    sendCommand(kCmdMaxZoom, QByteArray());
}

void GimbalLink::refreshAttitude()
{
    sendCommand(kCmdGetAttitude, QByteArray());
}

void GimbalLink::softReboot()
{
    QByteArray payload;
    payload.append(static_cast<char>(0x01)); // 相机重启
    payload.append(static_cast<char>(0x01)); // 云台复位
    sendCommand(kCmdSoftReboot, payload, false);
    m_pending.clear();
    emit notified(QStringLiteral("已发送云台软重启指令，约 20~40 秒后恢复"));
}
