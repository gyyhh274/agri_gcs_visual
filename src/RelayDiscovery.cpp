#include "RelayDiscovery.h"

#include <QDebug>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace {
constexpr quint16 kVideoPort = 8554;     // MediaMTX 图传中转
constexpr quint16 kPhotoPort = 37261;    // 照片/视频文件代理
constexpr quint16 kSshPort = 22;         // 机载 sshd
constexpr quint16 kGimbalPort = 37260;   // siyi-gimbal-relay（UDP）
constexpr quint16 kPointCloudPort = 37262;
constexpr quint16 kMappingPort = 37263;
constexpr quint16 kTelemetryPort = 8765;

/* 判定分三类，只有「协议对得上」才算命中：
 * 早期只判断 connect 成功，在有透明代理的环境里任何 IP 都会"连得上"，
 * 于是把 192.168.2.2 这种幽灵地址当成了机载电脑。 */
enum class ProbeKind { SshBanner, RtspOptions, PhotoJson, PointCloudHello, MappingStatus, TelemetryJson };

void probeOnce(const QString &host, quint16 port, int timeoutMs,
               std::function<void(bool)> done);
void probePayload(const QString &host, quint16 port, ProbeKind kind, int timeoutMs,
                  std::function<void(bool, const QString &)> done);

constexpr int kBatchSize = 64;          // 每批并发探测数
/* 单个地址的探测超时。
 * ⚠️ 不要调得太小：实测 Wi-Fi 省电模式下链路延迟会到 480ms，
 *    原来的 450ms 会让【所有】地址都超时，扫描结果变成"一个都没找到"，
 *    而目标其实好好的（2026-10-15 实际踩到过）。
 *    900ms 能容忍慢链路，又不至于让扫描慢到不可用。 */
constexpr int kConnectTimeoutMs = 900;
constexpr int kVerifyTimeoutMs = 1200;
/* 协议握手比纯 connect 慢：要发请求、等对端回包，
 * 隧道冷启动时 ssh 转发要几百毫秒，给足 2.6s。 */
constexpr int kPayloadTimeoutMs = 2600;

void trace(const QString &message)
{
    qInfo("[RelayDiscovery] %s", qPrintable(message));
}

QByteArray probeRequest(ProbeKind kind, const QString &host, quint16 port)
{
    switch (kind) {
    case ProbeKind::RtspOptions:
        // OPTIONS 不需要认证；MediaMTX 无凭据时回 401，同样能证明这是 RTSP 服务
        return QByteArrayLiteral("OPTIONS rtsp://") + host.toUtf8() + ':'
               + QByteArray::number(port)
               + QByteArrayLiteral("/a8mini RTSP/1.0\r\nCSeq: 1\r\n"
                                   "User-Agent: agri-gcs\r\n\r\n");
    case ProbeKind::PhotoJson:
        return QByteArrayLiteral("GET /cgi-bin/media.cgi/api/v1/getmedialist?media_type=0"
                                 "&path=101SIYI_IMG&start=0&count=1 HTTP/1.0\r\nHost: ")
               + host.toUtf8() + QByteArrayLiteral("\r\n\r\n");
    case ProbeKind::MappingStatus:
        return QByteArrayLiteral("GET /status HTTP/1.0\r\nHost: ")
               + host.toUtf8() + QByteArrayLiteral("\r\n\r\n");
    default:
        return {};
    }
}

struct Verdict {
    bool settled{false};    // 已有结论（成功，或 final 时失败）
    bool ok{false};
    QString detail;
};

/* final=false：数据还没收全，只在能确定成功时返回 settled；
 * final=true ：没等到匹配的响应，按失败收尾，不能含糊成"未知"。 */
