#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QTcpSocket;
class QTimer;

/* 机载电脑自动发现。
 *
 * 背景：机载电脑用 DHCP，地址会变（实测三天内变了三次：.113 → .109 → .112），
 * 每次都要手动改地址。这里让地面站自己扫出来。
 *
 * 判据（认准两个特征端口，见 GIMBAL_CONTROL.md §8.1）：
 *     8554  TCP  MediaMTX 图传中转
 *     37261 TCP  照片/视频文件代理（中转服务内置）
 * 只开 22 端口的不算 —— 曾经有别的设备抢了旧 IP，22 开着但服务全无。
 *
 * 扫描范围：
 *   * 「上次用过的地址」所在 /24（关键：地面站可能在 NAT 后面，
 *     自己的网卡网段和机载电脑并不相同，例如虚拟机在 192.168.85.x
 *     而机载电脑在 192.168.2.x）
 *   * 本机各网卡的 /24（地面站直接接在同一网段时用得上）
 *
 * 实现：并发 TCP 探测，分批 + 超时，典型 /24 约 1~3 秒扫完。
 */
class RelayDiscovery final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString foundHost READ foundHost NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY changed)

public:
    explicit RelayDiscovery(QObject *parent = nullptr);

    bool scanning() const { return m_scanning; }
    QString status() const { return m_status; }
    QString foundHost() const { return m_foundHost; }
    int progress() const { return m_progress; }

    /* 开始扫描。lastKnownHost 是当前配置里的地址（可为空）；
     * 会优先扫它所在的 /24。 */
    Q_INVOKABLE void scan(const QString &lastKnownHost);
    Q_INVOKABLE void cancel();

signals:
    void changed();
    void found(const QString &host);            // 找到并已验证
    void finished(bool ok, const QString &message);

private:
    void beginBatch();
    void finishBatch();
    void probeCandidates(const QStringList &hosts, quint16 port,
                         const std::function<void(const QString &)> &onHit);
    void setStatus(const QString &text);

    static QStringList expandSubnet(const QString &base);
    static QString localAddressFor(const QString &base);

    QList<QString> m_pending;      // 待探测（8554）
    QStringList m_hits;            // 8554 命中的候选（再验 37261）
    QTcpSocket *m_socket{nullptr};
    QTimer *m_batchTimer{nullptr};
    QString m_foundHost;
    QString m_status{QStringLiteral("未扫描")};
    bool m_scanning{false};
    int m_progress{0};
    int m_total{0};
    int m_verifyRemaining{0};   // 二次确认阶段的剩余计数（异步，必须在成员上）
};
