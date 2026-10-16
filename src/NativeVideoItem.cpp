#include "NativeVideoItem.h"
#ifdef AGRI_GCS_NATIVE_GSTREAMER
#include "A8VideoPipeline.h"
#endif

NativeVideoItem::NativeVideoItem(QQuickItem *parent) : QQuickItem(parent) {
#ifdef AGRI_GCS_NATIVE_GSTREAMER
    const QByteArray root = AGRI_GCS_GSTREAMER_ROOT;
    if (!qEnvironmentVariableIsSet("GST_PLUGIN_SYSTEM_PATH_1_0"))
        qputenv("GST_PLUGIN_SYSTEM_PATH_1_0", root + "/lib/gstreamer-1.0");
    if (!qEnvironmentVariableIsSet("GST_PLUGIN_SCANNER_1_0"))
        qputenv("GST_PLUGIN_SCANNER_1_0", root + "/libexec/gstreamer-1.0/gst-plugin-scanner");
    const QByteArray pluginPath = AGRI_GCS_QMLGL_PLUGIN_PATH;
    const QByteArray extraPlugins = qgetenv("GST_PLUGIN_PATH_1_0");
    qputenv("GST_PLUGIN_PATH_1_0", pluginPath + (extraPlugins.isEmpty() ? QByteArray() : ":" + extraPlugins));
    gst_init(nullptr, nullptr);
    // 优先 Apple VideoToolbox 硬件解码，与参考项目的 vtdec_hw 路径保持一致。
    if (GstElementFactory *factory = gst_element_factory_find("vtdec_hw")) {
        gst_plugin_feature_set_rank(GST_PLUGIN_FEATURE(factory), GST_RANK_PRIMARY + 100);
        gst_object_unref(factory);
    }
    m_video = std::make_unique<A8VideoPipeline>(qgetenv("QT_QUICK_BACKEND") != "software");
    connect(m_video.get(), &A8VideoPipeline::changed, this, &NativeVideoItem::changed);
#endif
}
NativeVideoItem::~NativeVideoItem() = default;
bool NativeVideoItem::available() const {
#ifdef AGRI_GCS_NATIVE_GSTREAMER
    return m_video->available();
#else
    return false;
#endif
}
QString NativeVideoItem::phase() const {
#ifdef AGRI_GCS_NATIVE_GSTREAMER
    const QString state = m_video->state();
    if (state == "PLAYING") return "live";
    if (state == "CONNECTING" || state == "BUFFERING") return "connecting";
    if (state == "RECONNECTING" || state == "STALLED") return "retry";
    if (state == "ERROR") return "error";
#endif
    return "off";
}
QString NativeVideoItem::detail() const {
#ifdef AGRI_GCS_NATIVE_GSTREAMER
    if (!m_video->lastError().isEmpty()) return m_video->lastError();
    if (live()) return QStringLiteral("实时画面 · %1 · %2×%3").arg(m_video->decoderName()).arg(m_video->width()).arg(m_video->height());
    return QStringLiteral("正在连接相机…");
#else
    return QStringLiteral("未启用原生 GStreamer");
#endif
}
bool NativeVideoItem::live() const { return phase() == "live"; }
int NativeVideoItem::failures() const {
#ifdef AGRI_GCS_NATIVE_GSTREAMER
    return m_video->reconnectCount();
#else
    return 0;
#endif
}
qulonglong NativeVideoItem::frames() const {
#ifdef AGRI_GCS_NATIVE_GSTREAMER
    return m_video->renderedFrames();
#else
    return 0;
#endif
}
QString NativeVideoItem::decoder() const {
#ifdef AGRI_GCS_NATIVE_GSTREAMER
    return m_video->decoderName();
#else
    return {};
#endif
}
void NativeVideoItem::attach(QQuickItem *item) {
#ifdef AGRI_GCS_NATIVE_GSTREAMER
    m_video->attach(item);
#else
    Q_UNUSED(item)
#endif
}
void NativeVideoItem::play(const QString &url, const QString &user, const QString &password, int latency, const QString &transport) {
#ifdef AGRI_GCS_NATIVE_GSTREAMER
    Q_UNUSED(transport) // 参考实现固定 TCP，避免 Wi-Fi 丢包损坏 H.265 参考帧。
    m_video->play(url, user, password, latency);
#else
    Q_UNUSED(url) Q_UNUSED(user) Q_UNUSED(password) Q_UNUSED(latency) Q_UNUSED(transport)
#endif
}
void NativeVideoItem::stop() {
#ifdef AGRI_GCS_NATIVE_GSTREAMER
    m_video->stop();
#endif
}