Verdict judge(ProbeKind kind, const QByteArray &rx, bool final)
{
    const QString text = QString::fromUtf8(rx);
    const auto failure = [final, &rx](const QString &reason) {
        if (!final)
            return Verdict{false, false, {}};
        return Verdict{true, false, rx.isEmpty() ? QStringLiteral("无响应") : reason};
    };

    switch (kind) {
    case ProbeKind::SshBanner:
        if (text.startsWith(QStringLiteral("SSH-2.0")))
            return Verdict{true, true, text.section(QLatin1Char('\n'), 0, 0).trimmed().left(48)};
        return failure(QStringLiteral("不是 SSH 服务"));
    case ProbeKind::RtspOptions:
        if (text.contains(QStringLiteral("RTSP/1.0")))
            return Verdict{true, true, text.section(QStringLiteral("\r\n"), 0, 0).trimmed()};
        return failure(QStringLiteral("不是 RTSP 服务"));
    case ProbeKind::PhotoJson:
        if (text.contains(QStringLiteral("\"code\"")) && text.contains(QStringLiteral("list")))
            return Verdict{true, true, QStringLiteral("照片代理就绪")};
        return failure(QStringLiteral("不是照片代理"));
    case ProbeKind::PointCloudHello:
        if (text.startsWith(QStringLiteral("AGM")))
            return Verdict{true, true, text.left(4) + QStringLiteral(" 点云握手")};
        return failure(QStringLiteral("不是点云服务"));
    case ProbeKind::MappingStatus:
        if (text.startsWith(QStringLiteral("HTTP/1.")))
            return Verdict{true, true, QStringLiteral("建图启动服务就绪")};
        return failure(QStringLiteral("不是建图启动服务"));
    case ProbeKind::TelemetryJson:
        if (text.contains(QStringLiteral("\"type\"")) && text.contains(QStringLiteral("telemetry")))
            return Verdict{true, true, QStringLiteral("已收到遥测帧")};
        return failure(QStringLiteral("无遥测数据"));
    }
    return failure(QStringLiteral("未知服务"));
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

    /* ── 快路径：先直接试当前/上次用的地址 ──────────────────────
     * 地址没变是最常见的情况。直接验协议，通过就立刻返回，
     * 不必扫整个网段（既快又不产生几百个连接）。
     * 只有它不通时才退化成全网段扫描。 */
    const QString known = lastKnownHost.trimmed();
    m_lastKnownHost = known;
    if (!known.isEmpty() && known.count(QLatin1Char('.')) == 3) {
        m_scanning = true;
        setStatus(QStringLiteral("正在确认 %1 …").arg(known));
        emit changed();
        trace(QStringLiteral("快路径：先探测已配置地址 %1").arg(known));
        probePayload(known, kVideoPort, ProbeKind::RtspOptions, kPayloadTimeoutMs,
                     [this, known](bool ok, const QString &) {
            if (!ok) {
                trace(QStringLiteral("已配置地址图传不通，转入全网段扫描"));
                m_scanning = false;
                beginScan();
                return;
            }
            // 图传通了，再验照片代理，避免误认别的 RTSP 服务
            probePayload(known, kPhotoPort, ProbeKind::PhotoJson, kPayloadTimeoutMs,
                         [this, known](bool ok2, const QString &) {
                m_scanning = false;
                if (ok2) {
                    m_foundHost = known;
                    setStatus(QStringLiteral("已找到机载电脑：%1（原地址有效）").arg(known));
                    trace(QStringLiteral("快路径命中: %1").arg(known));
                    emit changed();
                    emit found(known);
                    emit finished(true, m_status);
                } else {
                    trace(QStringLiteral("图传通但照片代理不通，转入全网段扫描"));
                    beginScan();
                }
            });
        });
        return;
    }

    beginScan();
}

// 全网段扫描（慢路径）
void RelayDiscovery::beginScan()
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
    if (!m_lastKnownHost.trimmed().isEmpty())
        addBase(m_lastKnownHost.trimmed());

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

