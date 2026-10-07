import QtQuick 2.15
import QtQuick.Controls 2.15
import QtPositioning 5.15
import Agri.Map 1.0

Rectangle {
    id: root
    color: "#071a29"
    radius: 6
    border.color: "#12324a"
    clip: true

    property var waypointModel: null
    property int selectedIndex: -1
    property int editMode: 0
    property bool editable: false
    property int routeRevision: 0
    readonly property alias mapItem: tileMap
    property bool nestDetailsVisible: false
    readonly property point nestPoint: {
        var revision = tileMap.viewportRevision
        return pointFor(nestPosition.latitude, nestPosition.longitude)
    }
    readonly property bool nestOnScreen: tileMap.mapReady && nestPoint.x >= 0 && nestPoint.x <= width && nestPoint.y >= 0 && nestPoint.y <= height
    signal waypointAdded(real latitude, real longitude)
    signal waypointMoved(int index, real latitude, real longitude)
    signal waypointSelected(int index)
    signal clearRequested()

    function coordinateAt(x, y) { return tileMap.toCoordinate(Qt.point(x, y), false) }
    function pointFor(lat, lon) { return tileMap.fromCoordinate(QtPositioning.coordinate(Number(lat), Number(lon)), false) }
    function hitWaypoint(x, y) {
        if (!waypointModel) return -1
        for (var i = waypointModel.count - 1; i >= 0; --i) {
            var row = waypointModel.get(i), p = pointFor(row.lat, row.lon)
            if (Math.pow(p.x - x, 2) + Math.pow(p.y - y, 2) < 22 * 22) return i
        }
        return -1
    }
    function addAt(x, y) {
        if (!editable || !tileMap.mapReady) return false
        var c = coordinateAt(x, y)
        waypointAdded(c.latitude, c.longitude)
        return true
    }
    function fitRoute() {
        if (!waypointModel || !waypointModel.count) return
        var coords = []
        for (var i = 0; i < waypointModel.count; ++i) {
            var p = waypointModel.get(i)
            coords.push(QtPositioning.coordinate(Number(p.lat), Number(p.lon)))
        }
        tileMap.fitCoordinates(coords)
    }
    function fitCoverage() {
        if (offlineMapSource.available)
            tileMap.fitBounds(offlineMapSource.north, offlineMapSource.south, offlineMapSource.west, offlineMapSource.east)
    }
    function openDirectory() {
        directoryField.text = offlineMapSource.directory
        schemeBox.currentIndex = offlineMapSource.tms ? 1 : 0
        sourceDialog.open()
    }
    function locateNest() {
        if (!tileMap.mapReady) return
        tileMap.center = QtPositioning.coordinate(nestPosition.latitude, nestPosition.longitude)
        nestDetailsVisible = true
    }
    function proposeNest(x, y) {
        if (!tileMap.mapReady) return false
        var c = coordinateAt(x, y)
        nestLatitudeField.text = c.latitude.toFixed(7)
        nestLongitudeField.text = c.longitude.toFixed(7)
        nestNameField.text = nestPosition.configured ? nestPosition.name : "机巢 01"
        nestError.text = ""
        editMode = 0
        nestDialog.open()
        return true
    }
    function saveNestPosition() {
        if (nestPosition.configure(nestLatitudeField.text, nestLongitudeField.text, nestNameField.text)) {
            nestDialog.accept(); nestDetailsVisible = true
        } else nestError.text = "经纬度不能为空，必须为有限数字且在标注范围内。原机巢位置未修改。"
    }
    onRouteRevisionChanged: routeCanvas.requestPaint()
    onSelectedIndexChanged: routeCanvas.requestPaint()
    Component.onCompleted: coverageTimer.restart()
    Connections {
        target: offlineMapSource
        function onChanged() {
            if (offlineMapSource.available) { tileMap.reload(); coverageTimer.restart() }
        }
    }
    Timer {
        id: coverageTimer; interval: 100
        onTriggered: if (offlineMapSource.available) {
            tileMap.center = QtPositioning.coordinate(offlineMapSource.centerLatitude, offlineMapSource.centerLongitude)
            tileMap.zoomLevel = Math.max(tileMap.minimumZoomLevel, Math.min(16, tileMap.maximumZoomLevel))
        }
    }
    AsyncTileMap {
        id: tileMap
        objectName: "offlineTileMap"
        anchors.fill: parent
        rootDirectory: offlineMapSource.available ? offlineMapSource.directory : ""
        tmsScheme: offlineMapSource.tms
        minimumZoomLevel: offlineMapSource.available ? offlineMapSource.minimumZoom : 0
        maximumZoomLevel: offlineMapSource.available ? offlineMapSource.maximumZoom : 19
        center: QtPositioning.coordinate(29.13678, 119.63764)
        zoomLevel: 16
        onViewportRevisionChanged: routeCanvas.requestPaint()
    }
    Canvas {
        id: routeCanvas
        anchors.fill: parent
        visible: tileMap.mapReady
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!root.waypointModel) return
            ctx.lineWidth = 3; ctx.strokeStyle = "#ffcc37"; ctx.beginPath()
            for (var i = 0; i < root.waypointModel.count; ++i) {
                var row = root.waypointModel.get(i), p = root.pointFor(row.lat, row.lon)
                if (i === 0) ctx.moveTo(p.x, p.y); else ctx.lineTo(p.x, p.y)
            }
            ctx.stroke()
            for (i = 0; i < root.waypointModel.count; ++i) {
                row = root.waypointModel.get(i); p = root.pointFor(row.lat, row.lon)
                if (p.x < -24 || p.y < -24 || p.x > width + 24 || p.y > height + 24) continue
                ctx.beginPath(); ctx.arc(p.x, p.y, i === root.selectedIndex ? 17 : 14, 0, 2 * Math.PI)
                ctx.fillStyle = i === root.selectedIndex ? "#087dff" : "#ffcc37"; ctx.fill()
                ctx.strokeStyle = "#ffffff"; ctx.lineWidth = 2; ctx.stroke()
                ctx.fillStyle = i === root.selectedIndex ? "#ffffff" : "#152538"
                ctx.font = "bold 12px sans-serif"; ctx.textAlign = "center"; ctx.textBaseline = "middle"
                ctx.fillText(String(i + 1), p.x, p.y)
            }
        }
    }
    PinchArea {
        id: pinchArea
        anchors.fill: parent
        enabled: tileMap.mapReady
        property real lastScale: 1
        property point lastCenter
        property bool suppressClick: false
        onPinchStarted: { lastScale = 1; lastCenter = pinch.center; suppressClick = true }
        onPinchUpdated: {
            tileMap.zoomBy(Math.log(pinch.scale / lastScale) / Math.LN2, pinch.center.x, pinch.center.y)
            tileMap.panBy(pinch.center.x - lastCenter.x, pinch.center.y - lastCenter.y)
            lastScale = pinch.scale; lastCenter = pinch.center
        }
        onPinchFinished: pinchReset.restart()
        Timer { id: pinchReset; interval: 200; onTriggered: pinchArea.suppressClick = false }
        MouseArea {
            id: mouseArea
            objectName: "mapMouseArea"
            anchors.fill: parent
            enabled: !pinchArea.pinch.active
            preventStealing: false
            property point startPoint
            property point lastPoint
            property bool dragged: false
            property int dragIndex: -1
            cursorShape: root.editMode !== 0 ? Qt.CrossCursor : pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            onPressed: {
                startPoint = Qt.point(mouse.x, mouse.y); lastPoint = startPoint; dragged = false
                dragIndex = root.editable && root.editMode !== 2 ? root.hitWaypoint(mouse.x, mouse.y) : -1
                if (dragIndex >= 0) root.waypointSelected(dragIndex)
            }
            onPositionChanged: {
                if (!pressed || pinchArea.suppressClick) return
                if (Math.abs(mouse.x - startPoint.x) + Math.abs(mouse.y - startPoint.y) > 6) dragged = true
                if (dragged) {
                    if (dragIndex >= 0) {
                        var c = root.coordinateAt(mouse.x, mouse.y)
                        root.waypointMoved(dragIndex, c.latitude, c.longitude)
                    } else tileMap.panBy(mouse.x - lastPoint.x, mouse.y - lastPoint.y)
                }
                lastPoint = Qt.point(mouse.x, mouse.y)
            }
            onReleased: {
                if (!dragged && !pinchArea.suppressClick && root.editMode === 1 && dragIndex < 0)
                    root.addAt(mouse.x, mouse.y)
                else if (!dragged && !pinchArea.suppressClick && root.editMode === 2)
                    root.proposeNest(mouse.x, mouse.y)
                dragIndex = -1
            }
            onCanceled: dragIndex = -1
            onWheel: { tileMap.zoomBy(wheel.angleDelta.y / 120 * 0.5, wheel.x, wheel.y); wheel.accepted = true }
        }
    }
    Item {
        id: nestMarker
        objectName: "nestMapMarker"
        x: root.nestPoint.x - width / 2; y: root.nestPoint.y - height / 2
        width: 38; height: 38
        visible: root.nestOnScreen
        Rectangle {
            anchors.fill: parent; radius: 8; color: "#072f35"; border.color: "#00e7be"; border.width: 2
            Canvas {
                anchors.fill: parent
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0,0,width,height); ctx.strokeStyle = "#00e7be"; ctx.lineWidth = 2
                    ctx.beginPath(); ctx.moveTo(8,17); ctx.lineTo(19,8); ctx.lineTo(30,17); ctx.stroke()
                    ctx.strokeRect(11,17,16,13); ctx.strokeRect(17,23,5,7)
                }
            }
        }
        Rectangle {
            x: (parent.width - width) / 2; y: parent.height + 5
            width: Math.min(190, nestLabel.implicitWidth + 16); height: 24; radius: 4; color: "#e5072f35"; border.color: "#00ad93"
            Text { id: nestLabel; anchors.fill: parent; anchors.margins: 4; text: nestPosition.name + (nestPosition.configured ? "（手动）" : "（非真实位置）"); textFormat: Text.PlainText; elide: Text.ElideRight; color: "#ccfff5"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter }
        }
        MouseArea {
            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: root.nestDetailsVisible = !root.nestDetailsVisible
            onWheel: { tileMap.zoomBy(wheel.angleDelta.y / 120 * 0.5, root.nestPoint.x, root.nestPoint.y); wheel.accepted = true }
        }
    }
    Rectangle {
        objectName: "nestMapDetails"
        visible: root.nestDetailsVisible && tileMap.mapReady
        x: 10; y: 54; width: Math.min(340, root.width - 20); height: 76; radius: 5
        color: "#ed072f35"; border.color: "#00ad93"
        Column {
            anchors.fill: parent; anchors.margins: 9; spacing: 4
            Text { width: parent.width; elide: Text.ElideRight; textFormat: Text.PlainText; text: nestPosition.name + (nestPosition.configured ? " · 手动配置，非实时定位" : " · 地图中心示例，非真实机巢"); color: "#ccfff5"; font.pixelSize: 12 }
            Text { text: "WGS84 纬度 " + nestPosition.latitude.toFixed(7) + " / 经度 " + nestPosition.longitude.toFixed(7); color: "#d1e5e2"; font.pixelSize: 11 }
            Text { text: "未接入机巢定位或在线状态；仅显示本地位置"; color: "#8db3ae"; font.pixelSize: 11 }
        }
        MouseArea { anchors.fill: parent; onClicked: root.nestDetailsVisible = false }
    }
    Row {
        x: 10; y: 10; spacing: 6
        GButton { width: 48; height: 34; text: "+"; enabled: tileMap.mapReady; onClicked: tileMap.zoomBy(1) }
        GButton { width: 48; height: 34; text: "−"; enabled: tileMap.mapReady; onClicked: tileMap.zoomBy(-1) }
        GButton { width: 86; height: 34; text: "地图范围"; enabled: tileMap.mapReady; onClicked: root.fitCoverage() }
        GButton { width: 86; height: 34; text: "地图目录"; onClicked: root.openDirectory() }
        GButton { visible: root.editable; width: 86; height: 34; text: root.editMode === 1 ? "结束添加" : "添加航点"; fill: root.editMode === 1 ? "#087dff" : "#143950"; enabled: tileMap.mapReady; onClicked: root.editMode = root.editMode === 1 ? 0 : 1 }
        GButton { visible: root.editable; width: 86; height: 34; text: "航线范围"; enabled: tileMap.mapReady && root.waypointModel && root.waypointModel.count > 0; onClicked: root.fitRoute() }
        GButton { visible: root.editable; width: 86; height: 34; text: "清空航线"; onClicked: root.clearRequested() }
        GButton { visible: root.editable; width: 86; height: 34; text: "定位机巢"; enabled: tileMap.mapReady; onClicked: root.locateNest() }
        GButton { visible: root.editable; width: 86; height: 34; text: root.editMode === 2 ? "取消设置" : "设置机巢"; fill: root.editMode === 2 ? "#008c75" : "#143950"; enabled: tileMap.mapReady; onClicked: root.editMode = root.editMode === 2 ? 0 : 2 }
    }
    Rectangle {
        visible: !offlineMapSource.available
        anchors.centerIn: parent; width: Math.min(parent.width - 30, 580); height: 104
        color: "#ed071a29"; radius: 6; border.color: "#335872"
        Column {
            anchors.fill: parent; anchors.margins: 14; spacing: 10
            Text { width: parent.width; text: offlineMapSource.status; color: "#edf5ff"; wrapMode: Text.WordWrap; font.pixelSize: 14 }
            Text { width: parent.width; text: "仅加载本地地图，不联网。地图就绪后可缩放、拖动和添加航点。"; wrapMode: Text.WordWrap; color: "#9db7ca"; font.pixelSize: 12 }
        }
    }
    Rectangle {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 43; color: "#db071a29"
        Column {
            x: 10; y: 4; spacing: 2
            Text { width: root.width - 20; elide: Text.ElideRight; font.pixelSize: 11; color: "#c5dbea"; text: offlineMapSource.status + "  |  Z " + tileMap.zoomLevel.toFixed(1) + (tileMap.loading ? " · 加载中" : "") + "  |  缺失瓦片显示网格" }
            Text { width: root.width - 20; elide: Text.ElideRight; textFormat: Text.PlainText; color: "#9ebbcf"; font.pixelSize: 11; text: "WGS84 · " + offlineMapSource.attribution + " · 未连接飞控" + (root.editable ? " · " + (root.editMode === 2 ? "点击地图设置机巢（需确认）" : root.editMode === 1 ? "点击空白处添加；拖动航点调整" : "拖动地图/航点；滚轮或双指缩放") : "") }
        }
    }
    Dialog {
        id: nestDialog
        objectName: "nestPositionDialog"
        parent: Overlay.overlay
        title: "设置机巢位置（WGS84）"
        modal: true; width: Math.min(520, Overlay.overlay.width - 30)
        x: (Overlay.overlay.width - width) / 2; y: (Overlay.overlay.height - height) / 2
        contentItem: Column {
            spacing: 10
            Text { width: parent.width; text: "保存的是手动坐标，不是机巢实时定位。请核对实际作业地点；不会修改或新增航点。"; wrapMode: Text.WordWrap; color: "#203040" }
            TextField { id: nestNameField; objectName: "nestNameField"; width: parent.width; placeholderText: "机巢名称"; selectByMouse: true; maximumLength: 64 }
            Text { text: "纬度（−85.05112878～85.05112878）"; color: "#41576b" }
            TextField { id: nestLatitudeField; objectName: "nestLatitudeField"; width: parent.width; selectByMouse: true }
            Text { text: "经度（−180～180）"; color: "#41576b" }
            TextField { id: nestLongitudeField; objectName: "nestLongitudeField"; width: parent.width; selectByMouse: true }
            Text { id: nestError; width: parent.width; color: "#b82030"; wrapMode: Text.WordWrap }
        }
        // A DialogButtonBox footer auto-accepts the popup, even on validation
        // failure. Explicit buttons leave invalid inputs visible for correction.
        footer: Item {
            implicitHeight: 54
            Row {
                anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10; spacing: 10
                Button { objectName: "nestSaveButton"; text: "保存位置"; onClicked: root.saveNestPosition() }
                Button { text: "取消"; onClicked: nestDialog.reject() }
            }
        }
    }
    Dialog {
        id: sourceDialog
        parent: Overlay.overlay
        title: "离线地图目录"
        modal: true; width: Math.min(560, Overlay.overlay.width - 30)
        x: (Overlay.overlay.width - width) / 2; y: (Overlay.overlay.height - height) / 2
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: offlineMapSource.configure(directoryField.text, schemeBox.currentIndex === 1)
        contentItem: Column {
            spacing: 12
            Text { width: parent.width; text: "填写包含数字缩放目录的本地路径，例如 /home/orangepi/tiles。支持 z/x/y.png、jpg、jpeg、webp；坐标必须是 WGS84 Web Mercator。"; wrapMode: Text.WordWrap; color: "#203040" }
            TextField { id: directoryField; width: parent.width; selectByMouse: true; placeholderText: "/home/orangepi/tiles" }
            ComboBox { id: schemeBox; model: ["XYZ（Y 从北向南）", "TMS（Y 从南向北）"]; width: parent.width }
            Text { width: parent.width; text: "不会自动下载或请求在线地图；目录和格式会保存到本机。"; wrapMode: Text.WordWrap; color: "#41576b" }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: "#1b425c"
        radius: 6
        enabled: false
    }
}
