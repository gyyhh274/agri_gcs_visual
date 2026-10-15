import QtQuick 2.15
import QtQuick.Controls 2.15
import Agri.Map 1.0

Rectangle {
    id: panel
    objectName: "realtimeMapPanel"
    color: "#071522"
    radius: 5
    border.color: "#16435c"
    clip: true

    property var client: mappingClient
    readonly property bool hasPoints: client.pointCount > 0
    property string notice: ""
    signal notified(string message)

    function connectSource() {
        endpointHost.text = client.host
        endpointPort.text = String(client.port)
        endpointError.text = ""
        connectionDialog.open()
    }
    function report(message) {
        notice = message
        noticeTimer.restart()
        notified(message)
    }

    Connections {
        target: panel.client
        function onExportFinished(success, message) { panel.report(message) }
    }
    Timer { id: noticeTimer; interval: 6500; onTriggered: panel.notice = "" }

    PointCloudView {
        id: cloud
        objectName: "pointCloudView"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tools.bottom
        anchors.bottom: footer.top
        source: panel.client
        follow: false
        topDown: false
        colorMode: 0

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            property real lastX: 0
            property real lastY: 0
            onPressed: { lastX = mouse.x; lastY = mouse.y }
            onPositionChanged: {
                if (!pressed) return
                var dx = mouse.x - lastX
                var dy = mouse.y - lastY
                if ((pressedButtons & (Qt.RightButton | Qt.MiddleButton)) || (mouse.modifiers & Qt.ShiftModifier))
                    cloud.pan(dx, dy)
                else
                    cloud.orbit(dx, dy)
                lastX = mouse.x
                lastY = mouse.y
            }
            onWheel: cloud.zoom(wheel.angleDelta.y / 120)
            onDoubleClicked: cloud.fitMap()
        }
    }

    Rectangle {
        id: tools
        width: parent.width
        height: 48
        color: "#0a2131"
        Row {
            x: 10; y: 9; spacing: 6
            GButton { objectName: "mappingOrbitButton"; width: 48; height: 30; pixelSize: 12; text: "三维"; checked: !cloud.topDown; fill: !cloud.topDown ? "#116282" : "#163b5e"; onClicked: cloud.topDown = false }
            GButton { objectName: "mappingTopButton"; width: 48; height: 30; pixelSize: 12; text: "俯视"; checked: cloud.topDown; fill: cloud.topDown ? "#116282" : "#163b5e"; onClicked: cloud.topDown = true }
            GButton { objectName: "mappingFollowButton"; width: 54; height: 30; pixelSize: 12; text: "跟随"; checked: cloud.follow; fill: cloud.follow ? "#116282" : "#163b5e"; onClicked: cloud.follow = !cloud.follow }
            GButton { objectName: "mappingFitButton"; width: 54; height: 30; pixelSize: 12; text: "全图"; onClicked: { cloud.follow = false; cloud.fitMap() } }
            ComboBox {
                id: colors
                objectName: "mappingColorMode"
                width: 88; height: 30
                model: ["高度着色", "反射强度"]
                font.pixelSize: 12
                onActivated: cloud.colorMode = currentIndex
                contentItem: Text { text: colors.displayText; color: "#dce9f7"; verticalAlignment: Text.AlignVCenter; leftPadding: 9; rightPadding: 14; font.pixelSize: 12 }
                background: Rectangle { color: "#163b5e"; radius: 4; border.color: "#24526e" }
            }
            GButton { objectName: "mappingPauseButton"; width: 54; height: 30; pixelSize: 12; text: panel.client.paused ? "继续" : "暂停"; fill: panel.client.paused ? "#86602c" : "#163b5e"; onClicked: panel.client.paused = !panel.client.paused }
            GButton { objectName: "mappingClearButton"; width: 48; height: 30; pixelSize: 12; text: "清空"; onClicked: clearDialog.open() }
            GButton { objectName: "mappingExportButton"; width: 48; height: 30; pixelSize: 12; text: "导出"; enabled: panel.hasPoints; opacity: enabled ? 1 : 0.45; onClicked: { exportPath.text = panel.client.defaultExportPath; exportDialog.open() } }
            GButton { objectName: "mappingConnectButton"; width: 54; height: 30; pixelSize: 12; text: "连接"; onClicked: panel.connectSource() }
        }
        Rectangle {
            anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
            width: 101; height: 26; radius: 4
            color: panel.client.demo ? "#533e20" : panel.client.connected && !panel.client.stale ? "#0d403a" : "#243442"
            Row {
                anchors.centerIn: parent; spacing: 6
                Rectangle { width: 6; height: 6; radius: 3; anchors.verticalCenter: parent.verticalCenter; color: panel.client.demo ? "#ffc875" : panel.client.connected && !panel.client.stale ? "#3ce6bb" : "#869ead" }
                Text { text: panel.client.demo ? "演示数据" : panel.client.connected ? (panel.client.stale ? (panel.hasPoints ? "数据过期" : "等待数据") : "LIVE") : "未连接"; color: panel.client.demo ? "#ffcb83" : "#cce9ec"; font.pixelSize: 11; font.bold: true }
            }
        }
    }

    Column {
        anchors.left: parent.left; anchors.leftMargin: 14
        anchors.top: tools.bottom; anchors.topMargin: 12
        spacing: 5
        Text { text: "WORLD / 三维环境"; color: "#89afb8"; font.pixelSize: 11; font.letterSpacing: 1.2 }
        Text {
            visible: panel.client.demo || panel.client.paused || (panel.hasPoints && (!panel.client.connected || panel.client.stale))
            text: panel.client.demo ? "演示 · 合成场景，非机载数据" : panel.client.paused ? "已暂停接收 · 地图保留" : panel.client.connected ? "源数据已过期 · 保留最后地图" : "连接中断 · 保留最后地图"
            color: "#ffcd87"; font.pixelSize: 12
        }
    }

    Column {
        anchors.centerIn: cloud
        width: Math.min(panel.width - 70, 460)
        spacing: 12
        visible: !panel.hasPoints
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: panel.client.demo ? "正在生成演示场景" : "等待机载点云"; color: "#cfedf1"; font.pixelSize: 23; font.bold: true }
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            text: panel.client.connected ? "已连接地图服务，等待雷达点云与定位数据。" : "连接机载地图服务后，环境点云与飞行轨迹将在这里逐步展开。"
            color: "#819fad"; font.pixelSize: 13; lineHeight: 1.35
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter; spacing: 10
            GButton { width: 116; height: 34; pixelSize: 12; text: "配置数据连接"; fill: "#116282"; onClicked: panel.connectSource() }
            GButton { objectName: "mappingDemoButton"; width: 102; height: 34; pixelSize: 12; text: "预览演示"; visible: !panel.client.demo; onClicked: demoDialog.open() }
        }
    }

    Rectangle {
        anchors.right: parent.right; anchors.rightMargin: 12
        anchors.bottom: footer.top; anchors.bottomMargin: 10
        width: demoControls.width + 16; height: 29; radius: 4
        visible: panel.client.demo
        color: "#654922"
        Row {
            id: demoControls; spacing: 12; anchors.centerIn: parent
            Text { text: "合成演示"; color: "#ffdba3"; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
            GButton { objectName: "mappingStopDemoButton"; width: 72; height: 23; pixelSize: 11; text: "退出演示"; fill: "#835c29"; hoverFill: "#a57535"; onClicked: { panel.client.stopDemo(); panel.client.enabled = true; panel.client.reconnect() } }
        }
    }

    Text {
        anchors.left: parent.left; anchors.leftMargin: 13
        anchors.bottom: footer.top; anchors.bottomMargin: 10
        text: "左键旋转  ·  右键平移  ·  滚轮缩放  ·  双击全图"
        color: "#6c8c9a"; font.pixelSize: 10
        visible: panel.hasPoints
    }

    Rectangle {
        id: footer
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 49; color: "#091f2f"
        Row {
            x: 12; y: 8; spacing: 19
            Text { text: "坐标 " + (panel.client.frameId || "—"); color: "#abc7d2"; font.pixelSize: 11 }
            Text { objectName: "mappingPointCount"; text: "点 " + panel.client.pointCount.toLocaleString(Qt.locale(), "f", 0); color: "#5ce0cb"; font.pixelSize: 11 }
            Text { text: "轨迹 " + panel.client.trajectoryCount.toLocaleString(Qt.locale(), "f", 0); color: "#abc7d2"; font.pixelSize: 11 }
            Text { text: panel.client.hasPose ? "位置 " + panel.client.positionX.toFixed(2) + ", " + panel.client.positionY.toFixed(2) + ", " + panel.client.positionZ.toFixed(2) + " m" : "位置 —"; color: "#abc7d2"; font.pixelSize: 11 }
        }
        Text {
            anchors.left: parent.left; anchors.leftMargin: 12; anchors.right: parent.right; anchors.rightMargin: 12; y: 29
            text: panel.notice.length ? panel.notice : panel.client.status
            color: panel.notice.length ? "#8ee8d4" : "#789bab"; font.pixelSize: 10; elide: Text.ElideRight
        }
    }

    Dialog {
        id: connectionDialog
        objectName: "mappingConnectionDialog"
        parent: Overlay.overlay
        width: 440; height: 295
        x: (parent.width - width) / 2; y: (parent.height - height) / 2
        modal: true; focus: true
        background: Rectangle { color: "#0a2234"; border.color: "#235a75"; radius: 7 }
        contentItem: Column {
            spacing: 13
            Text { text: "地图数据连接"; color: "#e1f3f5"; font.pixelSize: 18; font.bold: true }
            Text { width: parent.width; text: "填写地图服务地址；使用 SSH 隧道时保持 127.0.0.1。"; wrapMode: Text.WordWrap; color: "#9dbac8"; font.pixelSize: 12 }
            Row { spacing: 10; Text { width: 64; anchors.verticalCenter: parent.verticalCenter; text: "主机地址"; color: "#cfe0ef"; font.pixelSize: 13 } GField { id: endpointHost; objectName: "mappingHostInput"; width: 318; placeholder: "127.0.0.1" } }
            Row { spacing: 10; Text { width: 64; anchors.verticalCenter: parent.verticalCenter; text: "端口"; color: "#cfe0ef"; font.pixelSize: 13 } GField { id: endpointPort; objectName: "mappingPortInput"; width: 120; placeholder: "37262" } }
            Text { id: endpointError; color: "#ffb391"; height: 18; font.pixelSize: 12 }
            Row {
                spacing: 10
                GButton {
                    objectName: "mappingApplyConnection"; width: 112; height: 35; text: "连接"; fill: "#116282"
                    onClicked: {
                        var host = endpointHost.text.trim()
                        var portText = endpointPort.text.trim()
                        var port = Number(portText)
                        if (!host.length || !/^\d+$/.test(portText) || port < 1 || port > 65535) { endpointError.text = "请输入有效主机与 1–65535 的端口。"; return }
                        if (panel.client.demo) panel.client.stopDemo()
                        panel.client.host = host
                        panel.client.port = port
                        panel.client.enabled = true
                        panel.client.reconnect()
                        connectionDialog.close()
                    }
                }
                GButton { width: 80; height: 35; text: "取消"; onClicked: connectionDialog.close() }
                GButton { width: 92; height: 35; text: "演示预览"; onClicked: { connectionDialog.close(); demoDialog.open() } }
            }
        }
    }

    Dialog {
        id: clearDialog
        parent: Overlay.overlay; width: 410; height: 192
        x: (parent.width - width) / 2; y: (parent.height - height) / 2
        modal: true; focus: true
        background: Rectangle { color: "#0a2234"; border.color: "#235a75"; radius: 7 }
        contentItem: Column {
            spacing: 17
            Text { text: "清空地面站地图？"; color: "#e1f3f5"; font.pixelSize: 18; font.bold: true }
            Text { width: parent.width; text: "清空当前缓存的点云与轨迹。后续收到的扫描将重新累积。"; color: "#9dbac8"; font.pixelSize: 13; wrapMode: Text.WordWrap }
            Row { spacing: 10; GButton { objectName: "mappingConfirmClear"; width: 110; height: 34; text: "清空地图"; fill: "#885130"; onClicked: { panel.client.clearMap(); clearDialog.close() } } GButton { width: 90; height: 34; text: "取消"; onClicked: clearDialog.close() } }
        }
    }

    Dialog {
        id: demoDialog
        parent: Overlay.overlay; width: 430; height: 212
        x: (parent.width - width) / 2; y: (parent.height - height) / 2
        modal: true; focus: true
        background: Rectangle { color: "#0a2234"; border.color: "#6a5432"; radius: 7 }
        contentItem: Column {
            spacing: 17
            Text { text: "预览三维建图效果"; color: "#ffdb9e"; font.pixelSize: 18; font.bold: true }
            Text { width: parent.width; text: "演示使用合成场景，会替换当前显示地图。退出后可重新连接机载服务。"; color: "#b7c6cd"; font.pixelSize: 13; wrapMode: Text.WordWrap }
            Row { spacing: 10; GButton { objectName: "mappingConfirmDemo"; width: 124; height: 34; text: "开始合成演示"; fill: "#835c29"; onClicked: { panel.client.startDemo(); cloud.resetView(); demoDialog.close() } } GButton { width: 90; height: 34; text: "取消"; onClicked: demoDialog.close() } }
        }
    }

    Dialog {
        id: exportDialog
        parent: Overlay.overlay; width: 550; height: 225
        x: (parent.width - width) / 2; y: (parent.height - height) / 2
        modal: true; focus: true
        background: Rectangle { color: "#0a2234"; border.color: "#235a75"; radius: 7 }
        contentItem: Column {
            spacing: 15
            Text { text: "导出当前地图"; color: "#e1f3f5"; font.pixelSize: 18; font.bold: true }
            Text { width: parent.width; text: "将当前缓存点云保存为本机 PCD 文件，可在点云软件中打开。"; color: "#9dbac8"; font.pixelSize: 12; wrapMode: Text.WordWrap }
            GField { id: exportPath; objectName: "mappingExportPath"; width: parent.width; placeholder: "输入本机文件绝对路径" }
            Row { spacing: 10; GButton { objectName: "mappingConfirmExport"; width: 112; height: 34; text: "保存 PCD"; fill: "#116282"; onClicked: { panel.client.savePcd(exportPath.text.trim()); exportDialog.close() } } GButton { width: 90; height: 34; text: "取消"; onClicked: exportDialog.close() } }
        }
    }
}
