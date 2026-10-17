#pragma once
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>

class QNetworkReply;

// Separate from the read-only point-cloud stream and the flight task gateway.
class MappingStarter : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit MappingStarter(QObject *parent = nullptr);
    ~MappingStarter() override;
    bool autoStart() const { return m_autoStart; }
    bool busy() const { return m_running; }
    QString status() const { return m_status; }
    void setAutoStart(bool enabled);
    void setSuspended(bool suspended);
    void setEndpoint(const QString &host, int port);
    Q_INVOKABLE void start();
    void startIfEnabled();
signals:
    void changed();
private:
    void request();
    void cancel();
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_poll;
    QString m_host = QStringLiteral("127.0.0.1"), m_status;
    int m_port = 37263;
    bool m_autoStart = true, m_running = false, m_ensureNext = true, m_suspended = false;
};
