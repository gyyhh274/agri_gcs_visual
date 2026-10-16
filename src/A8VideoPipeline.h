#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <QElapsedTimer>
#include <gst/gst.h>
#include <atomic>
#include <memory>
#include <mutex>

// Create before loading QML: qmlglsink registers GstGLVideoItem on creation.
class A8VideoPipeline final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(int reconnectCount READ reconnectCount NOTIFY changed)
    Q_PROPERTY(qulonglong renderedFrames READ renderedFrames NOTIFY changed)
    Q_PROPERTY(qulonglong droppedFrames READ droppedFrames NOTIFY changed)
    Q_PROPERTY(QString decoderName READ decoderName NOTIFY changed)
    Q_PROPERTY(int width READ width NOTIFY changed)
    Q_PROPERTY(int height READ height NOTIFY changed)
    Q_PROPERTY(double fps READ fps NOTIFY changed)
public:
    explicit A8VideoPipeline(bool enabled = true, QObject *parent = nullptr);
    ~A8VideoPipeline() override;
    bool available() const { return m_sink != nullptr; }
    QString state() const { return m_state; }
    QString lastError() const { return m_error; }
    int reconnectCount() const { return m_reconnects; }
    qulonglong renderedFrames() const { return m_rendered; }
    qulonglong droppedFrames() const { return m_dropped; }
    QString decoderName() const { return m_decoder; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    double fps() const { return m_fps; }
    void attach(QQuickItem *item);
    Q_INVOKABLE void play(const QString &url, const QString &username = {},
                          const QString &password = {}, int latencyMs = 300);
    Q_INVOKABLE void playTest();
    Q_INVOKABLE void playFile(const QString &path);
    Q_INVOKABLE void stop();
signals:
    void changed();
private:
    enum Source { Rtsp, Test, File };
    struct StartGuard { std::mutex mutex; bool canceled = false; };
    void begin();
    void dispose();
    void poll();
    void retry(const QString &reason);
    void setState(const QString &state);
    QString safeError(QString text) const;
    GstElement *m_sink = nullptr;
    GstElement *m_pipeline = nullptr;
    GstBus *m_bus = nullptr;
    QPointer<QQuickItem> m_item;
    std::shared_ptr<StartGuard> m_startGuard;
    QTimer m_poll, m_retry;
    QElapsedTimer m_progress, m_fpsSample;
    Source m_source = Test;
    QString m_url, m_username, m_password;
    QString m_state = QStringLiteral("STOPPED"), m_error, m_decoder;
    int m_latency = 300, m_reconnects = 0, m_failures = 0;
    int m_width = 0, m_height = 0;
    quint64 m_rendered = 0, m_dropped = 0, m_attemptFrames = 0;
    quint64 m_renderedBase = 0, m_droppedBase = 0;
    double m_fps = 0;
    bool m_wanted = false, m_hadFrame = false;
};
