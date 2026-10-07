import QtQuick 2.15
import "."

/* 视频面板：真实 RTSP 画面 + 未接通时的占位图 + 状态角标。
 * 保持原有接口（thermal / clean），4 处调用点无需修改。 */
Rectangle {
    id: root
    property bool thermal: false
    property bool clean: false
    property bool showBadge: !clean
    color: "#071a29"
    radius: 5
    clip: true

    // ── 真实视频流 ──────────────────────────────────────────────
    // 小缩略图（clean）保持静态占位，不额外拉一路流
    VideoStream {
        id: stream
        anchors.fill: parent
        visible: !root.clean
        enabled: !root.clean
    }

    // ── 占位图（未出画面时显示，避免面板空白）──────────────────
    Image {
        anchors.fill: parent
        source: "qrc:/assets/video.png"
        fillMode: Image.Stretch
        smooth: true
        visible: !root.clean && !stream.live
        opacity: root.thermal ? 0.55 : 0.85
    }
    Item {
        anchors.fill: parent
        visible: root.clean
        clip: true
        Image {
            source: "qrc:/assets/video.png"
            width: 404; height: 336
            scale: Math.max(root.width / 404, root.height / 218)
            transformOrigin: Item.TopLeft
            x: -(width * scale - root.width) / 2
            y: -82 * scale
            smooth: true
        }
    }

    // ── 热成像色调演示叠层（沿用原行为）────────────────────────
    Rectangle {
        anchors.fill: parent
        visible: root.thermal
        color: "#50206c24"
    }

    // ── 状态角标 ────────────────────────────────────────────────
    Rectangle {
        visible: root.showBadge
        anchors.left: parent.left; anchors.top: parent.top
        anchors.margins: 6
        height: 24; width: badgeRow.width + 16; radius: 4
        color: "#cc04121d"
        border.color: stream.phase === "live" ? "#00db80"
                      : stream.phase === "connecting" ? "#f0a51e" : "#5b7488"
        Row {
            id: badgeRow
            anchors.centerIn: parent
            spacing: 6
            Rectangle {
                width: 8; height: 8; radius: 4; anchors.verticalCenter: parent.verticalCenter
                color: stream.phase === "live" ? "#00db80"
                     : stream.phase === "connecting" ? "#f0a51e"
                     : stream.phase === "retry" ? "#f0a51e" : "#8394a3"
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: "#dbe8f4"; font.pixelSize: 12
                text: stream.phase === "live" ? "图传实时"
                    : stream.phase === "connecting" ? "连接中"
                    : stream.phase === "retry" ? "重连中" + (stream.failures > 0 ? "（第" + stream.failures + "次）" : "")
                    : stream.phase === "error" ? "图传异常" : "未接入"
            }
        }
    }

    // 管线信息（仅大面板显示）
    Rectangle {
        visible: root.showBadge && !root.thermal
        anchors.left: parent.left; anchors.bottom: parent.bottom
        anchors.margins: 6
        height: 22; width: infoText.width + 14; radius: 4
        color: "#b304121d"
        Text {
            id: infoText
            anchors.centerIn: parent
            color: "#8fa7bb"; font.pixelSize: 11
            text: stream.usingPipeline ? "GStreamer · TCP · auto H.264/H.265"
                                       : "Qt 默认管线 · " + VideoConfig.url
        }
    }

    // 连接失败时点击画面即可立即重连
    MouseArea {
        anchors.fill: parent
        enabled: root.showBadge && (stream.phase === "retry" || stream.phase === "error")
        onClicked: stream.reconnectNow()
    }
}
