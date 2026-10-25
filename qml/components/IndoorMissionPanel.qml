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
            Text { text: "自动起降 · 单点往返"; color: "white"; font.pixelSize: 15; font.bold: true }
            Text { width: parent.width; text: control.mission.phase; color: "#82e8d1"; font.pixelSize: 13; wrapMode: Text.WordWrap }
            Text { width: parent.width; text: control.mission.readiness; color: control.mission.canRecord ? "#6fd9b9" : "#e8bb76"; font.pixelSize: 12; wrapMode: Text.WordWrap }
            GButton { width: parent.width; height: 34; text: "① 确认当前位置为起降点"; enabled: control.mission.canRecord; onClicked: control.mission.recordHome() }
            Text { width: parent.width; text: control.mission.homeValid ? "起降点: " + control.mission.groundOrigin.x.toFixed(2) + ", " + control.mission.groundOrigin.y.toFixed(2) + ", " + control.mission.groundOrigin.z.toFixed(2) + " m\n自动起飞上升 " + (control.mission.home.z-control.mission.groundOrigin.z).toFixed(2) + " m，返航高度 Z=" + control.mission.home.z.toFixed(2) : "在地面保持静止、飞控未解锁，确认本次起降位置。"; color: "#b7cedb"; font.pixelSize: 12; wrapMode: Text.WordWrap }
            Row {
                spacing: 8
                Text { text: "目标高度 Z"; color: "#cfe0ef"; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 12 }
                TextField { id: heightField; width: 88; height: 32; text: control.mission.height.toFixed(2); color: "#dce9f7"; font.pixelSize: 13; background: Rectangle { color: "#061522"; radius: 4; border.color: "#16405e" } enabled: control.mission.homeValid && control.mission.canEdit; onEditingFinished: control.mission.setHeight(Number(text)) }
                Text { text: "m · world"; color: "#9dbac8"; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 11 }
            }
            GButton { width: parent.width; height: 34; text: "② 俯视图选择目标 P"; enabled: control.mission.homeValid && control.mission.contextReady && control.mission.canEdit; onClicked: control.selectTarget() }
            Row {
                width: parent.width; spacing: 6
                TextField { id: targetXField; width: (parent.width-92)/2; height: 32; placeholderText: "目标 X"; color: "#dce9f7"; font.pixelSize: 12; selectByMouse: true; enabled: control.mission.homeValid && control.mission.canEdit; background: Rectangle { color: "#061522"; radius: 4; border.color: "#16405e" } }
                TextField { id: targetYField; width: (parent.width-92)/2; height: 32; placeholderText: "目标 Y"; color: "#dce9f7"; font.pixelSize: 12; selectByMouse: true; enabled: control.mission.homeValid && control.mission.canEdit; background: Rectangle { color: "#061522"; radius: 4; border.color: "#16405e" } }
                GButton { width: 80; height: 32; text: "应用坐标"; enabled: control.mission.homeValid && control.mission.contextReady && control.mission.canEdit && targetXField.text.trim().length > 0 && targetYField.text.trim().length > 0 && isFinite(Number(targetXField.text)) && isFinite(Number(targetYField.text)); onClicked: control.mission.setTarget(Number(targetXField.text), Number(targetYField.text)) }
            }
            Text { width: parent.width; text: control.mission.hasTarget ? "P: " + control.mission.target.x.toFixed(2) + ", " + control.mission.target.y.toFixed(2) + ", " + control.mission.target.z.toFixed(2) + " m" : "点击地图空闲位置选择 XY，高度单独设置。"; color: "#b7cedb"; font.pixelSize: 12; wrapMode: Text.WordWrap }
            Row {
                spacing: 8
                Text { text: "目标悬停"; color: "#cfe0ef"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                GField { id: hoverField; width: 88; height: 32; text: "2"; enabled: control.mission.canEdit }
                Text { text: "秒 · 0–60"; color: "#9dbac8"; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
            }
            Text { width: parent.width; text: "任务：自动起飞 → 前往 P → 悬停 → 返回起飞点 → 自动降落并锁定电机\n图上连线为参考，不代表实际避障轨迹。"; color: "#a9c3d0"; font.pixelSize: 12; wrapMode: Text.WordWrap }
            GButton { width: parent.width; height: 34; text: "③ 上传自动往返任务"; enabled: control.mission.canUpload && isFinite(Number(hoverField.text)) && Number(hoverField.text) >= 0 && Number(hoverField.text) <= 60; onClicked: control.mission.upload(Number(hoverField.text)) }
            GButton { objectName: "roundTripStartButton"; width: parent.width; height: 34; text: "④ 自动起飞并执行…"; fill: "#116282"; enabled: control.mission.canStart; onClicked: startDialog.open() }
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
        function onChanged() {
            if (!heightField.activeFocus) heightField.text = control.mission.height.toFixed(2)
            if (!control.mission.homeValid) { targetXField.clear(); targetYField.clear() }
        }
    }
    Dialog {
        id: startDialog
        parent: Overlay.overlay; width: 410; height: 310
        x: (parent.width-width)/2; y: (parent.height-height)/2; modal: true
        background: Rectangle { color: "#0a2234"; border.color: "#28617b"; radius: 6 }
        contentItem: Column {
            spacing: 18
            Text { text: "确认自动起飞并往返降落"; color: "white"; font.pixelSize: 18 }
            Text { width: parent.width; text: "无人机将自动解锁并起飞，前往 P，悬停后返回当前起降点，自动降落并锁定电机。\n请确认起降区域空旷、目标及高度正确，遥控器可随时接管。上传任务不会起飞；点击下方确认才开始。"; color: "#cfe0ef"; wrapMode: Text.WordWrap; font.pixelSize: 13 }
            Row {
                spacing: 12
                GButton { width: 140; height: 35; text: "确认自动起飞"; enabled: control.mission.canStart; onClicked: { control.mission.start(); startDialog.close() } }
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
            Text { width: parent.width; text: "停止自动任务及起降流程，不继续返程或自动降落。请观察机体并准备遥控器接管。终止后不能直接恢复本次任务。"; color: "#cfe0ef"; wrapMode: Text.WordWrap; font.pixelSize: 13 }
            Row {
                spacing: 12
                GButton { width: 140; height: 35; text: "确认终止"; enabled: control.mission.canCancel; onClicked: { control.mission.cancel(); cancelDialog.close() } }
                GButton { width: 100; height: 35; text: "保留任务"; onClicked: cancelDialog.close() }
            }
        }
    }
}
