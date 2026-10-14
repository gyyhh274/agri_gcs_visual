#include "RelayDiscovery.h"

#include <QDebug>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QTcpSocket>
#include <QTimer>
#include <functional>

namespace {
constexpr quint16 kVideoPort = 8554;    // MediaMTX 图传中转
constexpr quint16 kPhotoPort = 37261;   // 照片/视频文件代理
constexpr int kBatchSize = 64;          // 每批并发探测数
constexpr int kConnectTimeoutMs = 450;  // 单个地址的探测超时
constexpr int kVerifyTimeoutMs = 700;   // 二次确认超时

void trace(const QString &message)
{
    qInfo("[RelayDiscovery] %s", qPrintable(message));
}
} // namespace

RelayDiscovery::RelayDiscovery(QObject *parent) : QObject(parent) {}

QStringList RelayDiscovery::expandSubnet(const QString &base)
{
    QStringList list;
    list.reserve(254);
    for (int i = 1; i <= 254; ++i)
        list.append(base + QLatin1Char('.') + QString::number(i));
    return list;
}

QString RelayDiscovery::localAddressFor(const QString &base)
{
    const QString prefix = base + QLatin1Char('.');
    for (const QHostAddress &address : QNetworkInterface::allAddresses()) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol)
            continue;
        const QString ip = address.toString();
        if (ip.startsWith(prefix))
            return ip;
    }
    return {};
}

void RelayDiscovery::setStatus(const QString &text)
{
    if (m_status == text)
        return;
    m_status = text;
    emit changed();
}

void RelayDiscovery::cancel()
{
    if (m_batchTimer)
        m_batchTimer->stop();
    if (m_socket) {
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_scanning = false;
    setStatus(QStringLiteral("已取消扫描"));
    emit changed();
}

void RelayDiscovery::scan(const QString &lastKnownHost)
{
    if (m_scanning)
        return;

    // ── 收集候选网段 ──────────────────────────────────────────
    QStringList bases;
    const auto addBase = [&bases](const QString &ip) {
        const QStringList parts = ip.split(QLatin1Char('.'));
        if (parts.size() != 4)
            return;
        const QString base = parts.at(0) + QLatin1Char('.') + parts.at(1)
                             + QLatin1Char('.') + parts.at(2);
        if (!bases.contains(base))
            bases.append(base);
    };

    // 1) 上次用过的地址所在网段（地面站在 NAT 后时，这个最要紧）
    if (!lastKnownHost.trimmed().isEmpty())
        addBase(lastKnownHost.trimmed());

    // 2) 本机各网卡的网段
    for (const QHostAddress &address : QNetworkInterface::allAddresses()) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback())
            continue;
        addBase(address.toString());
    }

    if (bases.isEmpty()) {
        setStatus(QStringLiteral("找不到可扫描的网段"));
        emit finished(false, m_status);
        return;
    }

    m_pending.clear();
    for (const QString &base : bases) {
        const QString self = localAddressFor(base);
        for (const QString &ip : expandSubnet(base)) {
            if (!self.isEmpty() && ip == self)
                continue;                       // 跳过自己
            m_pending.append(ip);
        }
    }

    m_hits.clear();
    m_foundHost.clear();
    m_progress = 0;
    m_total = m_pending.size();
    m_scanning = true;
    setStatus(QStringLiteral("正在扫描 %1 个地址（网段：%2）…")
                  .arg(m_total)
                  .arg(bases.join(QStringLiteral(", "))));
    trace(QStringLiteral("候选网段: %1，共 %2 个地址").arg(bases.join(',')).arg(m_total));
    emit changed();

    if (!m_batchTimer) {
        m_batchTimer = new QTimer(this);
        m_batchTimer->setSingleShot(true);
        connect(m_batchTimer, &QTimer::timeout, this, &RelayDiscovery::finishBatch);
    }
    beginBatch();
}

// 探测一个端口：连上即算命中
static void probeOnce(const QString &host, quint16 port, int timeoutMs,
                      std::function<void(bool)> done)
{
    auto *socket = new QTcpSocket;
    auto *timer = new QTimer(socket);
    timer->setSingleShot(true);

    QObject::connect(socket, &QTcpSocket::connected, socket, [socket, timer, done]() {
        timer->stop();
        done(true);
        socket->abort();
        socket->deleteLater();
    });
    QObject::connect(socket, &QTcpSocket::errorOccurred, socket,
                     [socket, timer, done](QAbstractSocket::SocketError) {
        timer->stop();
        done(false);
        socket->deleteLater();
    });
    QObject::connect(timer, &QTimer::timeout, socket, [socket, done]() {
        done(false);
        socket->abort();
        socket->deleteLater();
    });

    timer->start(timeoutMs);
    socket->connectToHost(host, port);
}

void RelayDiscovery::beginBatch()
{
    if (m_pending.isEmpty()) {
        // 第一轮（8554）结束 → 对命中项做二次确认（37261）
        if (m_hits.isEmpty()) {
            m_scanning = false;
            setStatus(QStringLiteral("扫描完成：没有找到机载电脑（%1 个地址都没响应）").arg(m_total));
            trace(QStringLiteral("扫描结束，无命中"));
            emit changed();
            emit finished(false, m_status);
            return;
        }
        setStatus(QStringLiteral("发现 %1 个候选，正在确认…").arg(m_hits.size()));
        const QStringList candidates = m_hits;
        const int candidateCount = candidates.size();
        m_hits.clear();
        // ⚠️ 计数必须放在成员上：探测是异步的，用栈变量做引用捕获会悬垂
        m_verifyRemaining = candidateCount;
        for (const QString &host : candidates) {
            probeOnce(host, kPhotoPort, kVerifyTimeoutMs, [this, host, candidateCount](bool ok) {
                if (ok && m_foundHost.isEmpty()) {
                    m_foundHost = host;
                    trace(QStringLiteral("确认机载电脑: %1（8554 + %2 都在）").arg(host).arg(kPhotoPort));
                    m_scanning = false;
                    setStatus(QStringLiteral("已找到机载电脑：%1").arg(host));
                    emit changed();
                    emit found(host);
                    emit finished(true, m_status);
                }
                if (--m_verifyRemaining <= 0 && m_foundHost.isEmpty()) {
                    m_scanning = false;
                    setStatus(QStringLiteral("找到 %1 个图传端口，但没有一个是机载电脑"
                                             "（缺少 %2 文件代理）")
                                  .arg(candidateCount).arg(kPhotoPort));
                    emit changed();
                    emit finished(false, m_status);
                }
            });
        }
        return;
    }

    // 取一批
    QStringList batch;
    while (!m_pending.isEmpty() && batch.size() < kBatchSize)
        batch.append(m_pending.takeFirst());

    m_progress += batch.size();
    setStatus(QStringLiteral("正在扫描… %1/%2").arg(m_progress).arg(m_total));
    emit changed();

    for (const QString &host : batch) {
        probeOnce(host, kVideoPort, kConnectTimeoutMs, [this, host](bool ok) {
            if (ok && !m_hits.contains(host))
                m_hits.append(host);
        });
    }
    m_batchTimer->start(kConnectTimeoutMs + 80);
}

void RelayDiscovery::finishBatch()
{
    if (!m_scanning)
        return;
    beginBatch();
}
