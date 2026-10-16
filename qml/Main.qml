import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import "pages"
import "components"

ApplicationWindow {
    id: window
    visible: true
    width: 1536
    height: 1024
    minimumWidth: 1024
    minimumHeight: 680
    title: "无人机地面站"
    color: "#020d16"

    // 启动页/分组可由命令行指定（--start-page / --start-section）。
    // initialPage / initialSection 是 C++ 注入的上下文属性，始终存在。
    // ⚠️ 不要写成绑定 `property int pageIndex: initialPage`：
    //    实测这种形式会静默回落到类型默认值 0（不报错），
    //    必须在 Component.onCompleted 里显式赋值。
    property int pageIndex: 0
    property int previewTab: 0
    property int previewSection: 0
    Component.onCompleted: {
        pageIndex = initialPage
        previewSection = initialSection
    }
    property bool previewMode: false
    property bool overviewSettings: false
    property real designScale: Math.min(width / 1536, height / 1024)

    // 自动发现到机载电脑后直接采用该地址：
    // 视频（pipelineRevision 变化）与云台/照片（relayHost 派生）都会跟着切
    Connections {
        target: relayDiscovery
        function onFound(host) {
            console.log("[Main] 自动发现机载电脑:", host)
            VideoConfig.host = host
        }
    }

    // 连不上时自动扫一次网段（每个会话只自动扫一次，避免反复扫网段）
    property bool autoScanned: false
    Connections {
        target: VideoConfig
        function onRuntimePhaseChanged() {
            if (autoScanned || relayDiscovery.scanning) return
            if (VideoConfig.runtimePhase === "retry" || VideoConfig.runtimePhase === "error") {
                autoScanned = true
                console.log("[Main] 视频连不上，自动扫描机载电脑…")
                relayDiscovery.scan(VideoConfig.host)
            }
        }
    }

    Item {
        id: stage
        width: 1536
        height: 1024
        scale: window.designScale
        transformOrigin: Item.TopLeft
        x: (window.width - width * scale) / 2
        y: (window.height - height * scale) / 2

        Rectangle {
            anchors.fill: parent
            color: "#020d16"

            Rectangle {
                id: header
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 58
                color: "#041724"
                border.color: "#0a283d"

                Row {
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 14
                    Canvas {
                        width: 40; height: 34
                        onPaint: {
                            var c = getContext("2d")
                            c.strokeStyle = "#0788ff"; c.lineWidth = 2
                            c.beginPath(); c.moveTo(6,6); c.lineTo(33,27)
                            c.moveTo(33,6); c.lineTo(6,27); c.stroke()
                            for (var i=0;i<4;i++) {
                                var cx = i%2 ? 32 : 8; var cy = i<2 ? 7 : 26
                                c.beginPath(); c.ellipse(cx-7,cy-3,14,6); c.stroke()
                            }
                            c.fillStyle = "#0788ff"; c.fillRect(16,11,8,13)
                        }
                    }
                    Text {
                        text: "无人机地面站"
                        color: "#f4f8ff"
                        font.pixelSize: 23
                        font.bold: true
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text { text: "演示"; color: "#718da5"; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                }

                Row {
                    x: 315
                    height: parent.height
                    Repeater {
                        model: ["任务执行", "日志", "系统设置"]
                        delegate: Rectangle {
                            width: 120
                            height: header.height
                            color: window.pageIndex === index ? "#0755a5" : "transparent"
                            Rectangle {
                                visible: window.pageIndex === index
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 4
                                color: "#0a8cff"
                            }
                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                color: window.pageIndex === index ? "#ffffff" : "#dce9f7"
                                font.pixelSize: 18
                                font.bold: window.pageIndex === index
                            }
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: window.pageIndex = index
                                onDoubleClicked: if (index === 2) { window.overviewSettings = !window.overviewSettings; window.previewSection=0 }
                            }
                        }
                    }
                }

                Row {
                    anchors.right: clock.left
                    anchors.rightMargin: 28
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 26
                    Row {
                        spacing: 7
                        Rectangle { width: 12; height: 12; radius: 6; color: groundLink.connected && groundLink.fcuConnected ? "#00db80" : "#8394a3"; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: "无人机："; color: "#e4effa"; font.pixelSize: 14 }
                        Text { text: groundLink.connected && groundLink.fcuConnected ? "已连接" : "未连接"; color: groundLink.connected && groundLink.fcuConnected ? "#00e787" : "#9aafc0"; font.pixelSize: 14; font.bold: true }
                    }
                    Row {
                        spacing: 7
                        Rectangle { width: 12; height: 12; radius: 6; color: "#8394a3"; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: "机巢："; color: "#e4effa"; font.pixelSize: 14 }
                        Text { text: "演示"; color: "#9aafc0"; font.pixelSize: 14; font.bold: true }
                    }
                }

                Text {
                    id: clock
                    anchors.right: controls.left
                    anchors.rightMargin: 26
                    anchors.verticalCenter: parent.verticalCenter
                    text: window.previewMode ? "2024-12-26 15:30:24" : Qt.formatDateTime(new Date(), "yyyy-MM-dd  HH:mm:ss")
                    color: "#e4effa"
                    font.pixelSize: 13
                }
                Timer { interval: 1000; running: !window.previewMode; repeat: true; onTriggered: clock.text = Qt.formatDateTime(new Date(), "yyyy-MM-dd  HH:mm:ss") }

                Row {
                    id: controls
                    anchors.right: parent.right
                    anchors.rightMargin: 13
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 20
                    Text { text: "—"; color: "white"; font.pixelSize: 19; MouseArea { anchors.fill: parent; onClicked: window.showMinimized() } }
                    Text { text: "□"; color: "white"; font.pixelSize: 19; MouseArea { anchors.fill: parent; onClicked: window.visibility === Window.Maximized ? window.showNormal() : window.showMaximized() } }
                    Text { text: "×"; color: "white"; font.pixelSize: 23; MouseArea { anchors.fill: parent; onClicked: window.close() } }
                }
            }

            Loader {
                // Keep the task session alive: navigating away must not discard
                // edited coordinates, coordinate review or pending command ACKs.
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: header.bottom
                anchors.bottom: parent.bottom
                visible: window.pageIndex === 0
                sourceComponent: taskPage
            }
            Loader {
                property bool loadedOnce: false
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: header.bottom
                anchors.bottom: parent.bottom
                active: window.pageIndex === 1 || loadedOnce
                visible: window.pageIndex === 1
                sourceComponent: logPage
                onLoaded: loadedOnce = true
            }
            Loader {
                // Retain the settings video surface too: the Qt5 GL sink can
                // still have render jobs queued when a page is switched away.
                property bool loadedOnce: false
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: header.bottom
                anchors.bottom: parent.bottom
                active: window.pageIndex === 2 || loadedOnce
                visible: window.pageIndex === 2
                sourceComponent: settingsPage
                onLoaded: loadedOnce = true
            }
            Component { id: taskPage; TaskPage { objectName: "taskPage"; contentTab: window.previewTab; onRequestSettings: {window.previewSection=2;window.pageIndex=2} } }
            Component { id: logPage; LogPage { objectName: "logPage"; sectionIndex: Math.min(5, window.previewSection) } }
            Component { id: settingsPage; SettingsPage { objectName: "settingsPage"; sectionIndex: window.previewSection; overviewMode: window.overviewSettings } }
        }
    }
}
