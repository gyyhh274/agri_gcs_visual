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
    property bool adding: false
    property real pixelsPerMeter: 30
    property real centerX: 0
    property real centerY: 0
    signal waypointAdded(real xMeters, real yMeters)
    signal waypointMoved(int index, real xMeters, real yMeters)
    signal waypointSelected(int index)
    signal clearRequested()
    signal outdoorRequested()

    function pointFor(x, y) { return Qt.point(width / 2 + (x - centerX) * pixelsPerMeter, height / 2 - (y - centerY) * pixelsPerMeter) }
    function coordinateAt(x, y) { return Qt.point(centerX + (x - width / 2) / pixelsPerMeter, centerY - (y - height / 2) / pixelsPerMeter) }
    function hitWaypoint(x, y) {
        if (!waypointModel) return -1
        for (var i = waypointModel.count - 1; i >= 0; --i) {
            var row = waypointModel.get(i), point = pointFor(row.x, row.y)
            if (Math.pow(point.x - x, 2) + Math.pow(point.y - y, 2) < 18 * 18) return i
        }
        return -1
    }
    function zoomAt(factor, x, y) {
        var before = coordinateAt(x, y)
        pixelsPerMeter = Math.max(8, Math.min(160, pixelsPerMeter * factor))
        var after = coordinateAt(x, y)
        centerX += before.x - after.x; centerY += before.y - after.y
    }
    onPixelsPerMeterChanged: grid.requestPaint()
    onCenterXChanged: grid.requestPaint()
    onCenterYChanged: grid.requestPaint()
    onSelectedIndexChanged: grid.requestPaint()
    onRouteRevisionChanged: grid.requestPaint()
    onWidthChanged: grid.requestPaint()
    onHeightChanged: grid.requestPaint()
    Connections { target: groundLink; function onChanged() { grid.requestPaint() } }

    Canvas {
        id: grid; anchors.fill: parent
        onPaint: {
            var c = getContext("2d")
            c.clearRect(0, 0, width, height)
            var origin = root.pointFor(0, 0), spacing = root.pixelsPerMeter
            var step = spacing < 16 ? 2 : 1
            c.strokeStyle = "#163245"; c.lineWidth = 1
            var left = root.coordinateAt(0, 0).x, right = root.coordinateAt(width, 0).x
            var bottom = root.coordinateAt(0, height).y, top = root.coordinateAt(0, 0).y
            c.font = "11px sans-serif"; c.fillStyle = "#668ba2"
            for (var x = Math.ceil(left / step) * step; x <= right; x += step) {
                var px = root.pointFor(x, 0).x
                c.beginPath(); c.moveTo(px, 0); c.lineTo(px, height); c.stroke()
                if (px > 35 && px < width - 35) c.fillText(x.toFixed(0), px + 3, Math.max(88, Math.min(height - 45, origin.y + 14)))
            }
            for (var y = Math.ceil(bottom / step) * step; y <= top; y += step) {
                var py = root.pointFor(0, y).y
                c.beginPath(); c.moveTo(0, py); c.lineTo(width, py); c.stroke()
                if (py > 100 && py < height - 50) c.fillText(y.toFixed(0), Math.max(8, Math.min(width - 35, origin.x + 5)), py - 4)
            }
            c.strokeStyle = "#4f8198"; c.lineWidth = 1.5
            c.beginPath(); c.moveTo(0, origin.y); c.lineTo(width, origin.y); c.moveTo(origin.x, 0); c.lineTo(origin.x, height); c.stroke()
            c.fillStyle = "#b3d2e4"; c.fillText("O (0,0)", origin.x + 7, origin.y - 8)
            c.fillText("+X →", width - 65, height - 49); c.fillText("+Y ↑", 12, 96)
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
                    c.fillStyle = i === root.selectedIndex ? "#ffe087" : "#ffab45"; c.fill()
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
                if (root.editingEnabled) {
                    var at = root.coordinateAt(mouse.x, mouse.y)
                    root.waypointMoved(dragIndex, at.x, at.y)
                }
            } else {
                root.centerX = initialCenterX - (mouse.x - downX) / root.pixelsPerMeter
                root.centerY = initialCenterY + (mouse.y - downY) / root.pixelsPerMeter
            }
        }
        onReleased: {
            if (!moved && dragIndex < 0 && root.adding && root.editingEnabled) {
                var at = root.coordinateAt(mouse.x, mouse.y)
                root.waypointAdded(at.x, at.y)
            }
            dragIndex = -1
        }
        onWheel: root.zoomAt(wheel.angleDelta.y > 0 ? 1.15 : 1 / 1.15, wheel.x, wheel.y)
    }
    Rectangle {
        x: 8; y: 8; width: parent.width - 16; height: 72; color: "#ec071a29"; radius: 5
        Column {
            anchors.fill: parent; anchors.margins: 7; spacing: 5
            Row { spacing: 8
                Text { text: "室内局部地图 · world · 米"; color: "#ccebf5"; font.pixelSize: 14; font.bold: true }
                Text { text: "网格用于选点，未显示障碍物"; color: "#9cb8c8"; font.pixelSize: 12 }
            }
            Row { spacing: 6
                GButton { objectName: "indoorAddButton"; width: 88; height: 29; text: root.adding ? "结束添加" : "添加航点"; enabled: root.editingEnabled; onClicked: root.adding = !root.adding }
                GButton { width: 45; height: 29; text: "+"; onClicked: root.zoomAt(1.3, root.width / 2, root.height / 2) }
                GButton { width: 45; height: 29; text: "−"; onClicked: root.zoomAt(1 / 1.3, root.width / 2, root.height / 2) }
                GButton { width: 80; height: 29; text: "定位原点"; onClicked: { root.centerX = 0; root.centerY = 0 } }
                GButton { width: 90; height: 29; text: "定位无人机"; enabled: groundLink.connected && groundLink.odomFresh; onClicked: { root.centerX = groundLink.worldX; root.centerY = groundLink.worldY } }
                GButton { width: 80; height: 29; text: "清空航线"; enabled: root.editingEnabled; onClicked: root.clearRequested() }
                GButton { width: 100; height: 29; text: "切到室外地图"; onClicked: root.outdoorRequested() }
            }
        }
    }
    Rectangle {
        anchors.bottom: parent.bottom; width: parent.width; height: 38; color: "#ed071a29"
        Text {
            anchors.fill: parent; anchors.margins: 8; color: "#a5c3d4"; font.pixelSize: 11; elide: Text.ElideRight
            text: (root.pixelsPerMeter < 16 ? "每格 2 m" : "每格 1 m") + " · 拖动平移 / 滚轮缩放 · X/Y 随本次定位坐标轴，不代表东/北 · " +
                  (groundLink.connected && groundLink.odomFresh ? "实时位置有效" : "等待机载局部定位")
        }
    }
}
