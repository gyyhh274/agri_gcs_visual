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
        onOpened: {
            center = { valid: false }
            refreshFence()
        }
        function refreshFence() {
            const pos = control.mission.initializationPosition()
            if (!pos.valid) {
                center = { valid: false }
                fenceHint.text = "当前位置未更新，请连接机载或等待 LIVE 地图定位，再刷新位置"
                return
            }
            let current = center.valid ? fence : groundLink.flightBounds
            if (!current || current.length !== 6 || current.every(function (v) { return v === 0 }))
                current = control.mission.flightBounds
            let width = 2.0, length = 2.0, height = 1.55
            if (current && current.length === 6 && current[3] > current[0] && current[4] > current[1] && current[5] > current[2]) {
                width = current[3]-current[0]; length = current[4]-current[1]; height = current[5]-current[2]
            }
            center = pos
            fence = [pos.x-width/2, pos.y-length/2, pos.z-0.05,
                     pos.x+width/2, pos.y+length/2, pos.z-0.05+height]
            fenceHint.text = ""
            Qt.callLater(function () {
                for (let i = 0; i < 6; ++i)
                    if (cancelDialog.fields[i])
                        cancelDialog.fields[i].text = Number(cancelDialog.fence[i]).toFixed(2)
            })
        }
        /* 编辑完成后回写；同一轴上下限写反时自动交换，并提示原因。 */
        function commit(slot, raw) {
            const value = Number(raw)
            if (!isFinite(value)) { fenceHint.text = "请输入数字"; return }
            const next = fence.slice()
            next[slot] = value
            let swapped = false
            const pairs = [[0, 3], [1, 4], [2, 5]]
            for (let i = 0; i < pairs.length; ++i) {
                const lo = pairs[i][0], hi = pairs[i][1]
                if (next[lo] > next[hi]) {
                    const t = next[lo]; next[lo] = next[hi]; next[hi] = t
                    swapped = true
                }
            }
            if (center.valid) {
                const width = next[3]-next[0], length = next[4]-next[1]
                next[0] = center.x-width/2; next[3] = center.x+width/2
                next[1] = center.y-length/2; next[4] = center.y+length/2
            }
            fence = next
            for (let i = 0; i < 6; ++i)
                if (fields[i]) fields[i].text = Number(next[i]).toFixed(2)
            fenceHint.text = swapped ? "下限必须小于上限，已自动交换" : ""
        }
        background: Rectangle { color: "#0a2234"; border.color: "#28617b"; radius: 6 }
        contentItem: Column {
            spacing: 10
            Text { text: "任务初始化"; color: "white"; font.pixelSize: 18 }
            Text { width: parent.width
                   text: "每次初始化都以当前位置为水平中心，沿用上次围栏的宽和长。请核对新的边界是否在实际场地内。\n初始化会清理旧任务并重建规划器地图；仅在地面、飞控未解锁时可用。"
                   color: "#cfe0ef"; wrapMode: Text.WordWrap; font.pixelSize: 12 }

            Text { width: parent.width
                   text: "本次围栏（world 坐标，m）：X/Y 自动居中；Z 为高度上下限，可按现场调整。"
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
                            onEditingFinished: cancelDialog.commit(modelData.index, text)
                        }
                    }
                }
            }

            Row {
                spacing: 8
                Text { width: 270; color: "#8fb6cc"; font.pixelSize: 11; wrapMode: Text.WordWrap
                       text: cancelDialog.center.valid ? "中心：(" + Number(cancelDialog.center.x).toFixed(2) + ", " + Number(cancelDialog.center.y).toFixed(2) + ") · " + cancelDialog.center.source : "等待定位" }
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
                        // 直接读输入框，避免"改完立刻点确认"时 editingFinished 还没触发。
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
                        // 不信任编辑完成事件：确认时按预览中心重算水平边界。
                        const width = f[3]-f[0], length = f[4]-f[1]
                        f[0] = cancelDialog.center.x-width/2; f[3] = cancelDialog.center.x+width/2
                        f[1] = cancelDialog.center.y-length/2; f[4] = cancelDialog.center.y+length/2
                        if (control.mission.initialize(f, VideoConfig.projectDir, h)) cancelDialog.close()
                        else fenceHint.text = control.mission.status
                    }
                }
                GButton { width: 110; height: 35; text: "取消"; onClicked: cancelDialog.close() }
            }
        }
    }
}
