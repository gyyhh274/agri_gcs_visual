#include "PhotoLibrary.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

namespace {
constexpr char kPhotoDir[] = "101SIYI_IMG";
constexpr int kMaxPhotos = 200;
constexpr int kListCount = 200;

void trace(const QString &message)
{
    qInfo("[PhotoLibrary] %s", qPrintable(message));
}
}

PhotoLibrary::PhotoLibrary(QObject *parent) : QObject(parent)
{
    m_network = new QNetworkAccessManager(this);
    m_network->setProxy(QNetworkProxy::NoProxy);
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (m_cacheDir.isEmpty())
        m_cacheDir = QDir::homePath() + QStringLiteral("/.cache/agri_gcs_photo");
    m_cacheDir += QStringLiteral("/photos");
    QDir().mkpath(m_cacheDir);
}

QString PhotoLibrary::cacheDirectory() const
{
    return m_cacheDir;
}

QString PhotoLibrary::localPathFor(const QString &name) const
{
    return m_cacheDir + QLatin1Char('/') + name;
}

QString PhotoLibrary::pathFromCameraUrl(const QString &url)
{
    // 相机返回的是绝对地址 http://192.168.144.25:82/photo/...，
    // 经代理时只需保留路径部分。
    const QUrl parsed(url);
    if (!parsed.path().isEmpty())
        return parsed.path();
    const int index = url.indexOf(QStringLiteral(":82"));
    return index >= 0 ? url.mid(index + 3) : url;
}

QVariantList PhotoLibrary::photos() const
{
    QVariantList list;
    list.reserve(m_photos.size());
    for (const Photo &photo : m_photos) {
        QVariantMap item;
        item.insert(QStringLiteral("name"), photo.name);
        item.insert(QStringLiteral("ready"), !photo.localPath.isEmpty());
        item.insert(QStringLiteral("localPath"),
                    photo.localPath.isEmpty() ? QString() : QStringLiteral("file://") + photo.localPath);
        item.insert(QStringLiteral("sizeKB"), photo.sizeBytes / 1024);
        list.append(item);
    }
    return list;
}

void PhotoLibrary::setEndpoint(const QString &host, int port)
{
    m_host = host.trimmed();
    m_port = port > 0 ? port : 37261;
    m_endpoint = QStringLiteral("http://%1:%2").arg(m_host).arg(m_port);
    emit changed();
}

void PhotoLibrary::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit changed();
}

void PhotoLibrary::setStatus(const QString &text)
{
    if (m_status == text)
        return;
    m_status = text;
    emit changed();
}

void PhotoLibrary::clear()
{
    m_photos.clear();
    m_queue.clear();
    emit photosChanged();
    setStatus(QStringLiteral("已清空列表（相机文件未删除）"));
}

void PhotoLibrary::refresh()
{
    requestList(false);
}

void PhotoLibrary::fetchNewest()
{
    requestList(true);
}

// ── 媒体类型配置 ───────────────────────────────────────────────
void PhotoLibrary::setMediaType(int type)
{
    const int normalized = (type == 1) ? 1 : 0;
    if (m_mediaType == normalized)
        return;
    m_mediaType = normalized;
    m_remotePath = (normalized == 1) ? QStringLiteral("100SIYI_VID")
                                     : QStringLiteral("101SIYI_IMG");
    m_mediaLabel = (normalized == 1) ? QStringLiteral("录像") : QStringLiteral("照片");
    // 缓存分目录，避免照片和录像混在一起
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (m_cacheDir.isEmpty())
        m_cacheDir = QDir::homePath() + QStringLiteral("/.cache/agri_gcs_media");
    m_cacheDir += (normalized == 1) ? QStringLiteral("/videos") : QStringLiteral("/photos");
    QDir().mkpath(m_cacheDir);
    // 列表与类型相关，切类型要清掉
    m_photos.clear();
    m_queue.clear();
    // 状态文字要跟着媒体类型走（构造函数的默认文案是"照片库"）
    m_status = QStringLiteral("%1库未连接").arg(m_mediaLabel);
    emit photosChanged();
    emit changed();
}

