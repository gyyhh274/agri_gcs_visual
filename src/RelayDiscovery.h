#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class QTcpSocket;
class QTimer;

/* 机载电脑自动发现 + 服务自检。
 *
 * 背景：机载电脑用 DHCP，地址会变；早期只判断「TCP 连得上」，
 * 结果在有透明代理/NAT 的环境里，任何 IP 的任意端口都"连得上"，
 * 误把幽灵地址写成机载电脑（实测 192.168.2.2 被写入配置）。
 * 现在所有判定都必须通过协议握手：SSH 要读到 "SSH-2.0"，
 * 图传要读到 "RTSP/1.0"，照片代理要读到合法 JSON。
 *
 * 扫描范围：上次地址所在 /24 + 本机各网卡 /24。
 */
class RelayDiscovery final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString foundHost READ foundHost NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY changed)

    /* 逐项自检（「测试连接」按钮用）。
     * services 是 [{key,label,endpoint,state,detail}]，
     * state ∈ pending / ok / fail / skip。 */
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(QVariantList services READ services NOTIFY changed)
    Q_PROPERTY(QString checkSummary READ checkSummary NOTIFY changed)

public:
    explicit RelayDiscovery(QObject *parent = nullptr);

    bool scanning() const { return m_scanning; }
    QString status() const { return m_status; }
    QString foundHost() const { return m_foundHost; }
    int progress() const { return m_progress; }
    bool checking() const { return m_checking; }
    QVariantList services() const { return m_services; }
    QString checkSummary() const { return m_checkSummary; }

    /* 开始扫描。lastKnownHost 是当前配置里的地址（可为空）；
     * 会优先扫它所在的 /24。 */
    Q_INVOKABLE void scan(const QString &lastKnownHost);
    Q_INVOKABLE void cancel();
    /* 对 host 做一次逐项协议自检。user 目前只用于摘要文案，
     * SSH 认证本身由隧道服务持有。 */
    Q_INVOKABLE void checkAll(const QString &host, const QString &user);

signals:
    void changed();
    void found(const QString &host);            // 找到并已验证
    void finished(bool ok, const QString &message);

private:
    void beginScan();
    void beginBatch();
    void finishBatch();
    void finishChecks();
    void probeCandidates(const QStringList &hosts, quint16 port,
                         const std::function<void(const QString &)> &onHit);
    void setStatus(const QString &text);

    static QStringList expandSubnet(const QString &base);
    static QString localAddressFor(const QString &base);

    QList<QString> m_pending;      // 待探测（8554）
    QStringList m_hits;            // 8554 命中的候选（再验 37261）
    QString m_lastKnownHost;       // 本次扫描参考的"上次地址"
    QTcpSocket *m_socket{nullptr};
    QTimer *m_batchTimer{nullptr};
    QString m_foundHost;
    QString m_status{QStringLiteral("未扫描")};
    bool m_scanning{false};
    int m_progress{0};
    int m_total{0};
    int m_verifyRemaining{0};   // 二次确认阶段的剩余计数（异步，必须在成员上）

    bool m_checking{false};
    int m_checksPending{0};
    QVariantList m_services;
    QString m_checkSummary{QStringLiteral("未测试")};
};
