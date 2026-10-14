#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

/* 云台相机媒体库：经机载电脑的 HTTP 代理读取相机 TF 卡里的照片/录像。
 *
 * 链路：地面站 → http://<机载电脑>:37261（siyi-gimbal-relay 的 HTTP 代理）
 *              → http://192.168.144.25:82/cgi-bin/media.cgi/api/v1/...
 *
 * 相机文件接口（SDK 第七章，无需认证），照片与录像接口完全对称，
 * 只差 media_type 与目录名（实测确认）：
 *   media_type=0  目录 101SIYI_IMG   URL 前缀 /photo/
 *   media_type=1  目录 100SIYI_VID   URL 前缀 /mp4/
 *   getdirectories / getmedialist / getmediacount
 *
 * 本类被实例化两次（见 main.cpp）：
 *   photoLibrary  media_type=0  自动下载（JPEG 小，取回便于预览）
 *   videoLibrary  media_type=1  手动下载（录像动辄上百 MB，不能自动拉）
 *
 * 下载下来的文件缓存在本机 cache 目录，QML 直接用本地路径显示/播放，
 * 避免每次滚动都重新走网络。
 */
class PhotoLibrary final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY photosChanged)
    Q_PROPERTY(QVariantList photos READ photos NOTIFY photosChanged)
    Q_PROPERTY(QVariantList items READ photos NOTIFY photosChanged)
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY changed)
    // ── 媒体类型配置（照片/录像共用本类）────────────────────────
    Q_PROPERTY(int mediaType READ mediaType WRITE setMediaType NOTIFY changed)
    Q_PROPERTY(QString remotePath READ remotePath WRITE setRemotePath NOTIFY changed)
    Q_PROPERTY(bool autoDownload READ autoDownload WRITE setAutoDownload NOTIFY changed)
    Q_PROPERTY(QString mediaLabel READ mediaLabel NOTIFY changed)
    // ── 下载进度（录像手动下载时需要）──────────────────────────
    Q_PROPERTY(QString downloadingName READ downloadingName NOTIFY progressChanged)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY progressChanged)
    Q_PROPERTY(QString downloadingSizeText READ downloadingSizeText NOTIFY progressChanged)

public:
    explicit PhotoLibrary(QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    int count() const { return m_photos.size(); }
    QVariantList photos() const;
    QString endpoint() const { return m_endpoint; }

    int mediaType() const { return m_mediaType; }
    void setMediaType(int type);
    QString remotePath() const { return m_remotePath; }
    void setRemotePath(const QString &path);
    bool autoDownload() const { return m_autoDownload; }
    void setAutoDownload(bool enabled);
    QString mediaLabel() const { return m_mediaLabel; }

    QString downloadingName() const { return m_downloadingName; }
    double downloadProgress() const { return m_downloadProgress; }
    QString downloadingSizeText() const { return m_downloadingSizeText; }

    // 机载电脑上的代理地址（默认与云台中转同机）
    Q_INVOKABLE void setEndpoint(const QString &host, int port);
    // 拉取文件列表；autoDownload=true 时顺带下载本地还没有的
    Q_INVOKABLE void refresh();
    // 只同步最新一张（拍完照立即调用，避免整卡重下）
    Q_INVOKABLE void fetchNewest();
    // 按名字下载单个文件（录像用：文件大，必须手动触发）
    Q_INVOKABLE void download(const QString &name);
    // 下载当前列表里所有未下载的（录像慎用，会拉很多流量）
    Q_INVOKABLE void downloadAllMissing();
    // 删除本地缓存（不动相机上的文件）
    Q_INVOKABLE bool removeLocal(const QString &name);
    // 清空列表（不删除相机上的文件）
    Q_INVOKABLE void clear();
    Q_INVOKABLE QString cacheDirectory() const;

signals:
    void changed();
    void photosChanged();
    void progressChanged();
    void photoReady(const QString &name, const QString &localPath);
    void notified(const QString &text);

private:
    struct Photo {
        QString name;
        QString remotePath;   // 相机上的路径，如 /mp4/100SIYI_VID/REC_0001.mp4
        QString localPath;    // 本机缓存路径；为空表示还没下载
        qint64 sizeBytes{0};
    };

    void requestList(bool newestOnly);
    void handleListReply(QNetworkReply *reply, bool newestOnly);
    void enqueueMissing();
    void startNextDownload();
    void handleDownloadReply(QNetworkReply *reply);
    void setBusy(bool busy);
    void setStatus(const QString &text);
    void setDownloadProgress(const QString &name, double progress, const QString &sizeText);
    QString localPathFor(const QString &name) const;
    static QString pathFromCameraUrl(const QString &url);

    QNetworkAccessManager *m_network{nullptr};
    QList<Photo> m_photos;
    QStringList m_queue;          // 待下载的 name 队列
    QString m_host;
    int m_port{37261};
    QString m_endpoint;
    QString m_cacheDir;
    bool m_busy{false};
    bool m_newestOnly{false};

    // 媒体类型配置
    int m_mediaType{0};
    QString m_remotePath{QStringLiteral("101SIYI_IMG")};
    QString m_mediaLabel{QStringLiteral("照片")};
    bool m_autoDownload{true};

    // 下载进度
    QString m_downloadingName;
    double m_downloadProgress{0.0};
    QString m_downloadingSizeText;
    qint64 m_downloadTotal{0};
    qint64 m_downloadDone{0};
    // 流式写盘用（录像文件大，不能整包读进内存）
    QString m_downloadName;
    QFile *m_downloadFile{nullptr};

    QString m_status{QStringLiteral("照片库未连接")};
};
