pragma Singleton
import QtQuick 2.15
import Qt.labs.settings 1.1

/* 视频流配置（全局单例）。
 *
 * 地址依据：
 *  - 《A8 mini 云台相机用户手册》2.3 节 RTSP 地址表：
 *      思翼AI相机      rtsp://192.168.144.60:554/video0
 *      吊舱/云台主码流  rtsp://192.168.144.25:8554/video1
 *      吊舱/云台副码流  rtsp://192.168.144.25:8554/video2
 *      三防摄像头/天空端HDMI  rtsp://192.168.144.25:8554/main.264
 *  - 实机记录（diff-planner A8mini/README.md）：A8 mini 走的是旧地址 main.264，
 *    并且【流内容是 H.265，不能按文件名当成 H.264 解】，传输用 TCP。
 *  手册同时说明同一 RTSP 地址最多支持 4 路同时拉流，因此地面站拉流不会
 *  干扰机载电脑上的识别程序。
 */
Item {
    id: cfg

    // ── 预设地址 ────────────────────────────────────────────────
    // 实测可用链路（2026-10-07 验证）：
    //   A8 mini(192.168.144.25:8554/main.264, H.265)
    //     → Jetson 网卡 rtl8168 (192.168.144.30)
    //     → Jetson 上的 MediaMTX 中转 (0.0.0.0:8554, 路径 a8mini, read 需认证)
    //     → 地面站经 Wi-Fi 拉流 rtsp://192.168.2.113:8554/a8mini
    //   MediaMTX 只开 TCP，且该流实际编码是 H.265（配置里注明 "named main.264
    //   but carries H.265"）。中转按需拉流（sourceOnDemand），不影响相机。
    readonly property var codecNames: ["自动识别", "H.264", "H.265"]
    readonly property var transportNames: ["TCP（推荐）", "UDP"]

    /* ── 只配一处：机载电脑地址 ──────────────────────────────────
     *
     * 图传、云台控制、照片/视频取回都在【同一台机载电脑】上，所以界面上
     * 只需要填这一个地址（主机名或 IP），其余全部推导：
     *     图传  rtsp://<host>:8554/a8mini
     *     云台  <host>:37260（UDP）
     *     照片  <host>:37261（HTTP）
     *
     * 这样不会出现"视频连的是这台、云台连的是那台"的不一致；
     * 换机器只改这一个地址。特殊场景（例如地面站直接接在相机网段）
     * 也可以填完整的 rtsp:// 地址，此时原样使用。
     */
    property string host: "192.168.2.113"
    readonly property int rtspPort: 8554
    readonly property string streamPath: "a8mini"    // MediaMTX 上的路径名
    readonly property string url: host.indexOf("rtsp://") === 0
                                   ? host
                                   : "rtsp://" + host + ":" + rtspPort + "/" + streamPath

    readonly property string relayHost: {
        var match = /^rtsp:\/\/(?:[^@\/]*@)?([^:\/]+)/.exec(url)
        return match ? match[1] : "192.168.2.113"
    }
    readonly property int gimbalPort: 37260
    readonly property int photoPort: 37261
    readonly property string gimbalEndpoint: relayHost + ":" + gimbalPort
    readonly property string photoEndpoint: relayHost + ":" + photoPort
    // 派生地址一览（设置页显示用；短形式，主机名已在上面的输入框里）
    readonly property string derivedSummary: "图传 :" + rtspPort + "/" + streamPath
                                             + "　云台 :" + gimbalPort
                                             + "　照片 :" + photoPort

    // ── 持久化字段 ──────────────────────────────────────────────
    // MediaMTX 只读账号（read 权限仅限 a8mini 路径、仅限本地网段）
    // 口令不写进源码：首次使用时在「系统设置 → 认证账号」里填写，
    // 或写入本机配置 ~/.config/<应用>/VideoStream.conf。
    property string userId: "a8viewer"
    property string userPw: ""
    property int codecIndex: 0        // 0=自动 1=H.264 2=H.265
    property int transportIndex: 0    // 0=TCP 1=UDP
    property int latency: 200         // ms，仅 GStreamer 路径生效
    property bool enabled: true       // 关闭后不建立任何连接
    property bool livePanelVisible: true

    readonly property string codec: codecIndex === 1 ? "h264" : codecIndex === 2 ? "h265" : "auto"
    readonly property string transport: transportIndex === 1 ? "udp" : "tcp"
    readonly property bool valid: url.length > 6 && url.indexOf("rtsp://") === 0

    // 影响流水线的全部因素；变化即触发重建连接
    readonly property string pipelineRevision: url + "|" + transport + "|" + codec + "|" + latency

    // ── 运行时状态（由 VideoStream 写入，供各页面统一显示）──────
    property string runtimePhase: "off"      // off|connecting|live|retry|error
    property string runtimeDetail: "未接入"
    property bool runtimeLive: false
    property int runtimeFailures: 0
    property bool runtimeUsingPipeline: true
    // 外部请求立即重连（自增即触发，不做持久化）
    property int reconnectRequest: 0

    Settings {
        category: "VideoStream"
        property alias host: cfg.host
        property alias userId: cfg.userId
        property alias userPw: cfg.userPw
        property alias codecIndex: cfg.codecIndex
        property alias transportIndex: cfg.transportIndex
        property alias latency: cfg.latency
        property alias enabled: cfg.enabled
        property alias livePanelVisible: cfg.livePanelVisible
    }

    /* 构造自定义 GStreamer 流水线。
     * decodebin 会自动选择 H.264/H.265 解复用与解码器，避免把 main.264
     * 误当成 H.264；指定编码时则强制对应的 depay/parse/decoder。
     * 认证用 rtspsrc 的 user-id / user-pw 属性（MediaMTX 的只读账号）。 */
    function buildPipeline() {
        var q = '"'
        var p = "rtspsrc location=" + q + url + q
              + " protocols=" + transport
              + " latency=" + latency
              + " drop-on-latency=true"
        if (transport === "tcp")
            p += " tcp-timeout=5000000"
        if (userId.length > 0)
            p += " user-id=" + q + userId + q + " user-pw=" + q + userPw + q
        p += " ! "
        if (codec === "h264")
            p += "rtph264depay ! h264parse ! avdec_h264"
        else if (codec === "h265")
            p += "rtph265depay ! h265parse ! avdec_h265"
        else
            p += "decodebin"
        p += " ! videoconvert ! qtvideosink"
        return "gst-pipeline: " + p
    }

    // 回退用的普通地址（交给 Qt 自己的 playbin 处理）
    function plainUrl() { return url }

    function summary() {
        return relayHost + " · " + transport.toUpperCase()
             + " · " + codecNames[codecIndex] + " · " + latency + "ms"
    }
}
