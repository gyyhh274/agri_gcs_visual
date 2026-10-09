#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;

/* 云台相机照片库：经机载电脑的 HTTP 代理读取相机 TF 卡里的照片。
 *
 * 链路：地面站 → http://<机载电脑>:37261（siyi-gimbal-relay 的 HTTP 代理）
 *              → http://192.168.144.25:82/cgi-bin/media.cgi/api/v1/...
 *
 * 相机文件接口（SDK 第七章，无需认证）：
 *   getdirectories?media_type=0            目录列表
 *   getmedialist?media_type=0&path=...     文件列表（含下载 url）
 *   getmediacount?media_type=0&path=...    文件个数
 *
 * 下载下来的照片缓存在本机 cache 目录，QML 直接用本地路径显示，
 * 避免每次滚动都重新走网络。
 */
class PhotoLibrary final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY photosChanged)
    Q_PROPERTY(QVariantList photos READ photos NOTIFY photosChanged)
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY changed)

public:
    explicit PhotoLibrary(QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    int count() const { return m_photos.size(); }
    QVariantList photos() const;
    QString endpoint() const { return m_endpoint; }

    // 机载电脑上的代理地址（默认与云台中转同机）
    Q_INVOKABLE void setEndpoint(const QString &host, int port);
    // 拉取文件列表，并下载本地还没有的照片
    Q_INVOKABLE void refresh();
    // 只同步最新一张（拍完照立即调用，避免整卡重下）
    Q_INVOKABLE void fetchNewest();
    // 清空列表（不删除相机上的文件）
    Q_INVOKABLE void clear();
    Q_INVOKABLE QString cacheDirectory() const;

signals:
    void changed();
    void photosChanged();
    void photoReady(const QString &name, const QString &localPath);
    void notified(const QString &text);

private:
    struct Photo {
        QString name;
        QString remotePath;   // 相机上的路径，如 /photo/101SIYI_IMG/IMG_0001.jpg
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
    QString m_status{QStringLiteral("照片库未连接")};
};
