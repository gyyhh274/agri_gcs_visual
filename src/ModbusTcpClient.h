#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QStringList>

// Single in-flight transaction. A timed-out write is never automatically retried.
class ModbusTcpClient : public QObject {
    Q_OBJECT
public:
    explicit ModbusTcpClient(QObject *parent = nullptr);
    bool connected() const { return m_socket.state() == QAbstractSocket::ConnectedState; }
    bool connecting() const { return m_socket.state() == QAbstractSocket::ConnectingState || m_socket.state() == QAbstractSocket::HostLookupState; }
    bool busy() const { return m_function != 0; }
    bool verified() const { return m_verified; }
    QString status() const { return m_status; }
    QStringList log() const { return m_log; }
    void connectToHost(const QString &host, quint16 port, quint8 unit);
    void disconnectFromHost();
    bool identify();
    bool read(int function, int address, int count);
    bool writeMailbox(int address, quint16 command, quint16 sequence);
signals:
    void changed();
    void response(int function, const QByteArray &payload, int exception);
    void failed(const QString &reason);
private:
    bool send(int function, const QByteArray &payload);
    void receive();
    void fail(const QString &reason);
    void trace(const QString &message);
    QTcpSocket m_socket;
    QTimer m_timeout;
    QByteArray m_buffer, m_request;
    QStringList m_log;
    QString m_status = QStringLiteral("未连接");
    quint16 m_transaction = 0;
    quint8 m_unit = 1;
    int m_function = 0;
    bool m_verified = false;
};
