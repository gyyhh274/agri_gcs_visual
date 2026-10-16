#include "A8VideoPipeline.h"
#include <QQuickWindow>
#include <QRunnable>
#include <QUrl>
#include <QFileInfo>
#include <functional>

namespace {
class RenderJob final : public QRunnable {
public:
    explicit RenderJob(std::function<void()> fn) : m_fn(std::move(fn)) {}
    void run() override { m_fn(); }
private:
    std::function<void()> m_fn;
};

void rtspPadAdded(GstElement *, GstPad *pad, gpointer target) {
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) caps = gst_pad_query_caps(pad, nullptr);
    if (!caps || gst_caps_is_empty(caps)) { if (caps) gst_caps_unref(caps); return; }
    const auto *s = gst_caps_get_structure(caps, 0);
    const gchar *media = gst_structure_get_string(s, "media");
    const gchar *encoding = gst_structure_get_string(s, "encoding-name");
    if (g_strcmp0(media, "video") == 0) {
        if (g_strcmp0(encoding, "H265") != 0) {
            GST_ELEMENT_ERROR(GST_ELEMENT(target), STREAM, FORMAT,
                              ("Expected H.265 RTSP video"), ("Unsupported RTP encoding"));
        } else {
            GstPad *sink = gst_element_get_static_pad(GST_ELEMENT(target), "sink");
            if (sink && !gst_pad_is_linked(sink)) {
                auto result = gst_pad_link(pad, sink);
                if (result != GST_PAD_LINK_OK)
                    GST_ELEMENT_ERROR(GST_ELEMENT(target), STREAM, FAILED,
                                      ("Unable to link H.265 RTP: %s", gst_pad_link_get_name(result)),
                                      ("RTSP dynamic pad link failed"));
            }
            if (sink) gst_object_unref(sink);
        }
    }
    gst_caps_unref(caps);
}
}

A8VideoPipeline::A8VideoPipeline(bool enabled, QObject *parent) : QObject(parent) {
    if (enabled) {
        m_sink = gst_element_factory_make("qmlglsink", "a8_video_sink");
        if (m_sink) {
            gst_object_ref_sink(m_sink); // Keep one reference across reconnects.
            g_object_set(m_sink, "sync", TRUE, nullptr);
        } else {
            m_state = "ERROR";
            m_error = "Qt5 qmlglsink is unavailable. Install/build the Qt5 qmlgl plugin.";
        }
    } else {
        m_state = "DISABLED";
        m_error = "软件渲染模式不支持实时 GL 视频";
    }
    m_poll.setInterval(500);
    connect(&m_poll, &QTimer::timeout, this, &A8VideoPipeline::poll);
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, [this] {
        if (m_wanted) { ++m_reconnects; begin(); }
    });
}

A8VideoPipeline::~A8VideoPipeline() {
    stop();
    if (m_sink) {
        g_object_set(m_sink, "widget", nullptr, nullptr);
        gst_object_unref(m_sink);
    }
}

void A8VideoPipeline::attach(QQuickItem *item) {
    stop();
    m_item = item;
    if (m_sink && item) {
        g_object_set(m_sink, "widget", item, nullptr);
        g_object_set(m_sink, "force-aspect-ratio", TRUE, nullptr);
        connect(item, &QObject::destroyed, this, [this] {
            stop();
            if (m_sink) g_object_set(m_sink, "widget", nullptr, nullptr);
        });
    }
}

void A8VideoPipeline::setState(const QString &state) {
    m_state = state;
    emit changed();
}

QString A8VideoPipeline::safeError(QString text) const {
    if (!m_password.isEmpty()) text.replace(m_password, "***");
    // The application passes credentials as properties, never in the URL.
    return text;
}

void A8VideoPipeline::play(const QString &url, const QString &username,
                           const QString &password, int latencyMs) {
    stop();
    const QUrl parsed(url.trimmed());
    if (!parsed.isValid() || parsed.scheme() != "rtsp" || parsed.host().isEmpty()
        || !parsed.userInfo().isEmpty()) {
        m_error = "请输入有效的 rtsp:// 地址；账号密码请填入独立字段";
        setState("ERROR");
        return;
    }
    m_source = Rtsp; m_url = parsed.toString();
    m_username = username; m_password = password;
    m_latency = qBound(80, latencyMs, 2000);
    m_wanted = true; m_failures = m_reconnects = 0;
    m_rendered = m_dropped = 0;
    begin();
}

void A8VideoPipeline::playTest() {
    stop(); m_source = Test; m_wanted = true;
    m_failures = m_reconnects = 0; m_rendered = m_dropped = 0;
    begin();
}

void A8VideoPipeline::playFile(const QString &path) {
    stop();
    QUrl url(path);
    if (url.scheme().isEmpty()) url = QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath());
    if (!url.isLocalFile() || !QFileInfo::exists(url.toLocalFile())) {
        m_error = "本地视频文件不存在"; setState("ERROR"); return;
    }
    m_source = File; m_url = url.toString(); m_wanted = true;
    m_failures = m_reconnects = 0; m_rendered = m_dropped = 0;
    begin();
}