// 探测一个端口：连上即算命中（仅用于第一轮粗筛）
static void probeOnceImpl(const QString &host, quint16 port, int timeoutMs,
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

namespace {
// 包装：把实现暴露成前面声明过的 probeOnce
void probeOnce(const QString &host, quint16 port, int timeoutMs,
               std::function<void(bool)> done)
{
    probeOnceImpl(host, port, timeoutMs, std::move(done));
}
} // namespace

// 协议级探测：建立连接 → 发请求 → 收数据 → 判定是不是目标服务
static void probePayloadImpl(const QString &host, quint16 port, ProbeKind kind, int timeoutMs,
                             std::function<void(bool, const QString &)> done)
{
    auto *socket = new QTcpSocket;
    auto *timer = new QTimer(socket);
    timer->setSingleShot(true);
    struct State {
        QByteArray rx;
        bool settled{false};
    };
    auto state = std::make_shared<State>();

    const auto finish = [state, socket, timer, done](bool ok, const QString &detail) {
        if (state->settled)
            return;
        state->settled = true;
        timer->stop();
        done(ok, detail);
        socket->abort();
        socket->deleteLater();
    };

    QObject::connect(socket, &QTcpSocket::connected, socket, [socket, kind, host, port]() {
        const QByteArray request = probeRequest(kind, host, port);
        if (!request.isEmpty())
            socket->write(request);
    });
    QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, state, kind, finish]() {
        state->rx += socket->readAll();
        if (state->rx.size() > 16384) {
            finish(false, QStringLiteral("响应异常"));
            return;
        }
        const Verdict verdict = judge(kind, state->rx, false);
        if (verdict.settled)
            finish(verdict.ok, verdict.detail);
    });
    QObject::connect(socket, &QTcpSocket::disconnected, socket, [state, kind, finish]() {
        if (state->settled)
            return;
        const Verdict verdict = judge(kind, state->rx, true);
        finish(verdict.ok, verdict.detail);
    });
    QObject::connect(socket, &QTcpSocket::errorOccurred, socket,
                     [state, finish](QAbstractSocket::SocketError) {
        if (!state->settled)
            finish(false, QStringLiteral("连接失败"));
    });
    QObject::connect(timer, &QTimer::timeout, socket, [state, kind, finish]() {
        const Verdict verdict = judge(kind, state->rx, true);
        finish(verdict.ok, verdict.detail);
    });

    timer->start(timeoutMs);
    socket->connectToHost(host, port);
}

namespace {
void probePayload(const QString &host, quint16 port, ProbeKind kind, int timeoutMs,
                  std::function<void(bool, const QString &)> done)
{
    probePayloadImpl(host, port, kind, timeoutMs, std::move(done));
}
} // namespace

