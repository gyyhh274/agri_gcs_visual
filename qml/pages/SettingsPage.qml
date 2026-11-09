import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt.labs.settings 1.1
import "../components"

Item {
    id: page
    property int sectionIndex: 0
    property bool overviewMode: false
    readonly property bool droneConnected: groundLink.connected && groundLink.fcuConnected
    property bool nestConnected: false
    property bool thermal: false
    property string rtkPassword: "12345678"
    property bool nestFieldsEdited: false
    property var sectionNames: ["连接设置", "无人机设置", "机巢设置", "任务参数", "地图设置", "告警与安全", "存储与数据", "系统维护"]
    property var values: ({
        gatewayProtocol: "ros-gateway-v1", droneIp: "127.0.0.1", dronePort: "8765", droneAuto: false,
        nestIp: "192.168.1.100", nestPort: "8080", nestAuto: true,
        videoUrl: "rtsp://192.168.1.10:8554/live", videoPort: "8554", controlPort: "8765",
        rtkEnabled: true, rtkHost: "192.168.1.200", rtkPort: "2101", rtkAccount: "user", rtkMount: "RTCM32_GGB",
        deviceTime: "2024-12-26 15:30:24", ntpEnabled: true, ntpServer: "pool.ntp.org",
        autoStart: true, autoReconnect: true, connectionAlarm: true, reconnectInterval: "10",
        aircraftName: "巡检无人机 01", aircraftModel: "DJI M30T", autopilot: "PX4", serialPort: "/dev/ttyS4", baudRate: "57600", returnHeight: "120",
        nestName: "机巢 01", nestModel: "HIVE-01", nestLongitude: "121.4700", nestLatitude: "25.2000", nestChargeThreshold: "90", nestFanTemperature: "35",
        flightHeight: "120", flightSpeed: "8.5", waypointHold: "3", photoInterval: "5", routeSpacing: "30", missionReturn: true,
        tileDirectory: "", tileFormat: "XYZ", mapZoom: "16", mapLongitude: "119.63764", mapLatitude: "29.13678", showRoute: true,
        lowBattery: "25", criticalBattery: "15", windLimit: "12", lostLinkTimeout: "10", maxFlightHeight: "120", geofenceEnabled: true,
        dataDirectory: "./data", logDays: "30", videoDirectory: "./recordings", imageDirectory: "./photos", recordingEnabled: false,
        deviceName: "Orange Pi 5 Max", uiScale: "100", brightness: "80", language: "简体中文", touchEnabled: true
    })

    Settings { id: savedSettings; category: "GroundStationUi"; property string configuration: "" }
    readonly property string savedConfiguration: savedSettings.configuration
    Component.onCompleted: {
        if (savedSettings.configuration.length) {
            try {
                var oldValues = JSON.parse(savedSettings.configuration)
                // Previous releases stored a demonstrative MAVLink endpoint; do not
                // silently reuse it for the new ROS gateway.
                if (oldValues.gatewayProtocol !== "ros-gateway-v1") {
                    oldValues.droneIp = "127.0.0.1"
                    oldValues.dronePort = "8765"
                    oldValues.droneAuto = false
                    oldValues.controlPort = "8765"
                    oldValues.gatewayProtocol = "ros-gateway-v1"
                }
                page.values = Object.assign({}, page.values, oldValues)
            }
            catch (error) { page.notify("本地配置无法读取，已使用默认界面参数") }
        }
        page.setValue("tileDirectory", offlineMapSource.directory)
        page.setValue("tileFormat", offlineMapSource.tms ? "TMS" : "XYZ")
        page.syncNestFields()
    }
    function syncNestFields() {
        var next = Object.assign({}, values)
        next.nestName = nestPosition.configured ? nestPosition.name : "机巢 01"
        next.nestLatitude = nestPosition.latitude.toFixed(7)
        next.nestLongitude = nestPosition.longitude.toFixed(7)
        values = next
    }
    Connections {
        target: nestPosition
        function onChanged() { if (!page.nestFieldsEdited) page.syncNestFields() }
    }
    function setValue(key, value) {
        if (!key.length || values[key] === value) return
        if (key === "nestName" || key === "nestLatitude" || key === "nestLongitude") nestFieldsEdited = true
        var next = Object.assign({}, values)
        next[key] = value
        values = next
    }
    function save() {
        if (sectionIndex === 2 && !nestPosition.configure(String(values.nestLatitude), String(values.nestLongitude), String(values.nestName))) {
            notify("机巢坐标无效：请填写 WGS84 经纬度，纬度 ±85.05112878、经度 ±180 内；未保存")
            return
        }
        if (sectionIndex === 4) {
            var scheme = String(values.tileFormat).trim().toUpperCase()
            if (scheme !== "XYZ" && scheme !== "TMS") { notify("瓦片格式必须为 XYZ 或 TMS"); return }
            offlineMapSource.configure(values.tileDirectory, scheme === "TMS")
        }
        savedSettings.configuration = JSON.stringify(values)
        savedSettings.sync()
        aircraftProfile.applyConfiguration(savedSettings.configuration)
        notify(sectionIndex === 4 ? "地图目录已应用，正在检查本地瓦片" :
               sectionIndex === 1 ? "无人机资料已保存，任务页设备信息已同步；未向设备下发" :
               "本地界面配置已保存；硬件参数尚未下发")
    }
    function notify(message) {
        toastText.text = message
        toast.opacity = 1
        toastTimer.restart()
    }

    Row {
        id: columns
        anchors.fill: parent; anchors.margins: 13; spacing: 12
        Rectangle {
            visible: !page.overviewMode
            width: 175; height: parent.height; color: "#061827"; border.color: "#0d2c43"; radius: 5
            Column {
                width: parent.width
                Repeater {
                    model: ["⌁", "♜", "▣", "▤", "◉", "♧", "▰", "⚒"]
                    delegate: Rectangle {
                        width: 175; height: 58; color: page.sectionIndex === index ? "#0755c8" : "transparent"; radius: page.sectionIndex === index ? 4 : 0
                        Row {
                            x: 15; anchors.verticalCenter: parent.verticalCenter; spacing: 13
                            Text { text: modelData; color: "white"; font.pixelSize: 23; width: 25; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: page.sectionNames[index]; color: "#edf5ff"; font.pixelSize: 16; font.bold: page.sectionIndex === index; anchors.verticalCenter: parent.verticalCenter }
                        }
                        MouseArea { anchors.fill: parent; onClicked: page.sectionIndex = index }
                    }
                }
            }
        }
        Item {
            id: centerColumn
            width: columns.width - (page.overviewMode ? 0 : 175) - rightColumn.width - (page.overviewMode ? 12 : 24); height: parent.height
            Loader { anchors.fill: parent; sourceComponent: page.sectionIndex === 0 ? connectionSettings : configurationSettings }
        }
        Item {
            id: rightColumn
            width: page.overviewMode ? 506 : 462; height: parent.height
            GCard { id: posCard; width: parent.width; height: page.overviewMode ? 408 : 298; title: "设备位置"; MapPanel { anchors.fill: parent; anchors.margins: 4 } }
            GCard {
                id: videoCard; width: parent.width; height: page.overviewMode ? 278 : 350; anchors.top: posCard.bottom; anchors.topMargin: 10; title: "实时视频"
                Row {
                    anchors.right: parent.right; anchors.rightMargin: 8; y: -37; spacing: 2
                    GButton { width: 74; height: 32; text: "主相机"; fill: !page.thermal ? "#087dff" : "transparent"; onClicked: page.thermal = false }
                    GButton { width: 74; height: 32; text: "热成像"; fill: page.thermal ? "#087dff" : "transparent"; onClicked: { page.thermal = true; page.notify("热成像为界面色调演示，未接入热成像设备") } }
                }
                VideoPanel { anchors.fill: parent; anchors.margins: 4; thermal: page.thermal }
            }
            GCard {
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: videoCard.bottom; anchors.topMargin: 10; anchors.bottom: parent.bottom
                title: "系统状态"
                Column {
                    x: 15; y: 3; width: parent.width - 30
                    Repeater {
                        model: [
                            ["✣", "无人机连接", page.droneConnected ? "已连接" : "未连接", page.values.droneIp],
                            ["▣", "机巢连接", page.nestConnected ? "已连接" : "未连接", page.values.nestIp],
                            ["▧", "图传状态", VideoConfig.runtimeLive ? "实时" : (VideoConfig.runtimePhase === "connecting" ? "连接中" : VideoConfig.runtimePhase === "retry" ? "重连中" : "未接入"), VideoConfig.runtimeUsingPipeline ? "GStreamer" : "Qt"], ["●", "定位状态", groundLink.odomFresh ? "world 有效" : "未获得", "局部坐标"],
                            ["⌖", "RTK状态", "未接入", ""], ["▰", "网络状态", groundLink.connected ? "网关已连" : "未连接", groundLink.endpoint]
                        ]
                        delegate: Item {
                            width: parent.width; height: page.overviewMode ? 29 : 34
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                Text { width: 49; text: modelData[0]; color: "white"; font.pixelSize: 21; anchors.verticalCenter: parent.verticalCenter }
                                FormLabel { width: 105; height: 28; text: modelData[1] }
                                StatusDot { width: 126; anchors.verticalCenter: parent.verticalCenter; text: modelData[2]; dotColor: text === "已连接" || text === "world 有效" || text === "网关已连" ? "#00db80" : "#8394a3"; textColor: dotColor }
                                FormLabel { width: 145; height: 28; text: modelData[3]; elide: Text.ElideRight }
                            }
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#102b3e" }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: connectionSettings
        /* 一张卡说清「连谁、用什么身份、通没通」：
         *   地址 + 用户 → 派生 7 项服务，逐项做协议握手，而不是只看端口开没开。
         * 端口、隧道端点收进下面的「高级」，因为它们是常量，不该占主流程。 */
        Flickable {
            id: connFlick
            contentWidth: width
            contentHeight: connContent.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Column {
                id: connContent
                width: connFlick.width
                spacing: 10

                GCard {
                    id: onboardCard
                    width: parent.width
                    height: relayDiscovery.services.length > 0 ? 460 : 322
                    title: "机载连接"

                    /* 全部非 skip 项都通过才算就绪；空表时不算就绪。 */
                    property bool allReady: {
                        var list = relayDiscovery.services
                        if (!list || list.length === 0) return false
                        for (var i = 0; i < list.length; ++i) {
                            var state = list[i].state
                            if (state === "fail" || state === "pending") return false
                        }
                        return true
                    }

                    StatusDot {
                        anchors.right: parent.right; anchors.rightMargin: 18; y: -29
                        width: 210
                        text: relayDiscovery.services.length === 0 ? "未测试"
                              : (relayDiscovery.checking ? "测试中…"
                                 : (onboardCard.allReady ? "全部就绪" : relayDiscovery.checkSummary))
                        dotColor: relayDiscovery.services.length === 0 ? "#8394a3"
                                  : (relayDiscovery.checking ? "#f0a51e"
                                     : (onboardCard.allReady ? "#00db80" : "#e2566b"))
                        textColor: dotColor
                    }

                    Column {
                        x: 16; y: 6; width: parent.width - 32; spacing: 7

                        /* ── 唯一的两个输入 ─────────────────────────────── */
                        Row {
                            width: parent.width; height: 33; spacing: 8
                            FormLabel { width: 76; height: 33; text: "机载电脑" }
                            ValueField {
                                width: 190; height: 33
                                text: VideoConfig.host
                                placeholderText: "IP 或主机名"
                                onTextEdited: VideoConfig.host = text.trim()
                            }
                            FormLabel { width: 36; height: 33; text: "用户"; horizontalAlignment: Text.AlignRight }
                            ValueField {
                                width: 92; height: 33
                                text: VideoConfig.sshUser
                                placeholderText: "ly"
                                onTextEdited: VideoConfig.sshUser = text.trim()
                            }
                            GButton {
                                width: 104; height: 33
                                text: relayDiscovery.scanning ? "扫描中…" : "自动发现"
                                fill: relayDiscovery.scanning ? "#2a5d80" : "#0a6b4a"
                                onClicked: relayDiscovery.scan(VideoConfig.host)
                            }
                        }
                        Row {
                            width: parent.width; height: 31; spacing: 8
                            FormLabel { width: 76; text: "项目目录" }
                            ValueField {
                                width: parent.width - 84; height: 31
                                text: VideoConfig.projectDir
                                placeholderText: "/home/ly/Documents/diff-planner-A8mini-gcs-mission"
                                onTextEdited: VideoConfig.projectDir = text.trim()
                            }
                        }
                        Row {
                            width: parent.width; height: 31; spacing: 8
                            FormLabel { width: 76; text: "认证" }
                            Row {
                                width: 132; height: 31; spacing: 12
                                Repeater {
                                    model: ["密钥", "密码"]
                                    delegate: Item {
                                        width: 56; height: 31
                                        Rectangle {
                                            x: 0; y: 8; width: 15; height: 15; radius: 8
                                            color: VideoConfig.sshUsePassword === (index === 1) ? "#087dff" : "transparent"
                                            border.color: "#5f8bab"
                                            Rectangle {
                                                anchors.centerIn: parent; width: 5; height: 5; radius: 3; color: "white"
                                                visible: VideoConfig.sshUsePassword === (index === 1)
                                            }
                                        }
                                        Text {
                                            x: 24; anchors.verticalCenter: parent.verticalCenter
                                            text: modelData; color: "#dce8f4"; font.pixelSize: 13
                                        }
                                        MouseArea { anchors.fill: parent; onClicked: VideoConfig.sshUsePassword = (index === 1) }
                                    }
                                }
                            }
                            ValueField {
                                width: 240; height: 31
                                text: VideoConfig.sshPassword
                                echoMode: TextInput.Password
                                enabled: VideoConfig.sshUsePassword
                                opacity: VideoConfig.sshUsePassword ? 1 : 0.45
                                placeholderText: VideoConfig.sshUsePassword ? "机载电脑登录密码"
                                                                          : "使用密钥 ~/.ssh/agri_*.ed25519"
                                onTextEdited: VideoConfig.sshPassword = text
                            }
                            Text {
                                width: parent.width - 480; height: 31
                                text: VideoConfig.sshUsePassword ? "仅存本机配置（600）" : "密钥由隧道服务读取"
                                color: "#7f97ab"; font.pixelSize: 11
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                        }

                        /* ── 设备侧账号（视频中转的只读账号） ───────────── */
                        Row {
                            width: parent.width; height: 31; spacing: 8
                            FormLabel { width: 76; text: "视频账号" }
                            ValueField { width: 110; height: 31; text: VideoConfig.userId; onTextEdited: VideoConfig.userId = text }
                            ValueField {
                                width: 210; height: 31
                                text: VideoConfig.userPw
                                echoMode: TextInput.Password
                                placeholderText: "MediaMTX 只读口令"
                                onTextEdited: VideoConfig.userPw = text
                            }
                            GButton {
                                width: 76; height: 31
                                text: VideoConfig.enabled ? "停止" : "拉流"
                                fill: VideoConfig.enabled ? "#2a5d80" : "#087dff"
                                onClicked: VideoConfig.enabled = !VideoConfig.enabled
                            }
                            GButton { width: 64; height: 31; text: "重连"; onClicked: VideoConfig.reconnectRequest += 1 }
                        }

                        /* ── 一键自检 ───────────────────────────────────── */
                        Row {
                            width: parent.width; height: 36; spacing: 10
                            GButton {
                                width: 120; height: 33
                                text: relayDiscovery.checking ? "测试中…" : "测试连接"
                                fill: "#087dff"
                                onClicked: relayDiscovery.checkAll(VideoConfig.host, VideoConfig.sshUser)
                            }
                            Text {
                                width: parent.width - 130; height: 33
                                text: relayDiscovery.checkSummary
                                color: "#9fb4c6"; font.pixelSize: 12
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                        }

                        Rectangle { width: parent.width; height: 1; color: "#15344b" }

                        Row {
                            width: parent.width; height: 20
                            Text { width: 96; height: 20; text: "服务"; color: "#7f97ab"; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                            Text { width: 166; height: 20; text: "地址"; color: "#7f97ab"; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                            Text { width: 62; height: 20; text: "状态"; color: "#7f97ab"; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                            Text { width: parent.width - 324; height: 20; text: "说明（协议握手结果）"; color: "#7f97ab"; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                        }

                        Repeater {
                            model: relayDiscovery.services
                            delegate: Item {
                                width: parent.width; height: 25
                                property var svc: modelData
                                Text { width: 96; height: 25; text: svc.label; color: "#cbdbea"; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                                Text { x: 96; width: 166; height: 25; text: svc.endpoint; color: "#8fa6ba"; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                StatusDot {
                                    x: 262; width: 62
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: svc.state === "ok" ? "就绪"
                                          : svc.state === "fail" ? "失败"
                                          : svc.state === "skip" ? "不验" : "测试中"
                                    dotColor: svc.state === "ok" ? "#00db80"
                                              : svc.state === "fail" ? "#e2566b" : "#8394a3"
                                    textColor: dotColor
                                }
                                Text {
                                    x: 324; width: parent.width - 324; height: 25
                                    text: svc.detail
                                    color: svc.state === "fail" ? "#e8a2ad" : "#9fb4c6"
                                    font.pixelSize: 11; verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#0f2a3d" }
                            }
                        }

                        Text {
                            width: parent.width
                            visible: relayDiscovery.services.length === 0
                            text: "尚未测试。点「测试连接」会逐项做协议握手（SSH banner / RTSP / 照片 JSON / 点云 / 建图 / 遥测），因为只看端口能不能连是不够的：走代理时任何 IP 都会「连得上」。"
                            height: 34
                            color: "#6d8496"; font.pixelSize: 11; wrapMode: Text.WordWrap
                        }
                    }
                }

                Row {
                    id: lowerRow
                    width: parent.width; height: 278; spacing: 8
                    property real rtkWidth: Math.round((width - spacing) * 0.535)
                    GCard {
                        width: lowerRow.rtkWidth; height: parent.height; title: "RTK设置"
                        Column {
                            x: 13; y: 8; width: parent.width - 26; spacing: 5
                            SwitchRow { configuration: page.values; onEdited: page.setValue(key, value); label: "启用RTK"; labelWidth: 78; configKey: "rtkEnabled"; height: 30 }
                            Row {
                                width: parent.width; height: 31
                                FormLabel { width: 77; text: "连接方式" }
                                ChoiceField { width: 107; model: ["网络RTK", "串口RTK"] }
                                FormLabel { width: 77; text: "基站地址"; horizontalAlignment: Text.AlignHCenter }
                                ValueField { width: parent.width - 261; text: page.values.rtkHost; font.pixelSize: 11; leftPadding: 5; rightPadding: 3; onTextEdited: page.setValue("rtkHost", text) }
                            }
                            Row {
                                width: parent.width; height: 31
                                FormLabel { width: 77; text: "端口" }
                                ValueField { width: 107; text: page.values.rtkPort; onTextEdited: page.setValue("rtkPort", text) }
                                FormLabel { width: 77; text: "账号"; horizontalAlignment: Text.AlignHCenter }
                                ValueField { width: parent.width - 261; text: page.values.rtkAccount; onTextEdited: page.setValue("rtkAccount", text) }
                            }
                            Row {
                                width: parent.width; height: 31
                                FormLabel { width: 77; text: "差分格式" }
                                ChoiceField { width: 107; model: ["RTCM 3.2", "RTCM 3.0"] }
                                FormLabel { width: 77; text: "密码"; horizontalAlignment: Text.AlignHCenter }
                                ValueField { width: parent.width - 261; text: page.rtkPassword; echoMode: TextInput.Password; onTextEdited: page.rtkPassword = text }
                            }
                            Row {
                                width: parent.width; height: 33
                                FormLabel { width: 77; text: "挂载点" }
                                ValueField { width: parent.width - 192; text: page.values.rtkMount; onTextEdited: page.setValue("rtkMount", text) }
                                Item { width: 13; height: 1 }
                                GButton { width: 102; height: 33; text: "测试连接"; fill: "#087dff"; onClicked: page.notify("演示模式：尚未接入RTK服务，未执行连接测试") }
                            }
                        }
                    }
                    GCard {
                        width: parent.width - lowerRow.rtkWidth - lowerRow.spacing; height: parent.height; title: "时间与时区"
                        Column {
                            x: 14; y: 9; width: parent.width - 28; spacing: 9
                            Row {
                                width: parent.width; height: 31
                                FormLabel { width: 82; text: "设备时间" }
                                ValueField { width: parent.width - 219; text: page.values.deviceTime; onTextEdited: page.setValue("deviceTime", text) }
                                Item { width: 16; height: 1 }
                                GButton { width: 121; height: 31; text: "同步本机时间"; onClicked: { page.setValue("deviceTime", Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss")); page.notify("本机时间已填入界面；未修改设备或操作系统时钟") } }
                            }
                            Row { width: parent.width; height: 31; FormLabel { width: 82; text: "时区" } ChoiceField { width: parent.width - 82; model: ["(UTC+08:00) 北京、重庆、香港、乌鲁木齐", "(UTC+00:00) 协调世界时"] } }
                            Row { width: parent.width; height: 30; StatusDot { width: 166; anchors.verticalCenter: parent.verticalCenter; text: "自动校时（NTP）"; textColor: "#cbdbea" } Toggle { anchors.verticalCenter: parent.verticalCenter; checked: page.values.ntpEnabled; onToggled: page.setValue("ntpEnabled", checked) } }
                            Row { width: parent.width; height: 31; FormLabel { width: 82; text: "NTP服务器" } ValueField { width: 265; text: page.values.ntpServer; onTextEdited: page.setValue("ntpServer", text) } }
                        }
                    }
                }
                Row {
                    width: parent.width; height: 240; spacing: 8
                    GCard {
                        width: parent.width; height: parent.height; title: "其他设置"
                        Column {
                            x: 20; y: 8; width: parent.width - 40; spacing: 4
                            SwitchRow { configuration: page.values; onEdited: page.setValue(key, value); label: "开机自启动"; labelWidth: 113; configKey: "autoStart" }
                            SwitchRow { configuration: page.values; onEdited: page.setValue(key, value); label: "异常自动重连"; labelWidth: 113; configKey: "autoReconnect" }
                            SwitchRow { configuration: page.values; onEdited: page.setValue(key, value); label: "连接失败告警"; labelWidth: 113; configKey: "connectionAlarm" }
                            Row { width: parent.width; height: 31; FormLabel { width: 113; text: "重连间隔（秒）" } ValueField { width: 102; text: page.values.reconnectInterval; validator: IntValidator { bottom: 1; top: 3600 } onTextEdited: page.setValue("reconnectInterval", text) } }
                            GButton { anchors.right: parent.right; width: 100; height: 35; text: "保存设置"; fill: "#087dff"; onClicked: page.save() }
                        }
                    }
                }

            }
        }
    }

    Component {
        id: configurationSettings
        // 同样包一层 Flickable：字段多的分页（如"无人机设置"6 行）在窗口矮时
        // 底部会被切掉，加滚动后可查看
        Flickable {
            id: confFlick
            contentWidth: width
            contentHeight: Math.max(height, confCol.y + confCol.height + 20)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            GCard {
            id: configurationCard
            width: confFlick.width
            height: confFlick.contentHeight
            title: page.sectionNames[page.sectionIndex]
            property var sectionFields: [[],
                [["设备名称", "aircraftName"], ["无人机型号", "aircraftModel"], ["飞控类型", "autopilot"], ["飞控串口", "serialPort"], ["波特率", "baudRate"], ["返航高度（m）", "returnHeight"]],
                [["机巢名称", "nestName"], ["机巢型号", "nestModel"], ["机巢经度", "nestLongitude"], ["机巢纬度", "nestLatitude"], ["充电目标（%）", "nestChargeThreshold"], ["通风温度（℃）", "nestFanTemperature"]],
                [["飞行高度（m）", "flightHeight"], ["巡航速度（m/s）", "flightSpeed"], ["航点停留（秒）", "waypointHold"], ["拍摄间隔（秒）", "photoInterval"], ["航线间距（m）", "routeSpacing"]],
                [["离线瓦片目录", "tileDirectory"], ["瓦片格式（XYZ / TMS）", "tileFormat"]],
                [["低电量告警（%）", "lowBattery"], ["严重低电量（%）", "criticalBattery"], ["风速上限（m/s）", "windLimit"], ["失联超时（秒）", "lostLinkTimeout"], ["飞行高度上限（m）", "maxFlightHeight"]],
                [["数据保存目录", "dataDirectory"], ["日志保留（天）", "logDays"], ["录像保存目录", "videoDirectory"], ["照片保存目录", "imageDirectory"]],
                [["终端名称", "deviceName"], ["界面缩放（%）", "uiScale"], ["屏幕亮度（%）", "brightness"], ["显示语言", "language"]]
            ]
            Column {
                id: confCol
                x: 24; y: 20; width: parent.width - 48; spacing: 15
                Text { text: page.sectionNames[page.sectionIndex] + "参数"; color: "#e6f0fb"; font.pixelSize: 18; font.bold: true }
                Text { width: parent.width; text: "本地配置 · 演示模式"; color: "#7898b3"; font.pixelSize: 13 }
                Rectangle { width: parent.width; height: 1; color: "#17374e" }
                Repeater {
                    model: configurationCard.sectionFields[page.sectionIndex]
                    delegate: FieldRow { configuration: page.values; onEdited: page.setValue(key, value); width: parent.width; label: modelData[0]; configKey: modelData[1]; labelWidth: 176; height: 39 }
                }
                Text { visible: page.sectionIndex === 4; height: visible ? implicitHeight : 0; width: parent.width; text: offlineMapSource.status + "\n地图支持缩放、平移；任务页可点击新增及拖动航点。"; wrapMode: Text.WordWrap; color: "#a8c7dd"; font.pixelSize: 13 }
                Text { visible: page.sectionIndex === 2; height: visible ? implicitHeight : 0; width: parent.width; text: "WGS84 经纬度 · " + (nestPosition.configured ? "已保存手动位置（非实时定位）" : "未设置真实位置，当前为地图中心示例") + "\n保存本页后地图标记立即更新；不会向机巢发送指令。"; wrapMode: Text.WordWrap; color: "#a8c7dd"; font.pixelSize: 13 }
                SwitchRow { configuration: page.values; onEdited: page.setValue(key, value);
                    visible: page.sectionIndex >= 3 && page.sectionIndex !== 4; height: visible ? 30 : 0; labelWidth: 176
                    label: ["", "", "", "任务完成后返航", "显示航线", "启用电子围栏", "自动录像", "触摸操作"][page.sectionIndex]
                    configKey: ["", "", "", "missionReturn", "showRoute", "geofenceEnabled", "recordingEnabled", "touchEnabled"][page.sectionIndex]
                }
                Rectangle { width: parent.width; height: 1; color: "#17374e" }
                Row {
                    width: parent.width; spacing: 14
                    GButton { width: 130; height: 39; text: "保存设置"; fill: "#087dff"; onClicked: page.save() }
                    Text { height: 39; text: "设置保存在本机，未向无人机或机巢下发"; color: "#7898b3"; font.pixelSize: 13; verticalAlignment: Text.AlignVCenter }
                }
            }
            }
        }
    }
    Rectangle {
        id: toast; z: 20; width: Math.min(page.width - 80, toastText.implicitWidth + 42); height: 44; radius: 5; color: "#f20a273d"; border.color: "#087dff"; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 25; opacity: 0
        Text { id: toastText; anchors.centerIn: parent; color: "white"; font.pixelSize: 14 }
        Timer { id: toastTimer; interval: 2800; onTriggered: toast.opacity = 0 }
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }
}