void PhotoLibrary::setRemotePath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty() || m_remotePath == trimmed)
        return;
    m_remotePath = trimmed;
    emit changed();
}

void PhotoLibrary::setAutoDownload(bool enabled)
{
    if (m_autoDownload == enabled)
        return;
    m_autoDownload = enabled;
    emit changed();
}

void PhotoLibrary::setDownloadProgress(const QString &name, double progress, const QString &sizeText)
{
    m_downloadingName = name;
    m_downloadProgress = progress;
    m_downloadingSizeText = sizeText;
    emit progressChanged();
}

// 手动下载单个文件（录像文件大，不做自动下载）
void PhotoLibrary::download(const QString &name)
{
    for (const Photo &media : m_photos) {
        if (media.name != name)
            continue;
        if (!media.localPath.isEmpty()) {
            emit notified(QStringLiteral("%1 已在本地").arg(name));
            return;
        }
        if (!m_queue.contains(name))
            m_queue.append(name);
        if (!m_busy)
            startNextDownload();
        return;
    }
    emit notified(QStringLiteral("列表里没有 %1，先刷新一次").arg(name));
}

void PhotoLibrary::downloadAllMissing()
{
    int missing = 0;
    for (const Photo &media : m_photos) {
        if (media.localPath.isEmpty()) {
            if (!m_queue.contains(media.name))
                m_queue.append(media.name);
            ++missing;
        }
    }
    if (missing == 0) {
        emit notified(QStringLiteral("全部已下载"));
        return;
    }
    emit notified(QStringLiteral("开始下载 %1 个未缓存文件").arg(missing));
    if (!m_busy)
        startNextDownload();
}

bool PhotoLibrary::removeLocal(const QString &name)
{
    for (int i = 0; i < m_photos.size(); ++i) {
        Photo &media = m_photos[i];
        if (media.name != name)
            continue;
        if (media.localPath.isEmpty())
            return false;
        QFile::remove(media.localPath);
        media.localPath.clear();
        media.sizeBytes = 0;
        emit photosChanged();
        setStatus(QStringLiteral("已删除本地缓存 %1（相机文件保留）").arg(name));
        return true;
    }
    return false;
}

void PhotoLibrary::requestList(bool newestOnly)
{
    if (m_endpoint.isEmpty()) {
        setStatus(QStringLiteral("未设置%1库地址").arg(m_mediaLabel));
        emit notified(QStringLiteral("%1库未连接：缺少代理地址").arg(m_mediaLabel));
        return;
    }
    m_newestOnly = newestOnly;
    const QString url = QStringLiteral("%1/cgi-bin/media.cgi/api/v1/getmedialist"
                                       "?media_type=%2&path=%3&start=0&count=%4")
                            .arg(m_endpoint)
                            .arg(m_mediaType)
                            .arg(m_remotePath)
                            .arg(kListCount);
    setBusy(true);
    setStatus(QStringLiteral("正在读取相机%1列表…").arg(m_mediaLabel));
    trace(QStringLiteral("请求列表: %1").arg(url));

    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(8000);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, newestOnly]() { handleListReply(reply, newestOnly); });
}