void RelayDiscovery::beginBatch()
{
    if (m_pending.isEmpty()) {
        // 第一轮（8554）结束 → 对命中项做协议确认（照片代理）
        if (m_hits.isEmpty()) {
            m_scanning = false;
            setStatus(QStringLiteral("扫描完成：没有找到机载电脑（%1 个地址都没响应）").arg(m_total));
            trace(QStringLiteral("扫描结束，无命中"));
            emit changed();
            emit finished(false, m_status);
            return;
        }
        setStatus(QStringLiteral("发现 %1 个候选，正在确认协议…").arg(m_hits.size()));
        const QStringList candidates = m_hits;
        const int candidateCount = candidates.size();
        m_hits.clear();
        // ⚠️ 计数必须放在成员上：探测是异步的，用栈变量做引用捕获会悬垂
        m_verifyRemaining = candidateCount;
        for (const QString &host : candidates) {
            probePayload(host, kPhotoPort, ProbeKind::PhotoJson, kPayloadTimeoutMs,
                         [this, host, candidateCount](bool ok, const QString &) {
                if (ok && m_foundHost.isEmpty()) {
                    m_foundHost = host;
                    trace(QStringLiteral("确认机载电脑: %1（图传 + %2 协议都对）").arg(host).arg(kPhotoPort));
                    m_scanning = false;
                    setStatus(QStringLiteral("已找到机载电脑：%1").arg(host));
                    emit changed();
                    emit found(host);
                    emit finished(true, m_status);
                }
                if (--m_verifyRemaining <= 0 && m_foundHost.isEmpty()) {
                    m_scanning = false;
                    setStatus(QStringLiteral("找到 %1 个图传端口，但没有一个通过照片代理验证"
                                             "（可能是别的 RTSP 服务）")
                                  .arg(candidateCount));
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

void RelayDiscovery::checkAll(const QString &host, const QString &user)
{
    const QString target = host.trimmed();
    if (target.isEmpty() || m_checking)
        return;

    m_services.clear();
    const auto addRow = [this](const QString &key, const QString &label, const QString &endpoint) {
        m_services.append(QVariantMap{{QStringLiteral("key"), key},
                                      {QStringLiteral("label"), label},
                                      {QStringLiteral("endpoint"), endpoint},
                                      {QStringLiteral("state"), QStringLiteral("pending")},
                                      {QStringLiteral("detail"), QStringLiteral("等待测试")}});
    };
    addRow(QStringLiteral("ssh"), QStringLiteral("SSH 登录"),
           QStringLiteral("%1:%2").arg(target).arg(kSshPort));
    addRow(QStringLiteral("video"), QStringLiteral("图传 RTSP"),
           QStringLiteral("%1:%2").arg(target).arg(kVideoPort));
    addRow(QStringLiteral("gimbal"), QStringLiteral("云台指令"),
           QStringLiteral("%1:%2").arg(target).arg(kGimbalPort));
    addRow(QStringLiteral("photo"), QStringLiteral("照片/录像"),
           QStringLiteral("%1:%2").arg(target).arg(kPhotoPort));
    addRow(QStringLiteral("pointcloud"), QStringLiteral("点云流"),
           QStringLiteral("127.0.0.1:%1").arg(kPointCloudPort));
    addRow(QStringLiteral("mapping"), QStringLiteral("建图启动"),
           QStringLiteral("127.0.0.1:%1").arg(kMappingPort));
    addRow(QStringLiteral("telemetry"), QStringLiteral("遥测网关"),
           QStringLiteral("127.0.0.1:%1").arg(kTelemetryPort));

    m_checking = true;
    m_checksPending = 7;
    m_checkSummary = QStringLiteral("正在测试 %1（%2）…").arg(target, user);
    emit changed();
    trace(QStringLiteral("自检 %1").arg(target));

    const auto run = [this](int index, const QString &probeHost, quint16 port, ProbeKind kind) {
        probePayload(probeHost, port, kind, kPayloadTimeoutMs,
                     [this, index](bool ok, const QString &detail) {
            QVariantMap row = m_services.at(index).toMap();
            row.insert(QStringLiteral("state"), ok ? QStringLiteral("ok") : QStringLiteral("fail"));
            row.insert(QStringLiteral("detail"), detail);
            m_services[index] = row;
            emit changed();
            if (--m_checksPending <= 0)
                finishChecks();
        });
    };

    run(0, target, kSshPort, ProbeKind::SshBanner);
    run(1, target, kVideoPort, ProbeKind::RtspOptions);
    // 云台是 UDP：没有请求就没有回应，只能标成"不验"，不能假装测过
    {
        QVariantMap row = m_services.at(2).toMap();
        row.insert(QStringLiteral("state"), QStringLiteral("skip"));
        row.insert(QStringLiteral("detail"), QStringLiteral("UDP 端口，需实际下发指令才能确认"));
        m_services[2] = row;
        if (--m_checksPending <= 0)
            finishChecks();
    }
    run(3, target, kPhotoPort, ProbeKind::PhotoJson);
    run(4, QStringLiteral("127.0.0.1"), kPointCloudPort, ProbeKind::PointCloudHello);
    run(5, QStringLiteral("127.0.0.1"), kMappingPort, ProbeKind::MappingStatus);
    run(6, QStringLiteral("127.0.0.1"), kTelemetryPort, ProbeKind::TelemetryJson);
}

void RelayDiscovery::finishChecks()
{
    int ok = 0;
    int fail = 0;
    for (const QVariant &entry : m_services) {
        const QString state = entry.toMap().value(QStringLiteral("state")).toString();
        if (state == QStringLiteral("ok"))
            ++ok;
        else if (state == QStringLiteral("fail"))
            ++fail;
    }
    m_checking = false;
    m_checkSummary = QStringLiteral("%1 项就绪，%2 项未通").arg(ok).arg(fail);
    emit changed();
}
