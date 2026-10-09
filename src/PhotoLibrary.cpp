#include "PhotoLibrary.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
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

void PhotoLibrary::requestList(bool newestOnly)
{
    if (m_endpoint.isEmpty()) {
        setStatus(QStringLiteral("未设置照片库地址"));
        emit notified(QStringLiteral("照片库未连接：缺少代理地址"));
        return;
    }
    m_newestOnly = newestOnly;
    const QString url = QStringLiteral("%1/cgi-bin/media.cgi/api/v1/getmedialist"
                                       "?media_type=0&path=%2&start=0&count=%3")
                            .arg(m_endpoint, QLatin1String(kPhotoDir))
                            .arg(kListCount);
    setBusy(true);
    setStatus(QStringLiteral("正在读取相机文件列表…"));
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
    } else {
        m_queue.clear();
        for (const Photo &photo : m_photos) {
            if (photo.localPath.isEmpty())
                m_queue.append(photo.name);
        }
    }

    if (m_queue.isEmpty()) {
        setStatus(QStringLiteral("共 %1 张照片（已全部缓存）").arg(m_photos.size()));
        return;
    }
    setStatus(QStringLiteral("共 %1 张，正在下载 %2 张…")
                  .arg(m_photos.size()).arg(m_queue.size()));
    startNextDownload();
}

void PhotoLibrary::startNextDownload()
{
    if (m_queue.isEmpty()) {
        setBusy(false);
        setStatus(QStringLiteral("共 %1 张照片").arg(m_photos.size()));
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
    const QString url = m_endpoint + m_photos.at(index).remotePath;
    trace(QStringLiteral("下载: %1").arg(url));
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(20000);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { handleDownloadReply(reply); });
}

void PhotoLibrary::handleDownloadReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        trace(QStringLiteral("下载失败: %1").arg(reply->errorString()));
        setStatus(QStringLiteral("下载失败：%1").arg(reply->errorString()));
        setBusy(false);
        return;
    }

    const QString url = reply->url().path();
    const QString name = url.section(QLatin1Char('/'), -1);
    const QByteArray data = reply->readAll();
    const QString local = localPathFor(name);

    QFile file(local);
    if (!file.open(QIODevice::WriteOnly)) {
        setStatus(QStringLiteral("无法写入缓存：%1").arg(local));
        setBusy(false);
        return;
    }
    file.write(data);
    file.close();

    for (Photo &photo : m_photos) {
        if (photo.name == name) {
            photo.localPath = local;
            photo.sizeBytes = data.size();
            break;
        }
    }
    trace(QStringLiteral("已保存 %1（%2 字节）→ %3").arg(name).arg(data.size()).arg(local));
    emit photosChanged();
    emit photoReady(name, local);
    emit notified(QStringLiteral("已取回 %1（%2 KB）").arg(name).arg(data.size() / 1024));

    startNextDownload();
}
