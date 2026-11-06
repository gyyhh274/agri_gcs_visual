import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root
    objectName: "indoorMapPanel"
    color: "#071a29"; radius: 6; border.color: "#17415b"; clip: true
    property var waypointModel: null
    property int selectedIndex: -1
    property int routeRevision: 0
    property bool editingEnabled: true
    property bool wholeRouteEditable: true
    property int lockedPrefixCount: 0
    property int completedCount: 0
    property int activeIndex: -1
    property bool adding: false
    property real pixelsPerMeter: 30
    property real centerX: 0
    property real centerY: 0
    property real defaultZ: 1.0
    readonly property string mapSession: groundLink.mission.map_session_id || ""
    readonly property bool calibrationAllowed: groundLink.connected && groundLink.odomFresh &&
        !groundLink.armed && !groundLink.busy && mapSession.length > 0 &&
        ["IDLE", "READY", "COMPLETED", "FAILED", "LANDING"].indexOf(groundLink.mission.state) >= 0
    signal waypointAdded(real xMeters, real yMeters, real zMeters)
    signal waypointMoved(int index, real xMeters, real yMeters)
    signal waypointSelected(int index)
    signal waypointDoubleClicked(int index)
    signal clearRequested()
    signal outdoorRequested()

    onDefaultZChanged: {
        if (defaultZInput && parseFloat(defaultZInput.text) !== root.defaultZ) {
            defaultZInput.text = root.defaultZ.toString()
        }
    }

    function displayPointFor(x, y) { return Qt.point(width / 2 + (x - centerX) * pixelsPerMeter, height / 2 - (y - centerY) * pixelsPerMeter) }
    function displayCoordinateAt(x, y) { return Qt.point(centerX + (x - width / 2) / pixelsPerMeter, centerY - (y - height / 2) / pixelsPerMeter) }
    function pointFor(x, y) {
        var display = IndoorFrame.toDisplay(x, y)
        return displayPointFor(display.x, display.y)
    }
    function coordinateAt(x, y) {
        var display = displayCoordinateAt(x, y)
        return IndoorFrame.toWorld(display.x, display.y)
    }
    onMapSessionChanged: IndoorFrame.bindSession(mapSession)
    Component.onCompleted: IndoorFrame.bindSession(mapSession)
    function hitWaypoint(x, y) {
        if (!waypointModel) return -1
        for (var i = waypointModel.count - 1; i >= 0; --i) {
            var row = waypointModel.get(i), point = pointFor(row.x, row.y)
            if (Math.pow(point.x - x, 2) + Math.pow(point.y - y, 2) < 18 * 18) return i
        }
        return -1
    }
    function zoomAt(factor, x, y) {
        var before = displayCoordinateAt(x, y)
        pixelsPerMeter = Math.max(8, Math.min(160, pixelsPerMeter * factor))
        var after = displayCoordinateAt(x, y)
        centerX += before.x - after.x; centerY += before.y - after.y
    }
    onPixelsPerMeterChanged: grid.requestPaint()
    onCenterXChanged: grid.requestPaint()
    onCenterYChanged: grid.requestPaint()
    onSelectedIndexChanged: grid.requestPaint()
    onRouteRevisionChanged: grid.requestPaint()
    onCompletedCountChanged: grid.requestPaint()
    onActiveIndexChanged: grid.requestPaint()
    onWidthChanged: grid.requestPaint()
    onHeightChanged: grid.requestPaint()
    Connections { target: groundLink; function onChanged() { IndoorFrame.bindSession(root.mapSession); grid.requestPaint() } }
    Connections {
        target: IndoorFrame
        function onCalibrationChanged() { root.centerX = 0; root.centerY = 0; grid.requestPaint() }
    }

    Canvas {
        id: grid; anchors.fill: parent
        onPaint: {
            var c = getContext("2d")
            c.clearRect(0, 0, width, height)
            var origin = root.displayPointFor(0, 0), spacing = root.pixelsPerMeter
            var step = spacing < 16 ? 2 : 1
            c.strokeStyle = "#163245"; c.lineWidth = 1
            var left = root.displayCoordinateAt(0, 0).x, right = root.displayCoordinateAt(width, 0).x
            var bottom = root.displayCoordinateAt(0, height).y, top = root.displayCoordinateAt(0, 0).y
            c.font = "11px sans-serif"; c.fillStyle = "#668ba2"
            for (var x = Math.ceil(left / step) * step; x <= right; x += step) {
                var px = root.displayPointFor(x, 0).x
                c.beginPath(); c.moveTo(px, 0); c.lineTo(px, height); c.stroke()
                if (px > 35 && px < width - 35) c.fillText(x.toFixed(0), px + 3, Math.max(88, Math.min(height - 45, origin.y + 14)))
            }
            for (var y = Math.ceil(bottom / step) * step; y <= top; y += step) {
                var py = root.displayPointFor(0, y).y
                c.beginPath(); c.moveTo(0, py); c.lineTo(width, py); c.stroke()
                if (py > 100 && py < height - 50) c.fillText(y.toFixed(0), Math.max(8, Math.min(width - 35, origin.x + 5)), py - 4)
            }
            c.strokeStyle = "#4f8198"; c.lineWidth = 1.5
            c.beginPath(); c.moveTo(0, origin.y); c.lineTo(width, origin.y); c.moveTo(origin.x, 0); c.lineTo(origin.x, height); c.stroke()
            c.fillStyle = "#b3d2e4"; c.fillText("O (0,0)", origin.x + 7, origin.y - 8)
            c.fillText(IndoorFrame.forwardAxes ? "+X 右 →" : "+X →", width - 75, height - 49)
            c.fillText(IndoorFrame.forwardAxes ? "+Y 前 ↑" : "+Y ↑", 12, 96)
            if (root.waypointModel) {
                c.strokeStyle = "#ffab45"; c.lineWidth = 2; c.beginPath()
                for (var i = 0; i < root.waypointModel.count; ++i) {
                    var row = root.waypointModel.get(i), point = root.pointFor(row.x, row.y)
                    if (i === 0) c.moveTo(point.x, point.y); else c.lineTo(point.x, point.y)
                }
                c.stroke()
                for (i = 0; i < root.waypointModel.count; ++i) {
                    row = root.waypointModel.get(i); point = root.pointFor(row.x, row.y)
                    c.beginPath(); c.arc(point.x, point.y, i === root.selectedIndex ? 12 : 9, 0, Math.PI * 2)
                    c.fillStyle = i === root.selectedIndex ? "#ffe087" : i < root.completedCount ? "#6f8c9b" : i === root.activeIndex ? "#54d8ed" : "#ffab45"; c.fill()
                    c.fillStyle = "#081a29"; c.font = "bold 12px sans-serif"; c.fillText(String(i + 1), point.x - 4, point.y + 4)
                    c.font = "11px sans-serif"; c.fillStyle = "#d7e9f3"; c.fillText("Z " + Number(row.z).toFixed(1) + "m", point.x + 14, point.y - 10)
                }
            }
            if (groundLink.connected && groundLink.odomFresh) {
                var drone = root.pointFor(groundLink.worldX, groundLink.worldY)
                c.beginPath(); c.arc(drone.x, drone.y, 9, 0, Math.PI * 2); c.fillStyle = "#00e3ba"; c.fill()
                c.strokeStyle = "white"; c.lineWidth = 2; c.stroke()
                c.fillStyle = "#a1ffe9"; c.fillText("无人机 Z " + groundLink.worldZ.toFixed(2) + "m", drone.x + 13, drone.y + 17)
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        property real downX: 0; property real downY: 0
        property real initialCenterX: 0; property real initialCenterY: 0
        property int dragIndex: -1
        property bool moved: false
        onPressed: {
            downX = mouse.x; downY = mouse.y; moved = false
            initialCenterX = root.centerX; initialCenterY = root.centerY
            dragIndex = root.hitWaypoint(mouse.x, mouse.y)
            if (dragIndex >= 0) root.waypointSelected(dragIndex)
        }
        onPositionChanged: {
            if (!pressed) return
            if (Math.abs(mouse.x - downX) + Math.abs(mouse.y - downY) > 5) moved = true
            if (!moved) return
            if (dragIndex >= 0) {
                if (root.editingEnabled && dragIndex >= root.lockedPrefixCount) {
                    var at = root.coordinateAt(mouse.x, mouse.y)
                    root.waypointMoved(dragIndex, at.x, at.y)
                }
            } else {
                root.centerX = initialCenterX - (mouse.x - downX) / root.pixelsPerMeter
                root.centerY = initialCenterY + (mouse.y - downY) / root.pixelsPerMeter
            }
        }
        onDoubleClicked: {
            var hit = root.hitWaypoint(mouse.x, mouse.y)
            if (root.editingEnabled && hit >= root.lockedPrefixCount && hit >= 0) root.waypointDoubleClicked(hit)
        }
        onReleased: {
            if (!moved && dragIndex < 0 && root.adding && root.editingEnabled) {
                var at = root.coordinateAt(mouse.x, mouse.y)
                root.waypointAdded(at.x, at.y, root.defaultZ)
            }
            dragIndex = -1
        }
        onWheel: root.zoomAt(wheel.angleDelta.y > 0 ? 1.15 : 1 / 1.15, wheel.x, wheel.y)
    }
    Rectangle {
        x: 8; y: 8; width: parent.width - 16; height: 76; color: "#ec071a29"; radius: 5
        Column {
            anchors.fill: parent; anchors.margins: 7; spacing: 5
            Row { spacing: 8
                Text { anchors.verticalCenter: parent.verticalCenter; text: IndoorFrame.forwardAxes ? "室内局部地图 · 前方 Y+ · 米" : "室内局部地图 · world · 米"; color: "#ccebf5"; font.pixelSize: 14; font.bold: true }
                Text { anchors.verticalCenter: parent.verticalCenter; text: "网格用于选点，未显示障碍物"; color: "#9cb8c8"; font.pixelSize: 12 }
                GButton { objectName: "indoorCalibrateButton"; width: 84; height: 25; text: "标定前方 Y+"; onClicked: calibrationDialog.open() }
            }
            Row { spacing: 6
                GButton { objectName: "indoorAddButton"; width: 88; height: 29; text: root.adding ? "结束添加" : "添加航点"; enabled: root.editingEnabled; onClicked: root.adding = !root.adding }
                Row {
                    spacing: 4
                    anchors.verticalCenter: parent.verticalCenter
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "默认Z:"; color: "#ccebf5"; font.pixelSize: 12 }
                    GField {
                        id: defaultZInput
                        width: 52; height: 29
                        text: root.defaultZ.toString()
                        onTextChanged: {
                            var val = parseFloat(text)
                            if (!isNaN(val) && val >= 0 && val <= 100) {
                                if (root.defaultZ !== val) root.defaultZ = val
                            }
                        }
                    }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "m"; color: "#9cb8c8"; font.pixelSize: 12 }
                }
                GButton { width: 45; height: 29; text: "+"; onClicked: root.zoomAt(1.3, root.width / 2, root.height / 2) }
                GButton { width: 45; height: 29; text: "−"; onClicked: root.zoomAt(1 / 1.3, root.width / 2, root.height / 2) }
                GButton { width: 80; height: 29; text: "定位原点"; onClicked: { root.centerX = 0; root.centerY = 0 } }
                GButton { width: 90; height: 29; text: "定位无人机"; enabled: groundLink.connected && groundLink.odomFresh; onClicked: { var display = IndoorFrame.toDisplay(groundLink.worldX, groundLink.worldY); root.centerX = display.x; root.centerY = display.y } }
                GButton { width: 80; height: 29; text: "清空航线"; enabled: root.wholeRouteEditable; onClicked: root.clearRequested() }
                GButton { width: 100; height: 29; text: "切到室外地图"; onClicked: root.outdoorRequested() }
            }
        }
    }
    Rectangle {
        anchors.bottom: parent.bottom; width: parent.width; height: 38; color: "#ed071a29"
        Text {
            anchors.fill: parent; anchors.margins: 8; color: "#a5c3d4"; font.pixelSize: 11; elide: Text.ElideRight
            text: (root.pixelsPerMeter < 16 ? "每格 2 m" : "每格 1 m") + " · 拖动平移 / 滚轮缩放 · " +
                  (IndoorFrame.forwardAxes ? "前方 Y+，右侧 X+；旋转 " + IndoorFrame.rotationDeg.toFixed(1) + "°，上传使用 world" : "X/Y 随本次定位坐标轴；可标定前方 Y+") + " · " +
                  (groundLink.connected && groundLink.odomFresh ? "实时位置有效" : "等待机载局部定位")
        }
    }
    Dialog {
        id: calibrationDialog
        objectName: "indoorCalibrationDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 540; height: 460
        title: "标定实际前方为 Y+"
        modal: true
        background: Rectangle { color: "#0a2234"; border.color: "#1b5279"; radius: 6 }
        header: Text { text: calibrationDialog.title; color: "#dce9f7"; font.pixelSize: 17; font.bold: true; padding: 12 }
        footer: Item {
            implicitHeight: 46
            GButton { anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; width: 84; height: 32; text: "关闭"; onClicked: calibrationDialog.close() }
        }
        onOpened: rotationInput.text = IndoorFrame.rotationDeg.toFixed(1)
        contentItem: Column {
            spacing: 12
            Text {
                width: parent.width; wrapMode: Text.WordWrap; color: "#d8e9f5"; font.pixelSize: 14
                text: "保持未解锁，记录起点后，沿实际前方直走至少 0.5 m，再完成标定。地图原点保持不变，已有航点的实际位置保持不变。"
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap; color: root.calibrationAllowed ? "#9cb8c8" : "#ffbe67"; font.pixelSize: 13
                text: root.calibrationAllowed ? (IndoorFrame.calibrationPending ? "已记录起点，向前移动后点击“设为 Y+”。" : "可使用两点标定，也可按已知方向输入地图旋转角。") : "需要有效的机载定位，且未解锁、任务未执行，才能修改标定。"
            }
            Row {
                spacing: 10
                GButton { objectName: "indoorCalibrationStart"; width: 136; height: 34; text: "1. 记录当前位置"; enabled: root.calibrationAllowed; onClicked: IndoorFrame.beginCalibration(groundLink.worldX, groundLink.worldY, root.mapSession) }
                GButton { objectName: "indoorCalibrationFinish"; width: 136; height: 34; text: "2. 设为 Y+"; enabled: root.calibrationAllowed && IndoorFrame.calibrationPending; onClicked: { if (IndoorFrame.finishCalibration(groundLink.worldX, groundLink.worldY, root.mapSession)) rotationInput.text = IndoorFrame.rotationDeg.toFixed(1) } }
            }
            Row {
                spacing: 8
                Text { anchors.verticalCenter: parent.verticalCenter; text: "旋转角 °"; color: "#ccebf5"; font.pixelSize: 13 }
                GField { id: rotationInput; objectName: "indoorRotationInput"; width: 78; height: 32; text: "0" }
                GButton { width: 78; height: 32; text: "应用角度"; enabled: root.calibrationAllowed && rotationInput.text.trim().length > 0 && isFinite(Number(rotationInput.text)); onClicked: IndoorFrame.setRotation(Number(rotationInput.text), root.mapSession) }
                GButton { width: 150; height: 32; objectName: "indoorSiteAxesButton"; text: "现场前 Y+ / 右 X+"; enabled: root.calibrationAllowed; onClicked: { IndoorFrame.setRotation(90, root.mapSession); rotationInput.text = "90" } }
            }
            Text { width: parent.width; wrapMode: Text.WordWrap; text: "现场默认 +90°：原 X+ 显示为前方 Y+，原 Y− 显示为右侧 X+。方向设置会保存；新定位会话仍需核对原点和航线。"; color: "#9cb8c8"; font.pixelSize: 12 }
            Text { width: parent.width; wrapMode: Text.WordWrap; text: IndoorFrame.error; color: "#ffbe67"; font.pixelSize: 13; visible: text.length > 0 }
            GButton { width: 144; height: 32; text: "恢复 world 坐标"; enabled: root.calibrationAllowed; onClicked: { IndoorFrame.resetRotation(root.mapSession); rotationInput.text = "0" } }
        }
    }
}