void PhotoLibrary::handleListReply(QNetworkReply *reply, bool newestOnly)
{
    reply->deleteLater();
    setBusy(false);
    if (reply->error() != QNetworkReply::NoError) {
        trace(QStringLiteral("列表请求失败: %1").arg(reply->errorString()));
        setStatus(QStringLiteral("读取文件列表失败：%1").arg(reply->errorString()));
        emit notified(QStringLiteral("读取相机文件列表失败（检查机载代理是否运行）"));
        return;
    }

    const QByteArray body = reply->readAll();
    trace(QStringLiteral("列表响应 %1 字节: %2")
              .arg(body.size())
              .arg(QString::fromUtf8(body.left(160))));
    const QJsonDocument document = QJsonDocument::fromJson(body);
    const QJsonObject root = document.object();
    const QJsonArray list = root.value(QStringLiteral("data")).toObject()
                                .value(QStringLiteral("list")).toArray();
    trace(QStringLiteral("解析到 %1 个文件条目").arg(list.size()));

    if (!root.value(QStringLiteral("success")).toBool() && list.isEmpty()) {
        setStatus(QStringLiteral("相机返回错误：%1")
                      .arg(root.value(QStringLiteral("message")).toString()));
        return;
    }

    // 相机按文件名升序返回，最新的在最后 → 反过来，让最新的排在最前
    QStringList names;
    names.reserve(list.size());
    QList<Photo> fetched;
    for (int i = list.size() - 1; i >= 0; --i) {
        const QJsonObject entry = list.at(i).toObject();
        Photo photo;
        photo.name = entry.value(QStringLiteral("name")).toString();
        photo.remotePath = pathFromCameraUrl(entry.value(QStringLiteral("url")).toString());
        if (photo.name.isEmpty())
            continue;
        const QString local = localPathFor(photo.name);
        if (QFileInfo::exists(local)) {
            photo.localPath = local;
            photo.sizeBytes = QFileInfo(local).size();
        }
        fetched.append(photo);
        names.append(photo.name);
    }

    if (fetched.isEmpty()) {
        m_photos.clear();
        emit photosChanged();
        setStatus(QStringLiteral("相机 TF 卡里还没有照片"));
        emit notified(QStringLiteral("相机里还没有照片（先拍一张试试）"));
        return;
    }

    // 只保留最新的 kMaxPhotos 张
    while (fetched.size() > kMaxPhotos)
        fetched.removeLast();
    while (names.size() > kMaxPhotos)
        names.removeLast();

    // 列表变化时整体替换；只有新文件时才重建（避免 QML 闪动）
    bool changedList = (m_photos.size() != fetched.size());
    if (!changedList) {
        for (int i = 0; i < fetched.size(); ++i) {
            if (m_photos.at(i).name != fetched.at(i).name) {
                changedList = true;
                break;
            }
        }
    }
    if (changedList) {
        m_photos = fetched;
        emit photosChanged();
    } else {
        // 名称一致，但可能刚下载完成 → 更新本地路径
        m_photos = fetched;
        emit photosChanged();
    }

    if (newestOnly) {
        // 只下载最新一张
        m_queue.clear();
        if (!m_photos.isEmpty() && m_photos.first().localPath.isEmpty())
            m_queue.append(m_photos.first().name);
    } else if (!m_autoDownload) {
        // 录像等大文件：只列列表，下载必须手动点（避免一刷新就拉几百 MB）
        m_queue.clear();
        int cached = 0;
        for (const Photo &media : m_photos) {
            if (!media.localPath.isEmpty())
                ++cached;
        }
        setStatus(QStringLiteral("共 %1 个%2，已缓存 %3 个（点条目可下载）")
                      .arg(m_photos.size()).arg(m_mediaLabel).arg(cached));
        emit photosChanged();
        return;
    } else {
        m_queue.clear();
        for (const Photo &photo : m_photos) {
            if (photo.localPath.isEmpty())
                m_queue.append(photo.name);
        }
    }

    if (m_queue.isEmpty()) {
        setStatus(QStringLiteral("共 %1 个%2（已全部缓存）")
                      .arg(m_photos.size()).arg(m_mediaLabel));
        return;
    }
    setStatus(QStringLiteral("共 %1 个%2，正在下载 %3 个…")
                  .arg(m_photos.size()).arg(m_mediaLabel).arg(m_queue.size()));
    startNextDownload();
}