void A8VideoPipeline::begin() {
    if (!m_sink || !m_item || !m_item->window()) {
        m_wanted = false;
        if (m_sink) m_error = "视频窗口尚未准备好";
        setState(m_sink ? "ERROR" : m_state);
        return;
    }
    dispose();
    m_decoder.clear(); m_width = m_height = 0; m_fps = 0;
    m_attemptFrames = 0; m_hadFrame = false;
    m_renderedBase = m_rendered; m_droppedBase = m_dropped;
    m_pipeline = gst_pipeline_new("a8_video");
    const char *description = m_source == Rtsp
        ? "rtph265depay name=rtp_depay ! h265parse ! decodebin ! videoconvert ! glupload ! glcolorconvert name=gl_out"
        : m_source == Test
        ? "videotestsrc is-live=true ! video/x-raw,width=1280,height=720,framerate=25/1 ! glupload ! glcolorconvert name=gl_out"
        : "uridecodebin name=file_source ! video/x-raw ! videoconvert ! glupload ! glcolorconvert name=gl_out";
    GError *error = nullptr;
    GstElement *body = gst_parse_bin_from_description(description, FALSE, &error);
    if (!body || error) {
        QString reason = error ? QString::fromUtf8(error->message) : "无法创建视频管线";
        if (error) g_error_free(error);
        if (body) gst_object_unref(body);
        m_wanted = false; dispose(); m_error = safeError(reason); setState("ERROR"); return;
    }
    // Explicit ghost pads: automatic ghosting can select videoconvert's sink
    // while decodebin's dynamic source pad is still absent.
    GstElement *out = gst_bin_get_by_name(GST_BIN(body), "gl_out");
    GstPad *outPad = gst_element_get_static_pad(out, "src");
    gst_element_add_pad(body, gst_ghost_pad_new("src", outPad));
    gst_object_unref(outPad); gst_object_unref(out);
    if (m_source == Rtsp) {
        GstElement *depay = gst_bin_get_by_name(GST_BIN(body), "rtp_depay");
        GstPad *inPad = gst_element_get_static_pad(depay, "sink");
        gst_element_add_pad(body, gst_ghost_pad_new("sink", inPad));
        gst_object_unref(inPad); gst_object_unref(depay);
    }
    gst_bin_add(GST_BIN(m_pipeline), body);
    gst_bin_add(GST_BIN(m_pipeline), GST_ELEMENT(gst_object_ref(m_sink)));
    if (!gst_element_link(body, m_sink)) {
        m_wanted = false; dispose(); m_error = "无法连接 GL 视频输出"; setState("ERROR"); return;
    }
    if (m_source == Rtsp) {
        GstElement *source = gst_element_factory_make("rtspsrc", "rtsp_source");
        if (!source) {
            m_wanted = false; dispose(); m_error = "缺少 rtspsrc 插件"; setState("ERROR"); return;
        }
        g_object_set(source, "location", m_url.toUtf8().constData(),
                     "user-id", m_username.toUtf8().constData(),
                     "user-pw", m_password.toUtf8().constData(),
                     "protocols", 4, "latency", m_latency,
                     "tcp-timeout", guint64(10000000), nullptr);
        gst_bin_add(GST_BIN(m_pipeline), source);
        g_signal_connect(source, "pad-added", G_CALLBACK(rtspPadAdded), body);
    } else if (m_source == File) {
        GstElement *source = gst_bin_get_by_name(GST_BIN(body), "file_source");
        g_object_set(source, "uri", m_url.toUtf8().constData(), nullptr);
        gst_object_unref(source);
    }
    m_bus = gst_element_get_bus(m_pipeline);
    m_startGuard = std::make_shared<StartGuard>();
    auto guard = m_startGuard;
    // Hold references until the scene graph has executed (or canceled) the job.
    auto pipeline = std::shared_ptr<GstElement>(GST_ELEMENT(gst_object_ref(m_pipeline)),
                                               [](GstElement *p) { gst_object_unref(p); });
    auto sink = std::shared_ptr<GstElement>(GST_ELEMENT(gst_object_ref(m_sink)),
                                           [](GstElement *p) { gst_object_unref(p); });
    m_item->window()->scheduleRenderJob(new RenderJob([guard, pipeline, sink] {
        std::lock_guard<std::mutex> lock(guard->mutex);
        if (guard->canceled) return;
        // Qt's GL context must exist, and qmlglsink must reach READY first.
        // This propagates Qt's GstGLDisplay before any other GL element starts.
        if (gst_element_set_state(sink.get(), GST_STATE_READY) == GST_STATE_CHANGE_FAILURE) {
            GST_ELEMENT_ERROR(pipeline.get(), RESOURCE, FAILED,
                              ("qmlglsink could not initialize the Qt OpenGL context"), ("READY state failed"));
            return;
        }
        gst_element_set_state(pipeline.get(), GST_STATE_PLAYING);
    }), QQuickWindow::BeforeSynchronizingStage);
    m_item->window()->update();
    m_progress.start(); m_fpsSample.start(); m_poll.start(); setState("CONNECTING");
}

