import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt.labs.settings 1.1
import "../components"

Item {
    id: page
    property int contentTab: 0
    property string missionState: "未接入任务控制"
    property bool nestOpen: false
    property bool charging: true
    property bool thermal: false
    // 云台指令目标值（滑条用；真实角度以 gimbalLink 回报为准）
    property real pitchCmd: 0
    property real headingCmd: 0
    property real zoomCmd: 1
    property int selectedWaypoint: -1
    property int routeRevision: 0
    property int selectedVideo: 0
    property int photoPage: 0
    property int inspectedPhoto: 0
    signal requestSettings()
    Settings { id: routeStore; category: "MissionDemo" }

    // ── 云台链路 ──────────────────────────────────────────────
    // 相机只对机载电脑可达，实际连的是机载电脑上的 UDP 中转；
    // 中转与视频中转（MediaMTX）跑在同一台机器上，所以主机名从视频地址推导。
    readonly property string gimbalHost: {
        var match = /^rtsp:\/\/(?:[^@\/]*@)?([^:\/]+)/.exec(VideoConfig.url)
        return match ? match[1] : "192.168.2.113"
    }
    readonly property int gimbalPort: 37260

    function connectGimbal() {
        gimbalLink.connectToGimbal(gimbalHost, gimbalPort)
        actionMessage("云台链路：" + gimbalHost + ":" + gimbalPort)
    }

    Connections {
        target: gimbalLink
        function onNotified(text) { page.actionMessage(text) }
    }

    function routeJson() {
        var rows = []
        for (var i=0; i<waypoints.count; i++) rows.push(waypoints.get(i))
        return JSON.stringify(rows, null, 2)
    }
    function savedRouteJson() { return routeStore.value("routeJson", "") }
    function saveRoute() { routeStore.setValue("routeJson", routeJson()); routeStore.sync(); actionMessage("航线已保存到本机配置") }
    Component.onCompleted: {
        var saved = savedRouteJson()
        if (saved.length) importRoute(saved)
        // 面板就绪后自动连接云台中转（地址由视频流地址推导）
        Qt.callLater(connectGimbal)
    }
    function validCoordinate(lat, lon) {
        return isFinite(lat) && isFinite(lon) && Math.abs(lat) <= 85.05112878 && Math.abs(lon) <= 180
    }
    function addWaypoint(lat, lon) {
        if (!validCoordinate(lat, lon) || waypoints.count >= 200) { actionMessage("坐标无效或已达到 200 个航点上限"); return false }
        waypoints.append({no: String(waypoints.count + 1), lat: Number(lat).toFixed(7), lon: Number(lon).toFixed(7), alt: "120", speed: "8", action: "拍照", hover: "2", state: "待执行"})
        selectedWaypoint = waypoints.count - 1; routeRevision++
        return true
    }
    function moveWaypoint(index, lat, lon) {
        if (index < 0 || index >= waypoints.count || !validCoordinate(lat, lon)) return false
        waypoints.setProperty(index, "lat", Number(lat).toFixed(7))
        waypoints.setProperty(index, "lon", Number(lon).toFixed(7))
        selectedWaypoint = index; routeRevision++
        return true
    }
    function deleteWaypoint(index) {
        if (index < 0 || index >= waypoints.count) return
        waypoints.remove(index)
        for (var i = 0; i < waypoints.count; ++i) waypoints.setProperty(i, "no", String(i + 1))
        selectedWaypoint = Math.min(selectedWaypoint, waypoints.count - 1); routeRevision++
    }
    function clearRoute() { waypoints.clear(); selectedWaypoint = -1; routeRevision++ }
    function importRoute(jsonText) {
        try {
            var rows = JSON.parse(jsonText === undefined ? routeInput.text : jsonText)
            if (!Array.isArray(rows) || rows.length > 200) throw new Error("航点数量应为0–200")
            for(var i=0;i<rows.length;i++) {
                var p = rows[i]
                if (p.lat === null || p.lon === null || p.lat === undefined || p.lon === undefined || String(p.lat).trim() === "" || String(p.lon).trim() === "" || !page.validCoordinate(Number(p.lat),Number(p.lon))) throw new Error("经纬度格式无效或超出 Web Mercator 纬度范围")
                if (p.alt !== undefined && (!isFinite(Number(p.alt)) || Number(p.alt)<0)) throw new Error("高度应为非负数")
            }
            waypoints.clear()
            for(i=0;i<rows.length;i++) {
                p=rows[i]
                waypoints.append({no:String(i+1), lat:String(p.lat), lon:String(p.lon), alt:String(p.alt === undefined ? 120 : p.alt), speed:String(p.speed === undefined ? 8 : p.speed), action:String(p.action||"拍照"), hover:String(p.hover === undefined ? 2 : p.hover), state:"待执行"})
            }
            selectedWaypoint = waypoints.count ? 0 : -1; routeRevision++
            routeDialog.close(); actionMessage("已导入本地航线；未上传飞控")
        } catch(e) { actionMessage("导入失败："+e.message) }
    }

    function actionMessage(message) {
        toastText.text = message
        toast.opacity = 1
        toastTimer.restart()
    }

    ListModel {
        id: waypoints
    }

    ListModel {
        id: liveLogs
        ListElement { time: "15:20:11"; type: "任务"; module: "系统"; content: "开始任务：电力巡检_20241226" }
        ListElement { time: "15:20:12"; type: "通信"; module: "机巢"; content: "机巢连接成功（IP：192.168.1.100）" }
        ListElement { time: "15:20:13"; type: "机巢"; module: "机巢"; content: "停止充电成功" }
        ListElement { time: "15:20:15"; type: "机巢"; module: "机巢"; content: "打开舱门" }
        ListElement { time: "15:20:17"; type: "飞行"; module: "无人机"; content: "解锁成功" }
        ListElement { time: "15:20:20"; type: "飞行"; module: "无人机"; content: "起飞成功，高度 20 m" }
        ListElement { time: "15:20:35"; type: "任务"; module: "无人机"; content: "开始执行航线，共 12 个航点" }
        ListElement { time: "15:22:10"; type: "航点"; module: "无人机"; content: "到达航点 1，开始拍照" }
        ListElement { time: "15:24:18"; type: "航点"; module: "无人机"; content: "到达航点 2，开始拍照" }
        ListElement { time: "15:26:35"; type: "航点"; module: "无人机"; content: "到达航点 3，开始拍照" }
    }

    Row {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Item {
            width: 270
            height: parent.height

            GCard {
                id: deviceCard
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                height: 342
                title: "设备状态"

                Column {
                    x: 10; y: 10; width: parent.width - 20; spacing: 7
                    Row {
                        width: parent.width; height: 98
                        Image { width: 98; height: 86; source: "qrc:/assets/drone.png"; fillMode: Image.PreserveAspectFit }
                        Column {
                            width: 146; spacing: 7
                            Row { width: parent.width; Text { objectName: "taskAircraftName"; text: aircraftProfile.name; color: "white"; font.pixelSize: 16; font.bold: true; width: 99; elide: Text.ElideRight } StatusDot { text: groundLink.connected && groundLink.fcuConnected ? "在线" : "未连接"; dotColor: groundLink.connected && groundLink.fcuConnected ? "#00db80" : "#8394a3" } }
                            Text { objectName: "taskAircraftModel"; text: "型号        " + aircraftProfile.model; color: "#cddcec"; font.pixelSize: 14; width: parent.width; elide: Text.ElideRight }
                            Text { objectName: "taskAutopilot"; text: "飞控        " + aircraftProfile.autopilot; color: "#cddcec"; font.pixelSize: 14; width: parent.width; elide: Text.ElideRight }
                            Row { spacing:4; Text { text: "电量        "; color: "#cddcec"; font.pixelSize: 14 } Text { text: groundLink.connected && groundLink.batteryPercent >= 0 ? Math.round(groundLink.batteryPercent) + "%" : "—"; color: "#00e787"; font.pixelSize: 14; font.bold: true } Rectangle { width:42; height:11; radius:2; color:"#122d41"; anchors.verticalCenter:parent.verticalCenter; Rectangle {width:parent.width * Math.max(0, groundLink.batteryPercent) / 100; height:11; radius:2; color:"#00c477"} } }
                        }
                    }
                    Repeater {
                        model: [
                            ["定位", groundLink.odomFresh ? "world 坐标有效" : "未获得 world 定位"], ["飞行模式", groundLink.mode || "—"],
                            ["位置", groundLink.odomFresh ? groundLink.worldX.toFixed(1) + ", " + groundLink.worldY.toFixed(1) + " m" : "—"], ["高度", groundLink.odomFresh ? groundLink.worldZ.toFixed(1) + " m (world)" : "—"],
                            ["速度", "—"], ["卫星数", "—"], ["状态", groundLink.missionReady ? "机载任务就绪" : "未就绪"]
                        ]
                        delegate: Row {
                            width: 248; height: 21
                            Text { width: 102; text: modelData[0]; color: "#aebfd0"; font.pixelSize: 13 }
                            Text { text: modelData[1]; color: index === 6 ? "#00e787" : "#dce9f7"; font.pixelSize: index === 2 ? 11 : 13; font.bold: index === 6 }
                        }
                    }
                }
            }

            GCard {
                id: nestSummary
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: deviceCard.bottom; anchors.topMargin: 10
                height: 227
                title: "机巢"
                Row {
                    x: 10; y: 8; width: parent.width - 20
                    Image { width: 105; height: 98; source: "qrc:/assets/nest.png"; fillMode: Image.PreserveAspectFit }
                    Column {
                        width: 135; spacing: 6
                        Row { Text { text: "机巢"; color: "white"; font.pixelSize: 16; font.bold: true; width: 82 } StatusDot { text: "演示"; dotColor: "#8394a3"; textColor: "#9aafc0" } }
                        Text { text: "机巢型号    HIVE-01"; color: "#cddcec"; font.pixelSize: 13 }
                        Text { text: "舱门状态    " + (page.nestOpen ? "已打开" : "已关闭"); color: "#00e787"; font.pixelSize: 13 }
                        Text { text: "无人机      机内"; color: "#00e787"; font.pixelSize: 13 }
                        Text { text: "充电状态    " + (page.charging ? "充电中" : "已停止"); color: "#00e787"; font.pixelSize: 13 }
                        Text { text: "环境温度    23.5 ℃"; color: "#cddcec"; font.pixelSize: 13 }
                        Text { text: "环境湿度    45 %"; color: "#cddcec"; font.pixelSize: 13 }
                    }
                }
            }

            GCard {
                anchors.left: parent.left; anchors.right: parent.right; anchors.topMargin: 10
                anchors.top: nestSummary.bottom
                anchors.bottom: parent.bottom
                title: "任务信息（演示）"
                Column {
                    x: 10; y: 8; width: parent.width - 20; spacing: 5
                    Repeater {
                        model: [["任务名称", "电力巡检_20241226"], ["任务类型", "航线巡检"], ["任务状态", page.missionState], ["开始时间", "2024-12-26 15:20:11"], ["已飞时间", "00:10:13"]]
                        delegate: Row {
                            width: parent.width; height: 20
                            Text { width: 84; text: modelData[0]; color: "#aebfd0"; font.pixelSize: 13 }
                            Text { text: modelData[1]; color: index === 2 ? "#ffffff" : "#dce9f7"; font.pixelSize: 13 }
                        }
                    }
                    Row { width: parent.width; Text { text: "航点进度"; width: 84; color: "#aebfd0"; font.pixelSize: 13 } Text { text: "3 / 12"; width: 120; color: "#dce9f7"; font.pixelSize: 13 } Text { width: 44; horizontalAlignment: Text.AlignRight; text: "25%"; color: "#dce9f7"; font.pixelSize: 13 } }
                    Rectangle { width: parent.width; height: 13; radius: 6; color: "#143149"; Rectangle { width: parent.width * .25; height: parent.height; radius: 6; color: "#087dff" } }
                    Row { width: parent.width; Text { text: "预计总时长"; width: 84; color: "#aebfd0"; font.pixelSize: 13 } Text { text: "00:40:00"; color: "#dce9f7"; font.pixelSize: 13 } }
                    Grid {
                        width: parent.width; columns: 2; spacing: 8
                        GButton { width: 119; height:36; text: "开始任务"; icon: "▶"; fill: "#00b968"; hoverFill: "#00d079"; onClicked: page.actionMessage("任务控制未接入，未发送飞控指令") }
                        GButton { width: 119; height:36; text: "暂停任务"; icon: "Ⅱ"; fill: "#ff8a00"; hoverFill: "#ffa126"; onClicked: page.actionMessage("任务控制未接入，未发送飞控指令") }
                        GButton { width: 119; height:36; text: "返航"; icon: "⌾"; fill: "#087dff"; hoverFill: "#1695ff"; onClicked: page.actionMessage("返航控制未接入，未发送飞控指令") }
                        GButton { width: 119; height:36; text: "紧急降落"; icon: "×"; fill: "#ff4148"; hoverFill: "#ff5c61"; onClicked: page.actionMessage("当前为界面演示，未连接飞控，未发送降落指令") }
                    }
                }
            }
        }

        Item {
            width: 810
            height: parent.height
                MapPanel {
                    id: map; objectName: "taskMapPanel"
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    // Only the lower list changes tabs; keep the map viewport fixed.
                    height: 450
                    editable: true; waypointModel: waypoints
                    selectedIndex: page.selectedWaypoint; routeRevision: page.routeRevision
                    onWaypointAdded: page.addWaypoint(latitude, longitude)
                    onWaypointMoved: page.moveWaypoint(index, latitude, longitude)
                    onWaypointSelected: page.selectedWaypoint = index
                    onClearRequested: clearRouteDialog.open()
                }

            GCard {
                objectName: "taskDataPanel"
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: map.bottom; anchors.topMargin: 8; anchors.bottom: parent.bottom
                Item {
                    id: tabs
                    objectName: "taskContentTabs"
                    width: parent.width; height: 46
                    Row {
                        anchors.left: parent.left
                        Repeater {
                            model: ["航点列表", "实时日志", "图片列表", "视频列表"]
                            delegate: Rectangle {
                                objectName: "taskContentTab" + index
                                width: 120; height: 46
                                color: page.contentTab === index ? "#087dff" : "#071a29"
                                border.color: "#14344d"
                                Text { anchors.centerIn: parent; text: modelData; color: page.contentTab === index ? "white" : "#b9cadb"; font.pixelSize: 15; font.bold: page.contentTab === index }
                                MouseArea { anchors.fill: parent; onClicked: page.contentTab = index }
                            }
                        }
                    }
                    Row {
                        anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; spacing: 8
                        GButton { visible: page.contentTab === 0; width: 92; height: 34; text: "导入航线"; onClicked: { routeInput.text = page.savedRouteJson() || page.routeJson(); routeDialog.open() } }
                        GButton { visible: page.contentTab === 0; width: 92; height: 34; text: "保存航线"; onClicked: page.saveRoute() }
                        GButton { visible: page.contentTab === 0; width: 92; height: 34; text: "上传航线"; onClicked: page.actionMessage("尚未校准经纬度与机载 world 坐标；航线未发送") }
                    }
                }
                Loader { anchors.left: parent.left; anchors.right: parent.right; anchors.top: tabs.bottom; anchors.bottom: parent.bottom; sourceComponent: page.contentTab === 0 ? waypointView : page.contentTab === 1 ? logView : page.contentTab === 2 ? photoView : videoView }
            }
        }

        Item {
            width: 410
            height: parent.height
            GCard {
                id: realtime
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                height: 385
                title: "实时视频"
                Row {
                    anchors.right: parent.right; anchors.rightMargin: 8; y: -37; height: 36
                    GButton { width: 78; height: 34; text: "主相机"; fill: page.thermal ? "transparent" : "#087dff"; onClicked: page.thermal=false }
                    GButton { width: 70; height: 34; text: "热成像"; fill: page.thermal ? "#087dff" : "transparent"; onClicked: {page.thermal=true; page.actionMessage("热成像示意，未接入热成像视频源")} }
                }
                VideoPanel { anchors.fill: parent; anchors.margins: 5; thermal: page.thermal }
            }

            GCard {
                id: gimbal
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: realtime.bottom; anchors.topMargin: 10
                height: 268
                title: "云台控制"

                // ── 状态与重连 ──
                Row {
                    anchors.right: parent.right; anchors.rightMargin: 10; y: -35; height: 30; spacing: 10
                    StatusDot {
                        anchors.verticalCenter: parent.verticalCenter
                        text: gimbalLink.connected ? "云台在线" : "云台离线"
                        dotColor: gimbalLink.connected ? "#00db80" : "#8394a3"
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        color: gimbalLink.recordStatus === 1 ? "#ff6b6b" : "#9fb4c6"; font.pixelSize: 12
                        text: gimbalLink.recordStatusName + " · " + gimbalLink.modeName
                    }
                    GButton { width: 56; height: 28; text: "重连"; onClicked: page.connectGimbal() }
                }

                Row {
                    x: 11; y: 8; spacing: 20
                    // ── 方向盘：相对步进 ──
                    Rectangle {
                        width: 106; height: 106; radius: 53; color: "#04121d"; border.color: "#123650"; border.width: 2
                        Text { anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter; text: "⌃"; color: "white"; font.pixelSize: 22; width:36; height:28; horizontalAlignment:Text.AlignHCenter; MouseArea { anchors.fill:parent; onClicked: gimbalLink.jog(0, 5) } }
                        Text { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; text: "⌄"; color: "white"; font.pixelSize: 22; width:36; height:28; horizontalAlignment:Text.AlignHCenter; MouseArea { anchors.fill:parent; onClicked: gimbalLink.jog(0, -5) } }
                        Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "‹"; color: "white"; font.pixelSize: 33; width:28; height:40; horizontalAlignment:Text.AlignHCenter; MouseArea { anchors.fill:parent; onClicked: gimbalLink.jog(-5, 0) } }
                        Text { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: "›"; color: "white"; font.pixelSize: 33; width:28; height:40; horizontalAlignment:Text.AlignHCenter; MouseArea { anchors.fill:parent; onClicked: gimbalLink.jog(5, 0) } }
                        Rectangle {
                            anchors.centerIn: parent; width: 26; height: 26; radius: 13; color: "#0d2b3f"; border.color: "#1d4a68"
                            Text { anchors.centerIn: parent; text: "回中"; color: "#cfe0ef"; font.pixelSize: 10 }
                            MouseArea { anchors.fill: parent; onClicked: gimbalLink.centerGimbal() }
                        }
                    }
                    // ── 滑条：绝对角度 / 变倍 ──
                    Column {
                        width: 240; spacing: 13
                        Repeater {
                            model: ["俯仰角", "航向角", "变焦"]
                            delegate: Row {
                                width: 252; height: 25; spacing: 5
                                Text { width: 53; text: modelData; color: "#aebfd0"; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                                Text {
                                    width: 38; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter
                                    text: index===0 ? gimbalLink.pitch.toFixed(0)+"°"
                                        : index===1 ? gimbalLink.yaw.toFixed(0)+"°"
                                        : gimbalLink.zoom.toFixed(1)+"x"
                                    color: gimbalLink.attitudeFresh ? "#dce9f7" : "#6d8496"
                                }
                                GButton {
                                    width:25; height:25; text:"−"; fill:"#091e2d"
                                    onClicked: {
                                        if (index===0) gimbalLink.jog(0, -1)
                                        else if (index===1) gimbalLink.jog(-1, 0)
                                        else gimbalLink.zoomStep(-1)
                                    }
                                }
                                Slider {
                                    id: adjuster
                                    width: 88; height:25
                                    from: index===0 ? gimbalLink.pitchMin : index===1 ? -gimbalLink.yawLimit : 1
                                    to:   index===0 ? gimbalLink.pitchMax : index===1 ?  gimbalLink.yawLimit : gimbalLink.zoomMax
                                    stepSize: index===2 ? 0.1 : 1
                                    value: index===0 ? page.pitchCmd : index===1 ? page.headingCmd : page.zoomCmd
                                    onMoved: {
                                        if (index===0) { page.pitchCmd = value; gimbalLink.setAttitude(page.headingCmd, value) }
                                        else if (index===1) { page.headingCmd = value; gimbalLink.setAttitude(value, page.pitchCmd) }
                                        else { page.zoomCmd = value; gimbalLink.setZoomAbsolute(value) }
                                    }
                                    background: Rectangle { x:adjuster.leftPadding; y:(adjuster.height-height)/2; width:adjuster.availableWidth; height:3; color:"#132f44"; Rectangle {width:adjuster.visualPosition*parent.width; height:3; color:"#0788ff"} }
                                    handle: Rectangle { x:adjuster.leftPadding+adjuster.visualPosition*(adjuster.availableWidth-width); y:(adjuster.height-height)/2; width:13; height:13; radius:7; color:"#087dff" }
                                }
                                GButton {
                                    width:25; height:25; text:"+"; fill:"#091e2d"
                                    onClicked: {
                                        if (index===0) gimbalLink.jog(0, 1)
                                        else if (index===1) gimbalLink.jog(1, 0)
                                        else gimbalLink.zoomStep(1)
                                    }
                                }
                                // 相机回报真实角度时同步滑条（用户正在拖动时不打扰）
                                Connections {
                                    target: gimbalLink
                                    function onAttitudeChanged() {
                                        if (!gimbalLink.attitudeFresh || adjuster.pressed) return
                                        if (index === 0) { adjuster.value = gimbalLink.pitch; page.pitchCmd = gimbalLink.pitch }
                                        else if (index === 1) { adjuster.value = gimbalLink.yaw; page.headingCmd = gimbalLink.yaw }
                                    }
                                    function onChanged() {
                                        if (index !== 2 || adjuster.pressed) return
                                        adjuster.value = gimbalLink.zoom
                                        page.zoomCmd = gimbalLink.zoom
                                    }
                                }
                            }
                        }
                    }
                }

                // ── 云台模式与快捷动作 ──
                Row {
                    x: 12; y: 122; spacing: 6
                    Repeater {
                        model: [["锁定", 0], ["跟随", 1], ["FPV", 2]]
                        delegate: GButton {
                            width: 58; height: 30; text: modelData[0]
                            fill: gimbalLink.mode === modelData[1] ? "#087dff" : "transparent"
                            onClicked: gimbalLink.setMode(modelData[1])
                        }
                    }
                    Item { width: 8; height: 1 }
                    GButton { width: 58; height: 30; text: "朝下"; onClicked: gimbalLink.lookDown() }
                    GButton { width: 68; height: 30; text: "软重启"; onClicked: gimbalLink.softReboot() }
                }

                // ── 相机功能 ──
                Row {
                    anchors.left: parent.left; anchors.leftMargin: 11; anchors.bottom: parent.bottom; anchors.bottomMargin: 11; spacing: 7
                    Repeater {
                        model: [["拍照", "◉"], ["录像", "■"], ["变焦+", "⌕"], ["变焦−", "⌕"]]
                        delegate: GButton {
                            width: 91; height: 38
                            text: index===1 && gimbalLink.recordStatus === 1 ? "停止" : modelData[0]
                            icon: modelData[1]
                            onClicked: {
                                if (index === 0) gimbalLink.takePhoto()
                                else if (index === 1) gimbalLink.toggleRecording()
                                else if (index === 2) gimbalLink.zoomStep(1)
                                else gimbalLink.zoomStep(-1)
                            }
                        }
                    }
                }
            }

            GCard {
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: gimbal.bottom; anchors.topMargin: 10; anchors.bottom: parent.bottom
                title: "机巢控制"
                Row {
                    x: 10; y: 10; spacing: 10
                    Column {
                        width: 145; spacing: 8
                        Image { width: 145; height: 125; source: "qrc:/assets/nest.png"; fillMode: Image.PreserveAspectFit }
                        Text { text: "机巢当前状态："; color: "#dce9f7"; font.pixelSize: 13 }
                        Text { text: page.nestOpen ? "已打开" : "已关闭"; color: "#00e787"; font.pixelSize: 14; font.bold: true }
                    }
                    Column {
                        width: 118; spacing: 8
                        GButton { width: 112; height: 36; text: "打开舱门"; fill: "#00af64"; onClicked: {page.nestOpen=true; page.actionMessage("演示舱门已打开，未发送机巢指令")} }
                        GButton { width: 112; height: 36; text: "关闭舱门"; onClicked: {page.nestOpen=false; page.actionMessage("演示舱门已关闭")} }
                        GButton { width: 112; height: 36; text: "开始充电"; onClicked: {page.charging=true; page.actionMessage("演示充电状态已开启")} }
                        GButton { width: 112; height: 36; text: "停止充电"; onClicked: {page.charging=false; page.actionMessage("演示充电状态已停止")} }
                        GButton { width: 112; height: 36; text: "机巢设置"; onClicked: page.requestSettings() }
                    }
                    Column {
                        spacing: 9
                        Text { text: "机巢状态"; color: "white"; font.pixelSize: 14; font.bold: true }
                        Repeater {
                            model: ["在线", page.nestOpen ? "舱门已打开" : "舱门已关闭", "无人机 机内", page.charging ? "充电中" : "已停止", "温度 23.5 ℃", "湿度 45 %", "供电 正常"]
                            delegate: Row { spacing: 8; Rectangle { width: 11; height: 11; radius: 6; color: "#00db80"; anchors.verticalCenter: parent.verticalCenter } Text { text: modelData; color: "#cfe0ef"; font.pixelSize: 12 } }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: waypointView
        Item {
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 36; color: "#0a2234"
                Row { anchors.fill: parent
                    Repeater { model: [["序号", 42], ["纬度", 108], ["经度", 116], ["高度 (m)", 80], ["速度 (m/s)", 94], ["动作", 68], ["悬停 (s)", 76], ["状态", 106], ["操作", 100]]; delegate: Text { width: modelData[1]; height: 36; text: modelData[0]; color: "#9fb4c8"; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter } }
                }
            }
            ListView {
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.topMargin: 36; anchors.bottom: parent.bottom
                model: waypoints; clip: true
                Text { anchors.centerIn: parent; visible: waypoints.count === 0; text: "暂无航点 · 点击地图上的“添加航点”开始规划"; color: "#91adc2"; font.pixelSize: 15 }
                delegate: Rectangle {
                    id: waypointRow
                    property string waypointState: model.state
                    width: ListView.view.width; height: 31; color: index === page.selectedWaypoint ? "#0757ad" : (index % 2 ? "#081d2d" : "#071725"); border.color: "#102d42"
                    MouseArea { anchors.fill:parent; onClicked:page.selectedWaypoint=index }
                    Row { anchors.fill: parent
                        Repeater { model: [[no, 42], [lat, 108], [lon, 116], [alt, 80], [speed, 94], [action, 68], [hover, 76], [waypointRow.waypointState, 106]]; delegate: Text { width: modelData[1]; height: 31; text: modelData[0]; color: modelData[0] === "已完成" ? "#00e787" : modelData[0] === "执行中" ? "#ffe000" : "#dce9f7"; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter } }
                        Row { width:100; height:31; spacing:8
                            GButton { text:"✎"; width:36; height:30; fill:"transparent"; onClicked:{ page.selectedWaypoint=index; editLat.text=lat; editLon.text=lon; editAlt.text=alt; waypointDialog.open() } }
                            GButton { text:"×"; width:36; height:30; fill:"transparent"; onClicked:page.deleteWaypoint(index) }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: logView
        Item {
            Row { anchors.right: parent.right; anchors.rightMargin: 10; y: 8; spacing: 8
                GButton { width: 75; height: 32; text: "清空"; onClicked: liveLogs.clear() }
                GButton { width: 92; height: 32; text: "导出日志"; onClicked: page.actionMessage("当前为示例日志，尚未接入文件导出") }
            }
            Rectangle { x: 7; y: 49; width: parent.width - 14; height: 34; color: "#0a2234"
                Row { anchors.fill: parent; Repeater { model: [["时间", 130], ["类型", 130], ["模块", 130], ["内容", 380]]; delegate: Text { width: modelData[1]; height: 34; text: modelData[0]; color: "#aabed1"; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter } } }
            }
            ListView { x: 7; y: 83; width: parent.width - 14; height: parent.height - 90; model: liveLogs; clip: true
                delegate: Rectangle { width: ListView.view.width; height: 29; color: index % 2 ? "#081d2d" : "#071725"; border.color: "#102d42"
                    Row { anchors.fill: parent
                        Rectangle { width: 12; height: 12; radius: 6; color: index % 3 ? "#00db80" : "#087dff"; anchors.verticalCenter: parent.verticalCenter }
                        Text { width: 118; height: 29; text: time; color: "#cfe0ef"; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 12 }
                        Text { width: 130; height: 29; text: type; color: "#cfe0ef"; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 12 }
                        Text { width: 130; height: 29; text: module; color: "#cfe0ef"; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 12 }
                        Text { width: 380; height: 29; text: content; color: "#cfe0ef"; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
                    }
                }
            }
        }
    }

    Component {
        id: photoView
        Item {
            Row { x: 9; y: 8; spacing: 10
                GField { width: 145; height: 34; text: "2024-12-26" }
                GField { width: 126; height: 34; text: "全部航点" }
                GField { width: 126; height: 34; text: "全部动作" }
                GField { id: photoSearch; width: 208; height: 34; placeholder: "搜索图片名称" }
                GButton { width: 112; height: 34; text: "批量下载"; onClicked: page.actionMessage("当前为参考图片，未连接媒体存储") }
            }
            Flickable {
                objectName: "taskPhotoScroll"
                x: 9; y: 60; width: parent.width - 18
                height: Math.max(0, photoPagination.y - y - 10)
                contentWidth: width; contentHeight: photoGrid.implicitHeight
                clip: true; boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                Grid {
                id: photoGrid
                width: parent.width; columns: 6; spacing: 10
                Repeater { model: Math.min(18,86-page.photoPage*18)
                    delegate: Item {
                        property int number: page.photoPage*18+index+1
                        property string filename: "IMG_" + ("000"+number).slice(-4)+".jpg"
                        property bool selected: false
                        width: 123; height: 112
                        visible: filename.toLowerCase().indexOf(photoSearch.text.toLowerCase()) >= 0
                        MediaThumb { width:123; height:76; photoIndex:index }
                        MouseArea { anchors.fill:parent; onClicked:{page.inspectedPhoto=index; photoDialog.open()} }
                        Rectangle { x: 4; y: 4; width: 15; height: 15; radius:2; color: parent.selected ? "#087dff" : "#345066"; border.color: "#cfe0ef"; MouseArea {anchors.fill:parent; onClicked:parent.parent.selected=!parent.parent.selected} }
                        Text { y: 80; text: parent.filename; color: "#dce9f7"; font.pixelSize: 12 }
                        Text { y: 98; text: "15:" + (22 + Math.floor(index/3)) + ":" + (10 + index) + "    航点" + (Math.floor(index/2)+1); color: "#8fa6ba"; font.pixelSize: 11 }
                    }
                }
                }
            }
            Row { id: photoPagination; anchors.bottom:parent.bottom; anchors.bottomMargin:10; anchors.right:parent.right; anchors.rightMargin:12; spacing:6
                Text { text:"共 86 张"; color:"#aebfd0"; font.pixelSize:12; anchors.verticalCenter:parent.verticalCenter }
                Repeater { model:5; delegate:GButton {width:29; height:29; text:String(index+1); fill:page.photoPage===index?"#087dff":"#061522"; onClicked:page.photoPage=index} }
                GButton { text:"18 张/页"; width:88; height:29; fill:"#061522"; onClicked:page.actionMessage("每页展示18张参考图片") }
            }
        }
    }

    Component {
        id: videoView
        Item {
            Row { x: 9; y: 8; spacing: 10
                GField { width: 180; height: 34; text: "2024-12-26" }
                GField { width: 110; height: 34; text: "全部航段" }
                GField { width: 336; height: 34; placeholder: "搜索视频名称" }
                GButton { width: 100; height: 34; text: "批量下载"; onClicked: page.actionMessage("当前为视频封面示例，未连接媒体存储") }
            }
            Row { x: 9; y: 52; spacing: 10
                ListView { width: 420; height: parent.parent.height - 62; model: 5; clip: true
                    delegate: Rectangle { width: 420; height: 65; color: index === page.selectedVideo ? "#0a396d" : "#071927"; border.color: index === page.selectedVideo ? "#087dff" : "#15344b"; radius: 3
                        MediaThumb { x: 31; y: 5; width: 95; height: 55; photoIndex:index*2 }
                        Rectangle { x: 7; y: 25; width: 13; height: 13; color: index === page.selectedVideo ? "#087dff" : "transparent"; border.color: "#bcd0e2" }
                        Text { x: 136; y: 8; text: "DJI_20241226_15" + (2201 + index * 234) + ".mp4"; color: "#e7f1fb"; font.pixelSize: 12 }
                        Text { x: 136; y: 29; text: "15:22:01 - 15:24:35     航段" + (index + 1); color: "#9db1c5"; font.pixelSize: 11 }
                        Text { x: 136; y: 47; text: (3.2 - index * .15).toFixed(1) + " GB"; color: "#9db1c5"; font.pixelSize: 11 }
                        MouseArea {anchors.fill:parent; onClicked:page.selectedVideo=index}
                    }
                }
                Rectangle { width: 350; height: 350; color: "#06131e"; border.color: "#1b415c"; radius: 4
                    Text { x:10; y:8; text:"DJI_20241226_15"+(2201+page.selectedVideo*234)+".mp4"; color:"#dce9f7"; font.pixelSize:12 }
                    VideoPanel { x: 8; y: 29; width: 334; height: 184; clean: true }
                    Text { x: 10; y: 222; text: "▶    00:00:45 / 00:02:34                         ⛶"; color: "white"; font.pixelSize: 12; MouseArea {anchors.fill:parent; onClicked:page.actionMessage("演示封面，尚未加载可播放的视频文件")} }
                    Row { x:10; y:247; spacing:5; Repeater {model:5; delegate:MediaThumb {width:61; height:38; photoIndex:index+page.selectedVideo} } }
                    Text { x: 10; y: 295; text: "拍摄时间    2024-12-26 15:22:01\n航段        航段"+(page.selectedVideo+1)+"    文件大小    "+(3.2-page.selectedVideo*.15).toFixed(1)+" GB\n分辨率      3840 × 2160   30 fps"; color: "#cbdbea"; font.pixelSize: 11; lineHeight: 1.35 }
                }
            }
        }
    }

    Dialog {
        id: routeDialog
        title:"导入航线 · JSON"
        width:640; height:500; x:(page.width-width)/2; y:(page.height-height)/2
        modal:true
        background: Rectangle {color:"#0a2234"; border.color:"#1b5279"; radius:6}
        contentItem: Column { spacing:12
            Text {text:"粘贴包含 lat / lon / alt / speed 字段的航点数组"; color:"#cfe0ef"; font.pixelSize:14}
            ScrollView { width:parent.width; height:340; TextArea {id:routeInput; selectByMouse:true; wrapMode:TextEdit.Wrap; color:"#dce9f7"; font.pixelSize:13; background:Rectangle {color:"#061522"}} }
            Row {spacing:10; GButton {text:"导入"; fill:"#087dff"; onClicked:page.importRoute()} GButton {text:"取消"; onClicked:routeDialog.close()} }
        }
    }
    Dialog {
        id: waypointDialog
        title:"编辑航点 "+(page.selectedWaypoint+1)
        width:420; height:280; x:(page.width-width)/2; y:(page.height-height)/2; modal:true
        background: Rectangle {color:"#0a2234"; border.color:"#1b5279"; radius:6}
        contentItem: Column {spacing:12
            Row { Text {text:"纬度"; width:70; color:"white"} GField {id:editLat; width:300} }
            Row { Text {text:"经度"; width:70; color:"white"} GField {id:editLon; width:300} }
            Row { Text {text:"高度 (m)"; width:70; color:"white"} GField {id:editAlt; width:300} }
            GButton {text:"保存"; fill:"#087dff"; onClicked: {
                var la=Number(editLat.text),lo=Number(editLon.text),a=Number(editAlt.text)
                if(!page.validCoordinate(la,lo)||!isFinite(a)||a<0||page.selectedWaypoint<0) {page.actionMessage("请输入有效经纬度和高度");return}
                page.moveWaypoint(page.selectedWaypoint,la,lo);waypoints.setProperty(page.selectedWaypoint,"alt",String(a));waypointDialog.close()
            } }
        }
    }
    Dialog {
        id: clearRouteDialog
        parent: Overlay.overlay
        title: "清空本地航线？"
        width: 420
        modal: true; standardButtons: Dialog.Ok | Dialog.Cancel
        x: (Overlay.overlay.width - width) / 2; y: (Overlay.overlay.height - height) / 2
        contentItem: Text { text: "将清空当前编辑航点；不会清除飞控任务。\n保存后才会覆盖已保存的本地航线。"; color: "#203040"; wrapMode: Text.WordWrap }
        onAccepted: page.clearRoute()
    }
    Popup {
        id:photoDialog; width:910; height:630; x:(page.width-width)/2; y:(page.height-height)/2; modal:true; focus:true
        background:Rectangle {color:"#061522"; border.color:"#087dff"; radius:5}
        contentItem:Item { MediaThumb {anchors.fill:parent; anchors.bottomMargin:40; photoIndex:page.inspectedPhoto} GButton {anchors.bottom:parent.bottom; anchors.right:parent.right; text:"关闭预览"; onClicked:photoDialog.close()} }
    }

    Rectangle {
        id: toast
        z: 20
        width: toastText.width + 46
        height: 42
        radius: 6
        color: "#d90b2336"
        border.color: "#238ad7"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 180 } }
        Text { id: toastText; anchors.centerIn: parent; color: "white"; font.pixelSize: 14 }
        Timer { id: toastTimer; interval: 1800; onTriggered: toast.opacity = 0 }
    }
}
