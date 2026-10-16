#pragma once
#include <QQuickItem>
#include <memory>
class A8VideoPipeline;

// 与 MiniGUI_A8mini 共用硬件解码/GL 显示实现；不将视频帧拷回 CPU。
class NativeVideoItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(QString detail READ detail NOTIFY changed)
    Q_PROPERTY(bool live READ live NOTIFY changed)
    Q_PROPERTY(int failures READ failures NOTIFY changed)
    Q_PROPERTY(qulonglong frames READ frames NOTIFY changed)
    Q_PROPERTY(QString decoder READ decoder NOTIFY changed)
public:
    explicit NativeVideoItem(QQuickItem *parent = nullptr);
    ~NativeVideoItem() override;
    bool available() const;
    QString phase() const;
    QString detail() const;
    bool live() const;
    int failures() const;
    qulonglong frames() const;
    QString decoder() const;
    Q_INVOKABLE void attach(QQuickItem *item);
    Q_INVOKABLE void play(const QString &url, const QString &user, const QString &password,
                          int latency, const QString &transport);
    Q_INVOKABLE void stop();
signals:
    void changed();
private:
#ifdef AGRI_GCS_NATIVE_GSTREAMER
    std::unique_ptr<A8VideoPipeline> m_video;
#endif
};