void A8VideoPipeline::dispose() {
    m_poll.stop();
    if (m_startGuard) {
        std::lock_guard<std::mutex> lock(m_startGuard->mutex);
        m_startGuard->canceled = true;
    }
    m_startGuard.reset();
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        // m_sink also has our independent reference and can be reused safely.
        if (GST_OBJECT_PARENT(m_sink) == GST_OBJECT(m_pipeline))
            gst_bin_remove(GST_BIN(m_pipeline), m_sink);
        gst_object_unref(m_pipeline); m_pipeline = nullptr;
    }
    if (m_bus) { gst_object_unref(m_bus); m_bus = nullptr; }
}

void A8VideoPipeline::stop() {
    m_wanted = false; m_retry.stop(); dispose();
    if (m_sink) setState("STOPPED");
}

void A8VideoPipeline::retry(const QString &reason) {
    m_error = safeError(reason);
    setState("STALLED"); dispose();
    if (!m_wanted) return;
    const int delay = m_failures == 0 ? 1000 : m_failures == 1 ? 2000 : 5000;
    ++m_failures; m_retry.start(delay); setState("RECONNECTING");
}

void A8VideoPipeline::poll() {
    if (!m_pipeline || !m_bus) return;
    while (GstMessage *msg = gst_bus_pop(m_bus)) {
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
            GError *err = nullptr; gchar *debug = nullptr;
            gst_message_parse_error(msg, &err, &debug);
            QString reason = QString::fromUtf8(err->message);
            if (debug) reason += "\n" + QString::fromUtf8(debug);
            g_error_free(err); g_free(debug); gst_message_unref(msg);
            retry(reason); return;
        }
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
            gst_message_unref(msg);
            if (m_source == File) { stop(); setState("FINISHED"); }
            else retry("视频流已结束");
            return;
        }
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_BUFFERING) {
            gint percent = 0; gst_message_parse_buffering(msg, &percent);
            if (!m_hadFrame && percent < 100) setState("BUFFERING");
        }
        gst_message_unref(msg);
    }
    GstStructure *stats = nullptr;
    g_object_get(m_sink, "stats", &stats, nullptr);
    guint64 rendered = 0, dropped = 0;
    if (stats) {
        gst_structure_get_uint64(stats, "rendered", &rendered);
        gst_structure_get_uint64(stats, "dropped", &dropped);
        gst_structure_free(stats);
    }
    m_rendered = m_renderedBase + rendered; m_dropped = m_droppedBase + dropped;
    // GstBaseSink's average-rate is a timing/QoS ratio, not frames per second.
    const qint64 sampleMs = m_fpsSample.restart();
    if (sampleMs > 0)
        m_fps = double(rendered - qMin(rendered, m_attemptFrames)) * 1000.0 / double(sampleMs);
    if (rendered > m_attemptFrames) {
        m_attemptFrames = rendered; m_hadFrame = true; m_progress.restart();
        m_error.clear();
        m_state = "PLAYING";
        // Reset the backoff after sustained progress, not just one decoded frame.
        if (rendered >= 100) m_failures = 0;
    }
    GstPad *pad = gst_element_get_static_pad(m_sink, "sink");
    GstCaps *caps = pad ? gst_pad_get_current_caps(pad) : nullptr;
    if (caps && !gst_caps_is_empty(caps)) {
        const auto *s = gst_caps_get_structure(caps, 0);
        gst_structure_get_int(s, "width", &m_width);
        gst_structure_get_int(s, "height", &m_height);
    }
    if (caps) gst_caps_unref(caps);
    if (pad) gst_object_unref(pad);
    if (m_decoder.isEmpty()) {
        GstIterator *it = gst_bin_iterate_recurse(GST_BIN(m_pipeline));
        GValue value = G_VALUE_INIT;
        bool done = false;
        while (!done) {
            switch (gst_iterator_next(it, &value)) {
            case GST_ITERATOR_OK: {
                auto *element = GST_ELEMENT(g_value_get_object(&value));
                auto *factory = gst_element_get_factory(element);
                const char *klass = factory ? gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_KLASS) : nullptr;
                if (klass && QString::fromUtf8(klass).contains("Decoder/Video"))
                    m_decoder = QString::fromUtf8(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)));
                g_value_reset(&value); break;
            }
            case GST_ITERATOR_RESYNC: gst_iterator_resync(it); break;
            default: done = true; break;
            }
        }
        if (G_IS_VALUE(&value)) g_value_unset(&value);
        gst_iterator_free(it);
    }
    emit changed();
    // macOS can throttle GL presentation for inactive/occluded windows even
    // when isExposed() remains true. Only apply the presentation watchdog to
    // the active window; RTSP bus errors are still handled above in background.
    if (m_item && m_item->window() &&
        (!m_item->window()->isExposed() || !m_item->window()->isActive())) {
        m_progress.restart();
        return;
    }
    // MediaMTX may take 20 seconds to pull the camera on demand.
    if (m_progress.elapsed() > (m_hadFrame ? 5000 : 30000))
        retry(m_hadFrame ? "5 秒没有视频帧推进" : "30 秒内未收到首帧");
}
