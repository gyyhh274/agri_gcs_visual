import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt.labs.settings 1.1

Flickable {
    id: panel
    objectName: "onboardModulePanel"
    property var manager: null
    property var selection: ({flight:false, gimbal:false, yolo:false, auto_record:false, save_video:false, rviz:false})
    property string notice: "连接仅建立管理通道；勾选不会启动程序。"
    property string logModule: "flight"
    readonly property bool online: manager !== null && manager.connected
    readonly property bool working: manager !== null && manager.busy
    contentWidth: width
    contentHeight: body.height + 24
    clip: true
    ScrollBar.vertical: ScrollBar {}
    Settings { id: store; category: "OnboardModulesV1"; property string selectedJson: ""; property string host: "192.168.2.112"; property string user: "ly"; property string workspace: "/home/ly/Documents/diff-planner-A8miniV2" }
    Component.onCompleted: {
        if (store.selectedJson.length) {
            try {
                var saved = JSON.parse(store.selectedJson)
                for (var key in selection) if (typeof saved[key] !== "boolean") throw new Error("invalid selection")
                selection = normalized(saved)
            } catch (e) { notice = "模块配置无效，已恢复为仅连接。" }
        }
    }
    function normalized(value) {
        var next = {flight:!!value.flight, gimbal:!!value.gimbal, yolo:!!value.yolo, auto_record:!!value.auto_record, save_video:!!value.save_video, rviz:!!value.rviz}
        if (next.auto_record) next.gimbal = true
        if (next.save_video) next.yolo = true
        return next
    }
    function choose(key, checked) {
        var next = Object.assign({}, selection)
        next[key] = checked
        if (!checked && key === "gimbal") { next.auto_record = false; notice = "已同时取消起飞自动录像。" }
        else if (!checked && key === "yolo") { next.save_video = false; notice = "已同时取消识别视频保存。" }
        else if (checked && key === "auto_record") notice = "已带上云台控制；未启动飞行栈时，录像等待飞行状态。"
        else if (checked && key === "save_video") notice = "已带上 YOLO 识别。"
        else notice = "选择已保存；点击启动后才会运行。取消勾选不会停止正在运行的模块。"
        selection = normalized(next)
        store.selectedJson = JSON.stringify(selection); store.sync()
    }
    function preset(index) {
        selection = normalized({flight:index === 1 || index === 3, gimbal:index === 2 || index === 3, yolo:index === 2 || index === 3})
        store.selectedJson = JSON.stringify(selection); store.sync()
        notice = "预设已选择；点击启动后才会运行。"
    }
    function title(name) { return ({flight:"飞行栈", gimbal:"云台控制", yolo:"YOLO 识别", rviz:"RViz 调试", roscore:"基础 ROS"})[name] || name }
    function stateText(row) {
        if (!online) return "状态未知，请连接查询"
        return ({stopped:"未启动", starting:"启动中", running:"节点运行", stopping:"正在收尾", degraded:"节点未齐，查看日志", external:"外部启动，不接管", exited:"进程退出（" + row.exit_code + "）"})[row.state] || "状态未知"
    }
    Column {
        id: body
        x: 12; y: 12; width: parent.width - 24; spacing: 12
        Text { text: "机载程序 · 按需启动"; color: "#eef7ff"; font.pixelSize: 21; font.bold: true }
        Text { width: parent.width; wrapMode: Text.WordWrap; color: "#9bbbd2"; text: "飞行栈、云台与识别分别管理。启动程序不会解锁、起飞或执行航线。" }
        Row { spacing: 8; Text { text: "主机"; color: "#a9c7dd"; width: 68; anchors.verticalCenter: parent.verticalCenter } GField { id: hostField; width: Math.max(160, body.width - 80); text: store.host; enabled: !panel.online && !panel.working } }
        Row { spacing: 8; Text { text: "SSH 用户"; color: "#a9c7dd"; width: 68; anchors.verticalCenter: parent.verticalCenter } GField { id: userField; width: Math.max(160, body.width - 80); text: store.user; enabled: !panel.online && !panel.working } }
        Text { text: "机载工程目录"; color: "#a9c7dd" }
        GField { id: workspaceField; width: parent.width; text: store.workspace; enabled: !panel.online && !panel.working }
        Row {
            spacing: 8
            GButton { text: panel.online ? "断开管理" : "连接机载"; width: 110; height: 34; enabled: panel.manager !== null && !panel.working
                onClicked: {
                    if (panel.online) panel.manager.disconnectHost()
                    else { store.host = hostField.text.trim(); store.user = userField.text.trim(); store.workspace = workspaceField.text.trim(); store.sync(); panel.manager.connectHost(store.host, store.user, store.workspace) }
                }
            }
            GButton { text: "刷新状态"; width: 100; height: 34; enabled: panel.online && !panel.working; onClicked: panel.manager.refresh() }
        }
        Text { width: parent.width; wrapMode: Text.WrapAnywhere; color: panel.online ? "#39d898" : "#ffbf66"; text: panel.manager ? panel.manager.message : "预览模式，未启用机载管理" }
        Text { text: "本次启动功能"; color: "#eef7ff"; font.pixelSize: 18; font.bold: true }
        Flow {
            width: parent.width; spacing: 6
            Repeater { model: ["仅连接", "仅航点飞行", "地面识别调试", "完整巡检"]
                GButton { width: 104; height: 32; text: modelData; enabled: !panel.working; onClicked: panel.preset(index) }
            }
        }
        Repeater {
            model: [
                {key:"flight", label:"飞行栈", detail:"飞控、雷达定位、规划、控制和任务网关；仍需单独触发起飞。"},
                {key:"gimbal", label:"云台控制", detail:"接收云台动作；启动节点不会自动旋转，也不依赖飞行栈。"},
                {key:"yolo", label:"YOLO 识别", detail:"需要相机视频流和模型；后台不弹识别窗口。"},
                {key:"auto_record", label:"起飞自动录像", detail:"依赖云台；等待有效飞行状态，原始录像保存到相机卡。"},
                {key:"save_video", label:"保存识别视频", detail:"依赖 YOLO；带检测框的视频保存在机载电脑。"},
                {key:"rviz", label:"RViz 调试", detail:"需要图形桌面；独立运行，退出不停止飞行栈。"}]
            Column {
                width: body.width; spacing: 2
                CheckBox { text: modelData.label; checked: !!panel.selection[modelData.key]; enabled: !panel.working
                    onClicked: panel.choose(modelData.key, checked)
                    contentItem: Text { text: parent.text; color: "#e0edf9"; font.pixelSize: 15; leftPadding: parent.indicator.width + parent.spacing; verticalAlignment: Text.AlignVCenter }
                }
                Text { x: 30; width: parent.width - 30; text: modelData.detail; color: "#8faec5"; wrapMode: Text.WordWrap; font.pixelSize: 12 }
            }
        }
        Text { width: parent.width; color: "#ffbf66"; wrapMode: Text.WordWrap; text: panel.notice }
        GButton { objectName: "startSelectedModules"; width: 160; height: 38; text: panel.working ? "处理中…" : "启动所选模块"; fill: "#087dff"; enabled: panel.online && !panel.working && (panel.selection.flight || panel.selection.gimbal || panel.selection.yolo || panel.selection.rviz); onClicked: panel.manager.startSelected(panel.selection) }
        Text { width: parent.width; color: "#8faec5"; wrapMode: Text.WordWrap; text: "已运行模块不会被重复启动。改变录像选项需要先停止对应云台或识别模块，再重新启动。" }
        Repeater {
            model: panel.manager ? panel.manager.modules : []
            Column {
                width: body.width; spacing: 5
                Text { width: parent.width; color: "#dceaff"; text: panel.title(modelData.name) + " · " + panel.stateText(modelData); wrapMode: Text.WordWrap }
                Text { visible: modelData.missing && modelData.missing.length > 0; width: parent.width; wrapMode: Text.WrapAnywhere; color: "#ffbf66"; font.pixelSize: 12; text: visible ? "等待节点：" + modelData.missing.join(", ") : "" }
                Text { visible: modelData.name === "gimbal" && modelData.recording && modelData.recording !== "disabled"; width: parent.width; color: "#a9c7dd"; wrapMode: Text.WordWrap; text: modelData.recording === "waiting_flight_state" ? "自动录像：等待飞行状态" : "自动录像：监听飞行事件；实际录像状态见相机与日志" }
                Row {
                    spacing: 8
                    GButton { text: "查看日志"; width: 92; height: 30; enabled: panel.online && !panel.working; onClicked: { panel.logModule = modelData.name; panel.manager.readLogs(modelData.name) } }
                    GButton { text: "停止"; width: 76; height: 30; enabled: panel.online && !panel.working && ["starting", "running", "degraded"].indexOf(modelData.state) >= 0; onClicked: panel.manager.stopModule(modelData.name) }
                }
            }
        }
        Text { width: parent.width; text: "节点运行仅表示进程／节点存在；飞行就绪看任务遥测，图像和模型状态看对应日志。"; color: "#8faec5"; wrapMode: Text.WordWrap }
        GButton { text: "基础 ROS 日志"; width: 130; height: 30; enabled: panel.online && !panel.working; onClicked: { panel.logModule = "roscore"; panel.manager.readLogs("roscore") } }
        Text { text: panel.title(panel.logModule) + " · 最近日志"; color: "#eef7ff"; font.pixelSize: 16 }
        Rectangle {
            width: parent.width; height: 260; color: "#04111c"; border.color: "#17394f"
            ScrollView { anchors.fill: parent; anchors.margins: 8
                TextArea { text: panel.manager ? panel.manager.logs : ""; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; color: "#c2d9e9"; font.pixelSize: 12; background: null }
            }
        }
    }
}
