import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: control
    property var mission
    signal selectTarget()
    color: "#ee0a2234"; border.color: "#28617b"; radius: 6
    clip: true
    Flickable {
        anchors.fill: parent; anchors.margins: 12
        contentHeight: form.height; clip: true
        ScrollBar.vertical: ScrollBar { }
        Column {
            id: form; width: parent.width; spacing: 10
            Text { text: "地图单点往返"; color: "white"; font.pixelSize: 17; font.bold: true }
            Text { width: parent.width; text: control.mission.phase; color: "#82e8d1"; font.pixelSize: 13; wrapMode: Text.WordWrap }
            Text { width: parent.width; text: control.mission.contextReady ? "本次定位坐标与机载围栏已核对" : "等待任务网关、定位坐标核对及场地围栏"; color: control.mission.contextReady ? "#6fd9b9" : "#e8bb76"; font.pixelSize: 12; wrapMode: Text.WordWrap }
            GButton { width: parent.width; height: 34; text: "① 确认起始悬停点 R"; enabled: control.mission.canRecord; onClicked: control.mission.recordHome() }
            Text { width: parent.width; text: control.mission.homeValid ? "R: " + control.mission.home.x.toFixed(2) + ", " + control.mission.home.y.toFixed(2) + ", " + control.mission.home.z.toFixed(2) + " m" : "人工起飞并稳定悬停后，记录本次返回位置。"; color: "#b7cedb"; font.pixelSize: 12; wrapMode: Text.WordWrap }
            Row {
                spacing: 8
                Text { text: "目标高度 Z"; color: "#cfe0ef"; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 12 }
                TextField { id: heightField; width: 88; height: 32; text: control.mission.height.toFixed(2); color: "#dce9f7"; font.pixelSize: 13; background: Rectangle { color: "#061522"; radius: 4; border.color: "#16405e" } enabled: control.mission.homeValid && control.mission.canEdit; onEditingFinished: control.mission.setHeight(Number(text)) }
                Text { text: "m · world"; color: "#9dbac8"; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 11 }
            }
            GButton { width: parent.width; height: 34; text: "② 俯视图选择目标 P"; enabled: control.mission.homeValid && control.mission.contextReady && control.mission.canEdit; onClicked: control.selectTarget() }
            Text { width: parent.width; text: control.mission.hasTarget ? "P: " + control.mission.target.x.toFixed(2) + ", " + control.mission.target.y.toFixed(2) + ", " + control.mission.target.z.toFixed(2) + " m" : "点击地图空闲位置选择 XY，高度单独设置。"; color: "#b7cedb"; font.pixelSize: 12; wrapMode: Text.WordWrap }
            Row {
                spacing: 8
                Text { text: "目标悬停"; color: "#cfe0ef"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                GField { id: hoverField; width: 88; height: 32; text: "2"; enabled: control.mission.canEdit }
                Text { text: "秒 · 0–60"; color: "#9dbac8"; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
            }
            Text { width: parent.width; text: "任务：前往 P → 悬停 → 返回 R → 保持悬停\n图上连线为参考，不代表实际避障轨迹。"; color: "#a9c3d0"; font.pixelSize: 12; wrapMode: Text.WordWrap }
            GButton { width: parent.width; height: 34; text: "③ 上传往返任务"; enabled: control.mission.canUpload && isFinite(Number(hoverField.text)) && Number(hoverField.text) >= 0 && Number(hoverField.text) <= 60; onClicked: control.mission.upload(Number(hoverField.text)) }
            GButton { objectName: "roundTripStartButton"; width: parent.width; height: 34; text: "④ 开始执行…"; fill: "#116282"; enabled: control.mission.canStart; onClicked: startDialog.open() }
            Row {
                spacing: 8
                GButton { width: 124; height: 32; text: "查询任务状态"; enabled: groundLink.connected && !groundLink.busy; onClicked: groundLink.queryMission() }
                GButton { width: 124; height: 32; text: "终止任务…"; fill: "#804637"; enabled: control.mission.canCancel; onClicked: cancelDialog.open() }
            }
            Text { width: parent.width; text: control.mission.status; color: "#ecce9a"; font.pixelSize: 12; wrapMode: Text.WordWrap }
        }
    }
    Connections {
        target: control.mission
        function onChanged() { if (!heightField.activeFocus) heightField.text = control.mission.height.toFixed(2) }
    }
    Dialog {
        id: startDialog
        parent: Overlay.overlay; width: 370; height: 250
        x: (parent.width-width)/2; y: (parent.height-height)/2; modal: true
        background: Rectangle { color: "#0a2234"; border.color: "#28617b"; radius: 6 }
        contentItem: Column {
            spacing: 18
            Text { text: "确认开始往返任务"; color: "white"; font.pixelSize: 18 }
            Text { width: parent.width; text: "无人机将前往地图标出的 P，悬停后返回 R。请确认目标、高度和返回点正确，机体正在稳定悬停。\n完成后保持悬停，降落由你操作。"; color: "#cfe0ef"; wrapMode: Text.WordWrap; font.pixelSize: 13 }
            Row {
                spacing: 12
                GButton { width: 140; height: 35; text: "确认开始"; enabled: control.mission.canStart; onClicked: { control.mission.start(); startDialog.close() } }
                GButton { width: 100; height: 35; text: "取消"; onClicked: startDialog.close() }
            }
        }
    }
    Dialog {
        id: cancelDialog
        parent: Overlay.overlay; width: 370; height: 215
        x: (parent.width-width)/2; y: (parent.height-height)/2; modal: true
        background: Rectangle { color: "#0a2234"; border.color: "#28617b"; radius: 6 }
        contentItem: Column {
            spacing: 18
            Text { text: "终止当前任务"; color: "white"; font.pixelSize: 18 }
            Text { width: parent.width; text: "停止任务及规划轨迹，不自动降落或继续返程。请观察机体并准备遥控器接管。终止后不能直接恢复本次任务。"; color: "#cfe0ef"; wrapMode: Text.WordWrap; font.pixelSize: 13 }
            Row {
                spacing: 12
                GButton { width: 140; height: 35; text: "确认终止"; enabled: control.mission.canCancel; onClicked: { control.mission.cancel(); cancelDialog.close() } }
                GButton { width: 100; height: 35; text: "保留任务"; onClicked: cancelDialog.close() }
            }
        }
    }
}