void PhotoLibrary::startNextDownload()
{
    if (m_queue.isEmpty()) {
        setBusy(false);
        setDownloadProgress(QString(), 0.0, QString());
        setStatus(QStringLiteral("共 %1 个%2").arg(m_photos.size()).arg(m_mediaLabel));
        return;
    }
    const QString name = m_queue.takeFirst();
    int index = -1;
    for (int i = 0; i < m_photos.size(); ++i) {
        if (m_photos.at(i).name == name) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        startNextDownload();
        return;
    }

    setBusy(true);
    setStatus(QStringLiteral("正在下载 %1…").arg(name));
    m_downloadTotal = 0;
    m_downloadDone = 0;
    setDownloadProgress(name, 0.0, QStringLiteral("开始下载"));

    // 流式写盘：录像动辄上百 MB，不能整包读进内存
    m_downloadName = name;
    m_downloadFile = new QFile(localPathFor(name), this);
    if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setStatus(QStringLiteral("无法写入缓存：%1").arg(m_downloadFile->fileName()));
        m_downloadFile->deleteLater();
        m_downloadFile = nullptr;
        setBusy(false);
        return;
    }

    const QString url = m_endpoint + m_photos.at(index).remotePath;
    trace(QStringLiteral("下载: %1").arg(url));
    QNetworkRequest request{QUrl(url)};
    // 录像文件大，给足超时（照片也用同一值，没有副作用）
    request.setTransferTimeout(120000);
    QNetworkReply *reply = m_network->get(request);
    // 进度反馈：录像动辄上百 MB，界面必须有进度
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, name](qint64 received, qint64 total) {
                m_downloadTotal = total;
                m_downloadDone = received;
                const double pct = (total > 0) ? (100.0 * received / total) : 0.0;
                const QString text = (total > 0)
                    ? QStringLiteral("%1 / %2 MB")
                          .arg(received / 1048576.0, 0, 'f', 1)
                          .arg(total / 1048576.0, 0, 'f', 1)
                    : QStringLiteral("%1 MB").arg(received / 1048576.0, 0, 'f', 1);
                setDownloadProgress(name, pct, text);
            });
    // 边收边写
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        if (m_downloadFile && m_downloadFile->isOpen())
            m_downloadFile->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { handleDownloadReply(reply); });
}

void PhotoLibrary::handleDownloadReply(QNetworkReply *reply)
{
    reply->deleteLater();

    // 收尾：把流式写入的文件关掉（无论成败）
    const QString name = m_downloadName;
    qint64 written = 0;
    if (m_downloadFile) {
        if (m_downloadFile->isOpen()) {
            m_downloadFile->write(reply->readAll());   // 收尾残余
            written = m_downloadFile->size();
            m_downloadFile->close();
        }
        m_downloadFile->deleteLater();
        m_downloadFile = nullptr;
    }
    m_downloadName.clear();

    if (reply->error() != QNetworkReply::NoError) {
        trace(QStringLiteral("下载失败: %1").arg(reply->errorString()));
        setStatus(QStringLiteral("下载失败：%1").arg(reply->errorString()));
        setDownloadProgress(QString(), 0.0, QString());
        // 删掉半截文件，避免下次误判"已缓存"
        if (!name.isEmpty())
            QFile::remove(localPathFor(name));
        setBusy(false);
        startNextDownload();
        return;
    }

    if (name.isEmpty() || written <= 0) {
        setStatus(QStringLiteral("下载内容为空"));
        setDownloadProgress(QString(), 0.0, QString());
        setBusy(false);
        return;
    }

    const QString local = localPathFor(name);
    for (Photo &photo : m_photos) {
        if (photo.name == name) {
            photo.localPath = local;
            photo.sizeBytes = written;
            break;
        }
    }
    trace(QStringLiteral("已保存 %1（%2 字节）→ %3").arg(name).arg(written).arg(local));
    emit photosChanged();
    emit photoReady(name, local);
    if (written >= 1048576)
        emit notified(QStringLiteral("已取回 %1（%2 MB）").arg(name).arg(written / 1048576.0, 0, 'f', 1));
    else
        emit notified(QStringLiteral("已取回 %1（%2 KB）").arg(name).arg(written / 1024));
    setDownloadProgress(QString(), 0.0, QString());

    startNextDownload();
}
