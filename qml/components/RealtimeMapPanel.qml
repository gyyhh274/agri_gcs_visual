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
    property var starter: typeof mappingStarter !== "undefined" ? mappingStarter : null
    readonly property bool hasPoints: client.pointCount > 0
    property string notice: ""
    property bool initialViewFitted: false
    property bool expanded: typeof initialMappingExpanded !== "undefined" && initialMappingExpanded
    signal notified(string message)
    onExpandedChanged: { if (expanded) forceActiveFocus() }
    Keys.onEscapePressed: {
        if (expanded) { expanded = false; event.accepted = true }
    }

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
        function onSnapshotChanged() {
            if (!panel.hasPoints) panel.initialViewFitted = false
            if (!panel.initialViewFitted && panel.client.pointCount >= 1000) {
                // Wait for the initial snapshot to finish rather than fitting
                // the first small batch of a large room.
                if (panel.client.stale) initialFit.restart()
                else if (!initialFit.running) initialFit.start()
            }
        }
    }
    Timer {
        id: initialFit
        interval: 500
        onTriggered: {
            if (!panel.initialViewFitted && panel.hasPoints) {
                cloud.fitMap()
                panel.initialViewFitted = true
            }
        }
    }
    Component.onCompleted: { if (panel.hasPoints) initialFit.start() }
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
        cutaway: typeof initialMappingCutaway !== "undefined" && initialMappingCutaway

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            property real lastX: 0
            property real lastY: 0
            onPressed: { panel.initialViewFitted = true; lastX = mouse.x; lastY = mouse.y }
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
            onWheel: { panel.initialViewFitted = true; cloud.zoom(wheel.angleDelta.y / 120) }
            onDoubleClicked: cloud.fitMap()
        }
    }

    Rectangle {
        id: tools
        width: parent.width
        height: 48
        color: "#0a2131"
        Flickable {
            id: toolScroll
            anchors.left: parent.left; anchors.leftMargin: 10
            anchors.right: rightActions.left; anchors.rightMargin: 10
            anchors.top: parent.top; anchors.bottom: parent.bottom
            contentWidth: toolButtons.width
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; height: 3 }
            Row {
            id: toolButtons
            y: 9; spacing: 6
            GButton { objectName: "mappingOrbitButton"; width: 48; height: 30; pixelSize: 12; text: "三维"; checked: !cloud.topDown; fill: !cloud.topDown ? "#116282" : "#163b5e"; onClicked: cloud.topDown = false }
            GButton { objectName: "mappingTopButton"; width: 48; height: 30; pixelSize: 12; text: "俯视"; checked: cloud.topDown; fill: cloud.topDown ? "#116282" : "#163b5e"; onClicked: cloud.topDown = true }
            GButton { objectName: "mappingFollowButton"; width: 48; height: 30; pixelSize: 12; text: "跟随"; checked: cloud.follow; fill: cloud.follow ? "#116282" : "#163b5e"; onClicked: { panel.initialViewFitted = true; cloud.follow = !cloud.follow } }
            GButton { objectName: "mappingFocusButton"; width: 48; height: 30; pixelSize: 12; text: "聚焦"; enabled: panel.client.hasPose; opacity: enabled ? 1 : 0.45; onClicked: { panel.initialViewFitted = true; cloud.focusVehicle() } }
            GButton { objectName: "mappingFitButton"; width: 48; height: 30; pixelSize: 12; text: "全图"; onClicked: { panel.initialViewFitted = true; cloud.follow = false; cloud.fitMap() } }
            ComboBox {
                id: colors
                objectName: "mappingColorMode"
                width: 92; height: 30
                model: ["空间蓝青", "反射强度"]
                currentIndex: 0
                font.pixelSize: 12
                onActivated: cloud.colorMode = currentIndex
                contentItem: Text { text: colors.displayText; color: "#dce9f7"; verticalAlignment: Text.AlignVCenter; leftPadding: 9; rightPadding: 14; font.pixelSize: 12 }
                background: Rectangle { color: "#163b5e"; radius: 4; border.color: "#24526e" }
            }
            GButton { objectName: "mappingCutawayButton"; width: 48; height: 30; pixelSize: 12; text: "剖视"; checked: cloud.cutaway; fill: cloud.cutaway ? "#116282" : "#163b5e"; onClicked: cloud.cutaway = !cloud.cutaway }
            GButton { objectName: "mappingGlowButton"; width: 48; height: 30; pixelSize: 12; text: "光晕"; checked: cloud.glow; fill: cloud.glow ? "#116282" : "#163b5e"; enabled: cloud.gpuActive; onClicked: cloud.glow = !cloud.glow }
            GButton {
                id: pointSizeButton
                objectName: "mappingPointSizeButton"
                width: 48; height: 30; pixelSize: 12; text: "点径"
                onClicked: pointSizeMenu.open()
                Menu {
                    id: pointSizeMenu
                    y: pointSizeButton.height + 4
                    width: 124
                    MenuItem { text: (cloud.pointSize < 1.4 ? "✓  " : "    ") + "细点"; onTriggered: cloud.pointSize = 1.2 }
                    MenuItem { text: (cloud.pointSize >= 1.4 && cloud.pointSize < 1.9 ? "✓  " : "    ") + "标准"; onTriggered: cloud.pointSize = 1.6 }
                    MenuItem { text: (cloud.pointSize >= 1.9 && cloud.pointSize < 2.6 ? "✓  " : "    ") + "饱满"; onTriggered: cloud.pointSize = 2.2 }
                    MenuItem { text: (cloud.pointSize >= 2.6 ? "✓  " : "    ") + "大点"; onTriggered: cloud.pointSize = 3.0 }
                }
            }
            GButton { objectName: "mappingDepthButton"; width: 48; height: 30; pixelSize: 12; text: "立体"; checked: cloud.depthShading; fill: cloud.depthShading ? "#116282" : "#163b5e"; enabled: cloud.gpuActive; onClicked: cloud.depthShading = !cloud.depthShading }
            GButton { objectName: "mappingPauseButton"; width: 54; height: 30; pixelSize: 12; text: panel.client.paused ? "继续" : "暂停"; fill: panel.client.paused ? "#86602c" : "#163b5e"; onClicked: panel.client.paused = !panel.client.paused }
            GButton { objectName: "mappingClearButton"; width: 48; height: 30; pixelSize: 12; text: "清空"; onClicked: clearDialog.open() }
            GButton { objectName: "mappingExportButton"; width: 48; height: 30; pixelSize: 12; text: "导出"; enabled: panel.hasPoints; opacity: enabled ? 1 : 0.45; onClicked: { exportPath.text = panel.client.defaultExportPath; exportDialog.open() } }
            GButton { objectName: "mappingConnectButton"; width: 54; height: 30; pixelSize: 12; text: "连接"; onClicked: panel.connectSource() }
            }
        }
        Row {
            id: rightActions
            anchors.right: parent.right; anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            GButton { objectName: "mappingStartButton"; width: 78; height: 30; pixelSize: 12; text: panel.starter && panel.starter.busy ? "启动中…" : "启动建图"; visible: panel.starter !== null; enabled: panel.starter && !panel.starter.busy && !panel.client.demo; onClicked: panel.starter.start() }
        Rectangle {
            width: 84; height: 30; radius: 4
            color: panel.client.demo ? "#533e20" : panel.client.connected && !panel.client.stale ? "#0d403a" : "#243442"
            Row {
                anchors.centerIn: parent; spacing: 6
                Rectangle { width: 6; height: 6; radius: 3; anchors.verticalCenter: parent.verticalCenter; color: panel.client.demo ? "#ffc875" : panel.client.connected && !panel.client.stale ? "#3ce6bb" : "#869ead" }
                Text { text: panel.client.demo ? "演示数据" : panel.client.connected ? (panel.client.stale ? (panel.hasPoints ? "数据过期" : "等待数据") : "LIVE") : "未连接"; color: panel.client.demo ? "#ffcb83" : "#cce9ec"; font.pixelSize: 11; font.bold: true }
            }
        }
        GButton { objectName: "mappingExpandButton"; width: 48; height: 30; pixelSize: 12; text: panel.expanded ? "还原" : "放大"; checked: panel.expanded; fill: panel.expanded ? "#116282" : "#163b5e"; onClicked: panel.expanded = !panel.expanded }
        }
    }

    Column {
        anchors.left: parent.left; anchors.leftMargin: 14
        anchors.top: tools.bottom; anchors.topMargin: 12
        spacing: 8
        Rectangle {
            visible: panel.hasPoints
            width: statistics.width + 22; height: statistics.height + 16
            color: "#b508141e"; radius: 4
            border.color: "#2c466170"
            Column {
                id: statistics
                x: 11; y: 8; spacing: 4
                Text { objectName: "mappingPointCount"; text: "点云  " + panel.client.pointCount.toLocaleString(Qt.locale(), "f", 0); color: "#d2f2f4"; font.pixelSize: 11; font.bold: true }
                Text { text: "轨迹  " + panel.client.trajectoryCount.toLocaleString(Qt.locale(), "f", 0); color: "#92aebc"; font.pixelSize: 11 }
            }
        }
        Text {
            visible: panel.client.demo || panel.client.paused || (panel.hasPoints && (!panel.client.connected || panel.client.stale))
            text: panel.client.demo ? "演示 · 合成场景，非机载数据" : panel.client.paused ? "已暂停接收 · 地图保留" : panel.client.connected ? "源数据已过期 · 保留最后地图" : "连接中断 · 保留最后地图"
            color: "#ffcd87"; font.pixelSize: 12
        }
    }

    Rectangle {
        anchors.right: parent.right; anchors.rightMargin: 14
        anchors.top: tools.bottom; anchors.topMargin: 12
        width: 76; height: 76; radius: 5
        visible: panel.hasPoints
        color: "#9e08141e"; border.color: "#26466170"
        Canvas {
            id: orientationCanvas
            anchors.fill: parent
            property real azimuth: cloud.yaw
            property real elevation: cloud.topDown ? 89.5 : cloud.pitch
            onAzimuthChanged: requestPaint()
            onElevationChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                var yaw = azimuth * Math.PI / 180
                var pitch = elevation * Math.PI / 180
                var axes = [
                    { x: -Math.sin(yaw), y: Math.cos(yaw) * Math.sin(pitch), depth: -Math.cos(pitch) * Math.cos(yaw), color: "#ed8079", label: "X" },
                    { x: Math.cos(yaw), y: Math.sin(yaw) * Math.sin(pitch), depth: -Math.cos(pitch) * Math.sin(yaw), color: "#9bce8d", label: "Y" },
                    { x: 0, y: -Math.cos(pitch), depth: -Math.sin(pitch), color: "#77b6ef", label: "Z" }
                ]
                axes.sort(function(a, b) { return b.depth - a.depth })
                var cx = width / 2, cy = height / 2 + 3, length = 23
                ctx.lineWidth = 1.6
                ctx.font = "10px sans-serif"
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"
                for (var i = 0; i < axes.length; ++i) {
                    var axis = axes[i]
                    var tx = cx + axis.x * length, ty = cy + axis.y * length
                    ctx.strokeStyle = axis.color
                    ctx.fillStyle = axis.color
                    ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(tx, ty); ctx.stroke()
                    ctx.beginPath(); ctx.arc(tx, ty, 2, 0, Math.PI * 2); ctx.fill()
                    if (axis.x * axis.x + axis.y * axis.y < 0.02)
                        ctx.fillText(axis.label, cx + 11, cy - 11)
                    else
                        ctx.fillText(axis.label, cx + axis.x * (length + 9), cy + axis.y * (length + 9))
                }
                ctx.fillStyle = "#bed8de"
                ctx.beginPath(); ctx.arc(cx, cy, 2.5, 0, Math.PI * 2); ctx.fill()
            }
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
        visible: panel.hasPoints && panel.width >= 620
    }

    Rectangle {
        id: footer
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 43; color: "#091f2f"
        Row {
            x: 12; y: 8; spacing: 19
            Text { text: "坐标 " + (panel.client.frameId || "—"); color: "#abc7d2"; font.pixelSize: 11 }
            Text { text: panel.client.hasPose ? "位置 " + panel.client.positionX.toFixed(2) + ", " + panel.client.positionY.toFixed(2) + ", " + panel.client.positionZ.toFixed(2) + " m" : "位置 —"; color: "#abc7d2"; font.pixelSize: 11 }
        }
        Text {
            anchors.left: parent.left; anchors.leftMargin: 12; anchors.right: parent.right; anchors.rightMargin: 12; y: 26
            text: panel.notice.length ? panel.notice
                : panel.starter && !panel.client.demo && panel.starter.status.length && (panel.client.stale || panel.starter.busy)
                  ? panel.starter.status : panel.client.status
            color: panel.notice.length ? "#8ee8d4" : "#789bab"; font.pixelSize: 10; elide: Text.ElideRight
        }
    }

    Dialog {
        id: connectionDialog
        objectName: "mappingConnectionDialog"
        parent: Overlay.overlay
        width: 440; height: 340
        x: (parent.width - width) / 2; y: (parent.height - height) / 2
        modal: true; focus: true
        background: Rectangle { color: "#0a2234"; border.color: "#235a75"; radius: 7 }
        contentItem: Column {
            spacing: 13
            Text { text: "地图数据连接"; color: "#e1f3f5"; font.pixelSize: 18; font.bold: true }
            Text { width: parent.width; text: "填写地图服务地址；使用 SSH 隧道时保持 127.0.0.1。"; wrapMode: Text.WordWrap; color: "#9dbac8"; font.pixelSize: 12 }
            Row { spacing: 10; Text { width: 64; anchors.verticalCenter: parent.verticalCenter; text: "主机地址"; color: "#cfe0ef"; font.pixelSize: 13 } GField { id: endpointHost; objectName: "mappingHostInput"; width: 318; placeholder: "127.0.0.1" } }
            Row { spacing: 10; Text { width: 64; anchors.verticalCenter: parent.verticalCenter; text: "端口"; color: "#cfe0ef"; font.pixelSize: 13 } GField { id: endpointPort; objectName: "mappingPortInput"; width: 120; placeholder: "37262" } }
            CheckBox {
                objectName: "mappingAutoStartCheck"
                visible: panel.starter !== null
                text: "打开地面站时自动启动建图"
                checked: panel.starter ? panel.starter.autoStart : false
                onClicked: if (panel.starter) panel.starter.autoStart = checked
                palette.windowText: "#cfe0ef"
            }
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
