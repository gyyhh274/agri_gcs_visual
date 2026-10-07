import QtQuick 2.15
import QtMultimedia 5.15
import "."

/* 实时视频流播放器。
 *
 * 设计要点：
 *  1. 首选自定义 GStreamer 流水线（gst-pipeline:），这样能强制 TCP、设置
 *     latency、并用 decodebin 自动识别 H.264/H.265（实机 main.264 实际是 H.265）。
 *  2. 若自定义流水线起不来（Qt 版本/后端差异），自动回退到普通 rtsp:// 地址，
 *     由 Qt 自己的 playbin 处理，保证"至少能出画面"。
 *  3. 断流/卡死/结束都会按指数退避自动重连，界面不阻塞。
 *  4. 所有启动请求都经过 600ms 合并延时：必须等窗口暴露完成，
 *     qtvideosink 才能挂到 VideoOutput 上，否则流水线会停在 Loaded 永不播放。
 */
Item {
    id: root
    clip: true

    // 本面板是否参与拉流。小缩略图（clean 面板）不拉流，
    // 避免同一路相机被解码多次，白吃 CPU。
    property bool enabled: true
    // 期望是否拉流（配置关闭或地址非法时为 false）
    property bool wantActive: enabled && VideoConfig.enabled && VideoConfig.valid
    // 对外状态：off | connecting | live | retry | error
    property string phase: "off"
    property string detail: "未启用"
    property bool live: false
    property int failures: 0
    property bool pipelineMode: true      // 当前是否在用自定义流水线
    readonly property bool usingPipeline: pipelineMode
    readonly property string currentUrl: VideoConfig.url

    property bool retryPending: false
    // 诊断输出开关（排查"假在线"等问题时打开）
    property bool diagnostics: Qt.application.arguments.indexOf("--video-diag") >= 0

    /* 真出帧判据：Qt 的 playbin 在 RTSP 连不上时也可能短暂上报
     * Buffered + Playing（黑屏却显示"实时"）。实验确认唯一可靠的信号是
     * position 是否持续推进，因此 live 必须同时满足"position 在动"。 */
    property double lastPosition: -1
    property double lastProgressAt: 0
    readonly property bool progressing: (Date.now() - lastProgressAt) < 2500

    function logStatus(text) {
        console.log("[VideoStream] " + text)
    }

    // 把状态发布到配置单例，供设置页/状态表统一显示
    function publish() {
        if (!enabled) return
        VideoConfig.runtimePhase = phase
        VideoConfig.runtimeDetail = detail
        VideoConfig.runtimeLive = live
        VideoConfig.runtimeFailures = failures
        VideoConfig.runtimeUsingPipeline = pipelineMode
    }
    onPhaseChanged: publish()
    onDetailChanged: publish()
    onLiveChanged: publish()
    onFailuresChanged: publish()
    onPipelineModeChanged: publish()

    function refreshState() {
        if (!wantActive) {
            root.phase = "off"
            root.detail = VideoConfig.enabled ? "地址无效" : "视频流已关闭"
            root.live = false
            return
        }
        if (retryPending) {
            // 正在退避等待重连：保持"重连中（第 N 次）"，不要被后到的状态覆盖掉
            root.live = false
            root.phase = "retry"
            return
        }
        if (player.error !== MediaPlayer.NoError) {
            root.phase = "error"
            root.detail = player.errorString
            root.live = false
            return
        }
        switch (player.status) {
        case MediaPlayer.Buffered:
        case MediaPlayer.Buffering:
            if (player.playbackState === MediaPlayer.PlayingState) {
                if (!progressing) {
                    // 状态说在播、但 position 不推进 = 还没真正出帧
                    root.phase = "connecting"
                    root.detail = "等待画面数据…"
                    break
                }
                root.phase = "live"
                root.detail = "实时画面 · " + Math.round(VideoConfig.latency) + "ms 缓冲"
                root.live = true
                return
            }
            break
        case MediaPlayer.Loading:
            root.phase = "connecting"
            root.detail = "正在连接 " + VideoConfig.url
            break
        case MediaPlayer.Stalled:
            root.phase = "retry"
            root.detail = "画面卡顿，准备重连"
            break
        case MediaPlayer.EndOfMedia:
            root.phase = "retry"
            root.detail = "流已结束，准备重连"
            break
        case MediaPlayer.InvalidMedia:
            root.phase = "retry"
            root.detail = "无法解码该视频流"
            break
        default:
            root.phase = "connecting"
            root.detail = "正在建立连接…"
        }
        root.live = false
    }

    // 合并式启动请求：600ms 内的多次请求只执行最后一次
    function requestStart() {
        retryTimer.stop()
        failures = 0
        pipelineMode = true
        retryPending = false
        if (wantActive)
            startDelay.restart()
        else
            stopNow()
    }

    function stopNow() {
        retryTimer.stop()
        startDelay.stop()
        loadedWatchdog.stop()
        player.stop()
        player.source = ""
        refreshState()
    }

    // 真正建立连接
    function applySource() {
        retryPending = false
        if (!wantActive) { stopNow(); return }
        var next = pipelineMode ? VideoConfig.buildPipeline() : VideoConfig.plainUrl()
        // 重新计时：新连接在真正出帧前不得被判为 live
        lastPosition = -1
        lastProgressAt = 0
        // 先清空再赋值，确保同一个地址也能重新加载
        player.stop()
        player.source = ""
        player.source = next
        player.play()
        loadedWatchdog.restart()
        phase = "connecting"
        detail = (pipelineMode ? "连接中（TCP/H.265 流水线）" : "连接中（Qt 默认管线）")
        logStatus("启动 " + (pipelineMode ? "pipeline" : "plain") + " -> " + next)
    }

    function scheduleRetry() {
        if (retryPending) return          // 同一次故障（error + InvalidMedia）只排一次
        retryPending = true
        failures += 1
        // 交替尝试自定义流水线与 Qt 默认管线，避免一直卡在同一种方式
        pipelineMode = (failures % 2 === 1)
        var delay = Math.min(1000 * Math.pow(2, Math.min(failures - 1, 3)), 8000)
        phase = "retry"
        detail = "连接失败，第 " + failures + " 次重试（" + Math.round(delay / 1000) + "s 后）"
        logStatus("失败重试 #" + failures + " delay=" + delay + " pipeline=" + pipelineMode)
        retryTimer.interval = delay
        retryTimer.restart()
    }

    function reconnectNow() {
        requestStart()
    }

    MediaPlayer {
        id: player
        autoPlay: false
        // 直播流不做循环，结束由我们自己重连
        onErrorChanged: {
            if (player.error === MediaPlayer.NoError)
                return
            root.live = false
            root.logStatus("error=" + player.error + " " + player.errorString)
            root.scheduleRetry()
        }
        onStatusChanged: {
            root.logStatus("status=" + player.status + " playbackState=" + player.playbackState)
            if (player.status === MediaPlayer.EndOfMedia || player.status === MediaPlayer.Stalled)
                root.scheduleRetry()
            else
                root.refreshState()
        }
        onPositionChanged: {
            if (player.position !== root.lastPosition) {
                root.lastPosition = player.position
                root.lastProgressAt = Date.now()
            }
        }
        onPlaybackStateChanged: root.refreshState()
    }

    VideoOutput {
        id: output
        anchors.fill: parent
        source: player
        fillMode: VideoOutput.PreserveAspectCrop
    }

    Timer {
        id: retryTimer
        repeat: false
        onTriggered: root.applySource()
    }

    // 启动延时：等窗口暴露完成，同时合并同一时间窗内的多次启动请求
    Timer {
        id: startDelay
        interval: 600
        repeat: false
        onTriggered: root.applySource()
    }

    // 看门狗：连上了却始终不播放（停在 Loaded）就强制重启，避免"黑屏假在线"
    Timer {
        id: loadedWatchdog
        interval: 4000
        repeat: false
        onTriggered: {
            if (!root.wantActive)
                return
            if (player.status === MediaPlayer.Loaded
                    && player.playbackState !== MediaPlayer.PlayingState) {
                root.logStatus("卡在 Loaded 未播放，强制重启")
                root.scheduleRetry()
            }
        }
    }

    // 画面推进看门狗：声称在播但 position 冻结（假在线）时强制重连
    Timer {
        interval: 2000
        repeat: true
        running: root.wantActive
        onTriggered: {
            if (root.phase === "live" && !root.progressing) {
                root.logStatus("画面停止推进（假在线），强制重连")
                root.scheduleRetry()
            } else {
                root.refreshState()
            }
        }
    }

    // 诊断：每秒打印播放器指标，用于判定"假在线"（连不上却报 Buffered/Playing）
    Timer {
        interval: 1000
        repeat: true
        running: root.diagnostics
        onTriggered: console.log("[VS-diag] status=" + player.status
            + " state=" + player.playbackState
            + " pos=" + Math.round(player.position)
            + " buf=" + player.bufferStatus
            + " hasVideo=" + player.hasVideo
            + " seekable=" + player.seekable
            + " live=" + root.live)
    }

    // 地址、传输方式、编码或缓冲变化都要重建连接
    Connections {
        target: VideoConfig
        function onPipelineRevisionChanged() { if (root.wantActive) root.requestStart() }
        function onReconnectRequestChanged() { VideoConfig.enabled = true; root.requestStart() }
    }

    onWantActiveChanged: wantActive ? requestStart() : stopNow()

    Component.onCompleted: if (wantActive) requestStart()
}
