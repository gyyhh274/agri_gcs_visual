#include "ModbusTcpClient.h"
#include <QDateTime>
#include <QNetworkProxy>

namespace {
quint16 word(const QByteArray &data, int offset) {
    return (quint16(quint8(data.at(offset))) << 8) | quint8(data.at(offset + 1));
}
void appendWord(QByteArray &data, quint16 value) { data.append(char(value >> 8)); data.append(char(value)); }
}
ModbusTcpClient::ModbusTcpClient(QObject *parent) : QObject(parent) {
    m_socket.setProxy(QNetworkProxy::NoProxy);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        fail(busy() && m_function == 16 ? QStringLiteral("写请求超时：执行结果未知，禁止自动重发") : QStringLiteral("连接或请求超时"));
    });
    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        m_timeout.stop(); m_status = QStringLiteral("TCP 已连接，Modbus 待验证"); trace(m_status);
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, &ModbusTcpClient::receive);
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
        if (busy()) { fail(QStringLiteral("请求期间连接断开；写请求执行结果可能未知")); return; }
        m_timeout.stop(); m_verified = false; m_status = QStringLiteral("未连接"); emit changed();
    });
    connect(&m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred), this,
            [this](QAbstractSocket::SocketError) { fail(m_socket.errorString()); });
}
ModbusTcpClient::~ModbusTcpClient() {
    // The socket outlives the log/buffers in member destruction order. Stop it
    // while those members still exist, without emitting callbacks into an owner
    // whose own members may already be destroyed.
    QObject::disconnect(&m_socket, nullptr, this, nullptr);
    QObject::disconnect(&m_timeout, nullptr, this, nullptr);
    m_timeout.stop();
    m_socket.abort();
}
void ModbusTcpClient::trace(const QString &message) {
    m_log.append(QDateTime::currentDateTime().toString("HH:mm:ss.zzz ") + message);
    while (m_log.size() > 100) m_log.removeFirst();
    emit changed();
}
void ModbusTcpClient::connectToHost(const QString &host, quint16 port, quint8 unit) {
    disconnectFromHost(); m_unit = unit; m_status = QStringLiteral("连接中");
    m_socket.connectToHost(host, port); m_timeout.start(2000); emit changed();
}
void ModbusTcpClient::disconnectFromHost() {
    m_function = 0; m_timeout.stop(); m_buffer.clear(); m_verified = false; m_socket.abort();
    m_status = QStringLiteral("未连接"); emit changed();
}
void ModbusTcpClient::fail(const QString &reason) {
    m_function = 0; m_timeout.stop(); m_buffer.clear(); m_verified = false;
    m_socket.abort(); m_status = reason; trace(QStringLiteral("ERROR ") + reason); emit failed(reason);
}
bool ModbusTcpClient::send(int function, const QByteArray &payload) {
    if (!connected() || busy()) return false;
    m_function = function; m_request = payload; ++m_transaction;
    QByteArray frame; appendWord(frame, m_transaction); appendWord(frame, 0); appendWord(frame, payload.size() + 2);
    frame.append(char(m_unit)); frame.append(char(function)); frame.append(payload);
    if (m_socket.write(frame) != frame.size()) { fail(QStringLiteral("发送失败；写请求执行结果可能未知")); return false; }
    m_timeout.start(1500); trace(QStringLiteral("TX ") + frame.toHex(' ')); return true;
}
bool ModbusTcpClient::identify() { return send(43, QByteArray::fromHex("0e0100")); }
bool ModbusTcpClient::read(int function, int address, int count) {
    const int maxCount = function <= 2 ? 2000 : 125;
    if (function < 1 || function > 4 || address < 0 || address > 65535 || count < 1 || count > maxCount || address + count > 65536) return false;
    QByteArray payload; appendWord(payload, address); appendWord(payload, count); return send(function, payload);
}
bool ModbusTcpClient::writeMailbox(int address, quint16 command, quint16 sequence) {
    if (address < 0 || address > 65534) return false;
    QByteArray payload; appendWord(payload, address); appendWord(payload, 2); payload.append(char(4));
    appendWord(payload, command); appendWord(payload, sequence); return send(16, payload);
}
void ModbusTcpClient::receive() {
    m_buffer.append(m_socket.readAll());
    if (m_buffer.size() > 260) { fail(QStringLiteral("响应超过 Modbus TCP 最大帧长")); return; }
    if (m_buffer.size() < 7) return;
    const int length = word(m_buffer, 4);
    if (!busy() || word(m_buffer, 0) != m_transaction || word(m_buffer, 2) != 0 || quint8(m_buffer.at(6)) != m_unit || length < 2 || length > 254) {
        fail(QStringLiteral("无效 MBAP：事务号、协议号、Unit ID 或长度不匹配")); return;
    }
    if (m_buffer.size() < length + 6) return;
    if (m_buffer.size() != length + 6) { fail(QStringLiteral("收到非预期的额外响应数据")); return; }
    const auto frame = m_buffer; m_buffer.clear();
    const int function = quint8(frame.at(7));
    const auto payload = frame.mid(8);
    int exception = 0;
    bool valid = false;
    if (function == (m_function | 0x80)) {
        valid = payload.size() == 1 && quint8(payload.at(0)) != 0;
        if (valid) exception = quint8(payload.at(0));
    } else if (function == m_function) {
        if (function >= 1 && function <= 4) {
            const int count = word(m_request, 2);
            const int bytes = function <= 2 ? (count + 7) / 8 : count * 2;
            valid = payload.size() == bytes + 1 && quint8(payload.at(0)) == bytes;
        } else if (function == 16) {
            valid = payload.size() == 4 && payload == m_request.left(4);
        } else if (function == 43) {
            valid = payload.size() >= 6 && quint8(payload.at(0)) == 14 && quint8(payload.at(1)) == 1
                && ((quint8(payload.at(2)) & 0x7f) >= 1) && ((quint8(payload.at(2)) & 0x7f) <= 3)
                && (quint8(payload.at(3)) == 0 || quint8(payload.at(3)) == 255);
            int offset = 6;
            if (valid) for (int i = 0; i < quint8(payload.at(5)); ++i) {
                if (offset + 2 > payload.size()) { valid = false; break; }
                offset += 2 + quint8(payload.at(offset + 1));
                if (offset > payload.size()) { valid = false; break; }
            }
            valid = valid && offset == payload.size();
        }
    }
    if (!valid) { fail(QStringLiteral("Modbus 响应功能码、数据长度或写入回显不匹配")); return; }
    const int completedFunction = m_function; m_function = 0; m_timeout.stop(); m_verified = true;
    m_status = exception ? QStringLiteral("Modbus 已响应；异常码 0x%1").arg(exception, 2, 16, QChar('0')) : QStringLiteral("Modbus 响应有效");
    trace(QStringLiteral("RX ") + frame.toHex(' ')); emit response(completedFunction, payload, exception);
}
