import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: control
    property var mission
    property bool selectingTarget: false
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
            GButton { objectName: "roundTripInitializeButton"; width: parent.width; height: 34; text: "① 任务初始化…"; fill: "#2f6f8f"; enabled: control.mission.canInitialize; onClicked: cancelDialog.open() }
            GButton {
                objectName: "roundTripPickTargetButton"; width: parent.width; height: 34
                text: control.selectingTarget ? "② 选点中，点击地图范围内" : "② 选择目标点"
                checked: control.selectingTarget
                enabled: control.mission.homeValid ? control.mission.contextReady && control.mission.canEdit : control.mission.canRecord
                onClicked: {
                    if (control.mission.homeValid) control.selectTarget()
                    else homeDialog.open()
                }
            }
            GButton { objectName: "roundTripUploadButton"; width: parent.width; height: 34; text: "③ 上传任务"; enabled: control.mission.canUpload && isFinite(Number(hoverField.text)) && Number(hoverField.text) >= 0 && Number(hoverField.text) <= 60; onClicked: control.mission.upload(Number(hoverField.text)) }
            GButton { objectName: "roundTripStartButton"; width: parent.width; height: 34; text: "④ 执行飞行任务…"; fill: "#116282"; enabled: control.mission.canStart; onClicked: startDialog.open() }
            Text { width: parent.width; text: control.mission.homeValid ? "起降点: " + control.mission.groundOrigin.x.toFixed(2) + ", " + control.mission.groundOrigin.y.toFixed(2) + ", " + control.mission.groundOrigin.z.toFixed(2) + " m\n自动起飞上升 " + (control.mission.home.z-control.mission.groundOrigin.z).toFixed(2) + " m，返航高度 Z=" + control.mission.home.z.toFixed(2) : "在地面保持静止、飞控未解锁，确认本次起降位置。"; color: "#b7cedb"; font.pixelSize: 12; wrapMode: Text.WordWrap }
            Row {
                spacing: 8
                Text { text: "目标高度 Z"; color: "#cfe0ef"; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 12 }
                TextField { id: heightField; width: 88; height: 32; text: control.mission.height.toFixed(2); color: "#dce9f7"; font.pixelSize: 13; background: Rectangle { color: "#061522"; radius: 4; border.color: "#16405e" } enabled: control.mission.homeValid && control.mission.canEdit; onEditingFinished: control.mission.setHeight(Number(text)) }
                Text { text: "m · world"; color: "#9dbac8"; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 11 }
            }
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
        id: homeDialog
        objectName: "roundTripHomeDialog"
        parent: Overlay.overlay; width: 410; height: 250
        x: (parent.width-width)/2; y: (parent.height-height)/2; modal: true
        background: Rectangle { color: "#0a2234"; border.color: "#28617b"; radius: 6 }
        contentItem: Column {
            spacing: 16
            Text { text: "确认本次起降点"; color: "white"; font.pixelSize: 18 }
            Text { width: parent.width; text: "将当前位置设为起降点，选点后飞机会返回此处降落。请确认飞机在地面、飞控未解锁，且起降区域空旷。"; color: "#cfe0ef"; font.pixelSize: 13; wrapMode: Text.WordWrap }
            Text { width: parent.width; text: "当前位置：(" + groundLink.worldX.toFixed(2) + ", " + groundLink.worldY.toFixed(2) + ", " + groundLink.worldZ.toFixed(2) + ") m · world"; color: "#8fb6cc"; font.pixelSize: 12; wrapMode: Text.WordWrap }
            Row {
                spacing: 12
                GButton {
                    objectName: "roundTripConfirmHomeButton"; width: 180; height: 35; text: "确认起降点并选目标"
                    enabled: control.mission.canRecord
                    onClicked: {
                        if (!control.mission.canRecord) return
                        control.mission.recordHome()
                        if (control.mission.homeValid) { homeDialog.close(); control.selectTarget() }
                    }
                }
                GButton { width: 100; height: 35; text: "取消"; onClicked: homeDialog.close() }
            }
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
        objectName: "roundTripInitializeDialog"
        parent: Overlay.overlay; width: 460; height: groundLink.connected ? 500 : 535
        x: (parent.width-width)/2; y: (parent.height-height)/2; modal: true
        /* 6 个围栏值，顺序与协议一致：xmin, ymin, zmin, xmax, ymax, zmax。
         * 输入框不用 text 绑定（绑定会和用户键入互相打架，导致"改不动"），
         * 改成打开时按值写入、编辑完成后再回写。 */
        property var fence: [0, 0, 0, 0, 0, 0]
        property var fields: []
        property var center: ({ valid: false })
        property bool hasFencePreview: false
        property real previousCenterX: 0
        property real previousCenterY: 0
        property real previousCenterZ: 0
        property int lastEditedIndex: -1
        property bool editValid: true
        onOpened: {
            center = { valid: false }
            hasFencePreview = false
            lastEditedIndex = -1
            editValid = true
            refreshFence()
        }
        function refreshFence() {
            if (hasFencePreview && lastEditedIndex >= 0) {
                const index = lastEditedIndex
                if (!commit(index, fields[index].text)) return
                lastEditedIndex = -1
            }
            const pos = control.mission.initializationPosition()
            if (!pos.valid) {
                center = { valid: false }
                fenceHint.text = "当前位置未更新，请连接机载或等待 LIVE 地图定位，再刷新位置"
                return
            }
            // 每次打开使用 1 米立方体；同一次编辑内刷新位置时保留各边的偏移。
            let offsets = [-0.5, -0.5, -0.05, 0.5, 0.5, 0.95]
            if (hasFencePreview) {
                offsets = [fence[0]-previousCenterX, fence[1]-previousCenterY, fence[2]-previousCenterZ,
                           fence[3]-previousCenterX, fence[4]-previousCenterY, fence[5]-previousCenterZ]
            }
            center = pos
            previousCenterX = pos.x; previousCenterY = pos.y; previousCenterZ = pos.z
            hasFencePreview = true
            lastEditedIndex = -1
            editValid = true
            fence = [pos.x+offsets[0], pos.y+offsets[1], pos.z+offsets[2],
                     pos.x+offsets[3], pos.y+offsets[4], pos.z+offsets[5]]
            fenceHint.text = ""
            Qt.callLater(function () {
                for (let i = 0; i < 6; ++i)
                    if (cancelDialog.fields[i])
                        cancelDialog.fields[i].text = Number(cancelDialog.fence[i]).toFixed(2)
            })
        }
        /* 每个边界独立编辑，保留用户输入的数值。 */
        function commit(slot, raw) {
            const value = raw.trim().length ? Number(raw) : NaN
            const next = fence.slice()
            let error = ""
            if (!isFinite(value)) error = "请输入有效数字"
            else {
                next[slot] = value
                if (!(next[0] < next[3] && next[1] < next[4] && next[2] < next[5]))
                    error = "每个轴的下限必须小于上限"
            }
            editValid = !error
            if (!error) fence = next
            for (let i = 0; i < 6; ++i)
                if (fields[i]) fields[i].text = Number(fence[i]).toFixed(2)
            fenceHint.text = error
            return !error
        }
        background: Rectangle { color: "#0a2234"; border.color: "#28617b"; radius: 6 }
        contentItem: Column {
            spacing: 10
            Text { text: "任务初始化"; color: "white"; font.pixelSize: 18 }
            Text { width: parent.width
                   text: groundLink.mission.init_phase === "awaiting_calibration"
                       ? "服务已重启。请刷新新地图位置，在新坐标系内重新确认围栏；起降点也需重新标定。"
                       : "第一次确认会重启机载服务并重建地图。服务重启后，请再次打开初始化，刷新位置并确认新围栏。仅在地面、飞控未解锁时可用。"
                   color: "#cfe0ef"; wrapMode: Text.WordWrap; font.pixelSize: 12 }

            Text { width: parent.width
                   text: "本次围栏（world 坐标，m）：各边界可单独修改。"
                   color: "#8fb6cc"; font.pixelSize: 11; wrapMode: Text.WordWrap }
            Grid {
                id: fenceGrid
                columns: 2; spacing: 6; width: parent.width
                Repeater {
                    model: [
                        { label: "X 下限", index: 0 }, { label: "X 上限", index: 3 },
                        { label: "Y 下限", index: 1 }, { label: "Y 上限", index: 4 },
                        { label: "Z 最低", index: 2 }, { label: "Z 最高", index: 5 }
                    ]
                    delegate: Row {
                        width: (parent.width - 8) / 2; spacing: 6
                        Text { width: 52; text: modelData.label; color: "#b7cedb"; font.pixelSize: 12
                               anchors.verticalCenter: parent.verticalCenter }
                        TextField {
                            objectName: "initialFenceBound" + modelData.index
                            width: parent.width - 58; height: 30
                            color: "#dce9f7"; font.pixelSize: 12
                            selectByMouse: true; inputMethodHints: Qt.ImhFormattedNumbersOnly
                            background: Rectangle { color: "#061522"; radius: 4; border.color: "#16405e" }
                            Component.onCompleted: cancelDialog.fields[modelData.index] = this
                            onTextEdited: cancelDialog.lastEditedIndex = modelData.index
                            onEditingFinished: {
                                cancelDialog.commit(modelData.index, text)
                                cancelDialog.lastEditedIndex = -1
                            }
                        }
                    }
                }
            }

            Row {
                spacing: 8
                Text { width: 270; color: "#8fb6cc"; font.pixelSize: 11; wrapMode: Text.WordWrap
                       text: cancelDialog.center.valid ? "当前位置：(" + Number(cancelDialog.center.x).toFixed(2) + ", " + Number(cancelDialog.center.y).toFixed(2) + ") · " + cancelDialog.center.source : "等待定位" }
                GButton { width: 100; height: 30; text: "刷新位置"; pixelSize: 12; onClicked: cancelDialog.refreshFence() }
            }
            GButton { width: parent.width; height: 30; text: "连接机载任务服务"; visible: !groundLink.connected; onClicked: control.mission.connectForInitialization() }

            Row {
                spacing: 8
                Text { text: "起飞悬停高度（m）"; color: "#b7cedb"; font.pixelSize: 12
                       anchors.verticalCenter: parent.verticalCenter }
                TextField {
                    id: takeoffHeight; width: 100; height: 30; text: "0.5"
                    color: "#dce9f7"; font.pixelSize: 12; selectByMouse: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    background: Rectangle { color: "#061522"; radius: 4; border.color: "#16405e" }
                }
                Text { text: "相对起降点"; color: "#8fb6cc"; font.pixelSize: 11
                       anchors.verticalCenter: parent.verticalCenter }
            }

            Text { id: fenceHint; text: ""; visible: text.length > 0
                   color: "#e8bb76"; font.pixelSize: 11; width: parent.width; wrapMode: Text.WordWrap }

            Text { width: parent.width
                   text: "项目目录：" + VideoConfig.projectDir
                   color: "#8fb6cc"; font.pixelSize: 11; wrapMode: Text.WrapAnywhere }

            Row {
                spacing: 12
                GButton {
                    width: 150; height: 35; text: "确认初始化"
                    enabled: control.mission.canInitialize && cancelDialog.center.valid
                    onClicked: {
                        // 焦点尚未离开输入框时，先提交最后一次修改。
                        if (cancelDialog.lastEditedIndex >= 0) {
                            const index = cancelDialog.lastEditedIndex
                            if (!cancelDialog.commit(index, cancelDialog.fields[index].text)) return
                            cancelDialog.lastEditedIndex = -1
                        }
                        if (!cancelDialog.editValid) return
                        const f = []
                        for (let i = 0; i < 6; ++i)
                            f.push(cancelDialog.fields[i] && cancelDialog.fields[i].text.trim().length ? Number(cancelDialog.fields[i].text) : NaN)
                        if (f.some(function (v) { return !isFinite(v) })) {
                            fenceHint.text = "围栏数值无效"
                            return
                        }
                        if (!(f[0] < f[3] && f[1] < f[4] && f[2] < f[5])) {
                            fenceHint.text = "每个轴必须下限 < 上限"
                            return
                        }
                        const h = Number(takeoffHeight.text)
                        if (!isFinite(h) || h <= 0.1 || h >= f[5] - f[2]) {
                            fenceHint.text = "起飞高度须大于 0.1 m，且小于围栏高度"
                            return
                        }
                        if (control.mission.initialize(f, VideoConfig.projectDir, h)) cancelDialog.close()
                        else fenceHint.text = control.mission.status
                    }
                }
                GButton { width: 110; height: 35; text: "取消"; onClicked: cancelDialog.close() }
            }
        }
    }
}
