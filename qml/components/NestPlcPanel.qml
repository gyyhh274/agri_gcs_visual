import QtQuick 2.15
import QtQuick.Controls 2.15

Column {
    id: panel
    objectName: "nestPlcPanel"
    spacing: 10
    property var draft: Object.assign({}, nestPlc.configuration)
    function edit(key, value) {
        var next = Object.assign({}, draft); next[key] = value; draft = next
    }
    function apply() { if (nestPlc.configure(draft)) draft = Object.assign({}, nestPlc.configuration) }
    function readOnce() {
        if (!/^\d+$/.test(readAddress.text.trim()) || !/^\d+$/.test(readCount.text.trim())) {
            inputMessage.text = "请填写十进制地址和数量；地址从 0 开始，不使用 40001 这样的区号。"
            return
        }
        var address = Number(readAddress.text), count = Number(readCount.text)
        if (!isFinite(address) || !isFinite(count) || address > 65535 || count < 1 || count > (readFunction.currentIndex < 2 ? 2000 : 125) || address + count > 65536) {
            inputMessage.text = "地址或数量越界：地址 0–65535，位最多 2000 / 寄存器最多 125。"
            return
        }
        inputMessage.text = ""
        nestPlc.readRegisters(readFunction.currentIndex + 1, Number(readAddress.text), Number(readCount.text))
    }
    Text { text: "M511S · Modbus TCP 调试"; color: "#e6f0fb"; font.pixelSize: 18; font.bold: true }
    Text {
        width: parent.width; wrapMode: Text.WordWrap; color: "#9fb9ce"; font.pixelSize: 13
        text: "连接、协议响应、寄存器读取和舱门反馈分别显示。Unit ID 默认 1 仅供试验；寄存器映射须与 PLC 项目核对。不会自动连接或自动扫描地址。"
    }
    Repeater {
        model: [["主机 / IPv4", "host"], ["TCP 端口", "port"], ["Unit ID（0–255）", "unit"]]
        delegate: Row {
            width: panel.width; spacing: 10
            Text { width: 176; height: 34; text: modelData[0]; color: "#9fb9ce"; font.pixelSize: 13; verticalAlignment: Text.AlignVCenter }
            GField {
                width: Math.min(310, panel.width - 186); enabled: !nestPlc.connected && !nestPlc.connecting
                text: String(panel.draft[modelData[1]])
                onTextChanged: if (text !== String(panel.draft[modelData[1]])) panel.edit(modelData[1], text)
            }
        }
    }
    Flow {
        width: parent.width; spacing: 8
        GButton { text: "保存 PLC 配置"; enabled: !nestPlc.connected && !nestPlc.connecting; onClicked: panel.apply() }
        GButton {
            objectName: "plcConnectButton"
            text: nestPlc.connected || nestPlc.connecting ? "断开 PLC" : "连接 TCP"
            onClicked: {
                if (nestPlc.connected || nestPlc.connecting) nestPlc.disconnectPlc()
                else if (nestPlc.configure(panel.draft)) nestPlc.connectPlc()
            }
        }
        GButton { objectName: "plcIdentifyButton"; text: "只读协议验证"; width: 126; enabled: nestPlc.connected && !nestPlc.busy; onClicked: nestPlc.identify() }
        GButton {
            text: "本机模拟配置"; width: 126; enabled: !nestPlc.connected && !nestPlc.connecting
            onClicked: { panel.draft = {host: "127.0.0.1", port: 15020, unit: 1, statusFunction: 3, statusAddress: 100, commandAddress: 200}; panel.apply() }
        }
    }
    Text { width: parent.width; wrapMode: Text.WordWrap; text: "TCP：" + (nestPlc.connected ? "已连接" : nestPlc.connecting ? "连接中" : "未连接") + "    Modbus：" + (nestPlc.protocolVerified ? "已验证响应" : "待验证") + "    反馈：" + (nestPlc.feedbackFresh ? "有效" : "未就绪 / 已过期"); color: nestPlc.protocolVerified ? "#00db80" : "#e1b66a"; font.pixelSize: 13 }
    Text { objectName: "plcStatusText"; width: parent.width; wrapMode: Text.WordWrap; text: nestPlc.status; color: "#bcd2e4"; font.pixelSize: 13 }
    Text { width: parent.width; wrapMode: Text.WordWrap; text: "“只读协议验证”发送 FC43/14 设备识别，无须寄存器地址。返回异常 01 也能证明 Modbus 响应有效，但不代表寄存器读取成功。"; color: "#7898b3"; font.pixelSize: 12 }
    Rectangle { width: parent.width; height: 1; color: "#17374e" }
    Text { text: "读取已确认安全的地址"; color: "#e6f0fb"; font.pixelSize: 16 }
    Flow {
        width: parent.width; spacing: 8
        ChoiceField { id: readFunction; width: 205; model: ["01 读线圈", "02 读离散输入", "03 读保持寄存器", "04 读输入寄存器"]; currentIndex: 2 }
        GField { id: readAddress; objectName: "plcReadAddress"; width: 150; placeholder: "0 基十进制地址" }
        GField { id: readCount; width: 85; text: "1"; placeholder: "数量" }
        GButton { objectName: "plcReadButton"; text: "读取一次"; enabled: nestPlc.connected && !nestPlc.busy; onClicked: panel.readOnce() }
    }
    Text { id: inputMessage; width: parent.width; wrapMode: Text.WordWrap; color: "#e1b66a"; font.pixelSize: 12 }
    Text { objectName: "plcReadResult"; width: parent.width; wrapMode: Text.WrapAnywhere; text: nestPlc.readResult; color: "#bcd2e4"; font.pixelSize: 13 }
    Rectangle { width: parent.width; height: 1; color: "#17374e" }
    Text { text: "舱门接口映射（待 PLC 实现 / 核对）"; color: "#e6f0fb"; font.pixelSize: 16 }
    Text {
        width: parent.width; wrapMode: Text.WordWrap; color: "#9fb9ce"; font.pixelSize: 12
        text: "本项目约定：反馈连续 4 个 16 位寄存器 = 状态位、故障码、已确认命令序号、递增心跳。状态位 bit0 开到位 / bit1 关到位 / bit2 开启中 / bit3 关闭中 / bit4 故障 / bit5 联锁 / bit6 急停 / bit7 远程就绪。命令邮箱为 2 个保持寄存器 = 命令（1 开 / 2 关 / 3 停）与序号，通过 FC16 一次写入。这是待实现的接口约定，并非 M511S 固定地址。"
    }
    Row {
        spacing: 10
        Text { width: 176; height: 34; text: "反馈寄存器类型"; color: "#9fb9ce"; font.pixelSize: 13; verticalAlignment: Text.AlignVCenter }
        ChoiceField { width: 240; enabled: !nestPlc.connected && !nestPlc.connecting; model: ["03 保持寄存器", "04 输入寄存器"]; currentIndex: Number(panel.draft.statusFunction) === 4 ? 1 : 0; onActivated: panel.edit("statusFunction", currentIndex + 3) }
    }
    Repeater {
        model: [["反馈起始地址（4 个）", "statusAddress"], ["命令邮箱地址（2 个）", "commandAddress"]]
        delegate: Row {
            width: panel.width; spacing: 10
            Text { width: 176; height: 34; text: modelData[0]; color: "#9fb9ce"; font.pixelSize: 13; verticalAlignment: Text.AlignVCenter }
            GField {
                width: 240; enabled: !nestPlc.connected && !nestPlc.connecting; placeholder: "未分配（留空）"
                text: Number(panel.draft[modelData[1]]) === -1 ? "" : String(panel.draft[modelData[1]])
                onTextChanged: {
                    var current = Number(panel.draft[modelData[1]]) === -1 ? "" : String(panel.draft[modelData[1]])
                    if (text !== current) panel.edit(modelData[1], text)
                }
            }
        }
    }
    Flow {
        width: parent.width; spacing: 8
        GButton { text: nestPlc.monitoring ? "停止监测" : "启动反馈监测"; width: 126; enabled: nestPlc.connected; onClicked: nestPlc.setMonitoring(!nestPlc.monitoring) }
        GButton { text: "打开舱门"; enabled: nestPlc.canMove; onClicked: nestPlc.sendCommand(1) }
        GButton { text: "关闭舱门"; enabled: nestPlc.canMove; onClicked: nestPlc.sendCommand(2) }
        GButton { text: "停止舱门"; fill: "#a45d19"; enabled: nestPlc.canStop; onClicked: nestPlc.sendCommand(3) }
    }
    CheckBox {
        text: "已核对 PLC 映射、限位及联锁，允许本次连接写入"
        checked: nestPlc.writeEnabled; enabled: nestPlc.connected && (nestPlc.writeEnabled || nestPlc.feedbackFresh)
        onClicked: nestPlc.setWriteEnabled(checked)
        palette.windowText: "#dce8f4"
    }
    Text { width: parent.width; wrapMode: Text.WordWrap; text: "舱门：" + nestPlc.doorState + "    状态位：0x" + nestPlc.flags.toString(16) + "    故障码：" + nestPlc.faultCode + "    回执：" + nestPlc.ackSequence + "    心跳：" + nestPlc.heartbeat; color: nestPlc.feedbackFresh ? "#00db80" : "#7898b3"; font.pixelSize: 13 }
    Text { width: parent.width; wrapMode: Text.WordWrap; text: nestPlc.commandStatus; color: "#e1b66a"; font.pixelSize: 13 }
    GButton { visible: nestPlc.uncertain; height: visible ? 36 : 0; text: "确认现场已停止"; width: 170; enabled: nestPlc.feedbackFresh && !nestPlc.busy; onClicked: nestPlc.acknowledgeStopped() }
    Text { width: parent.width; wrapMode: Text.WordWrap; text: "停止按钮通过网络下发软件停止，现场急停由 PLC 与硬件实现。PLC 须独立实现限位、互锁、动作超时与通信失效处理。重启后还需确认 PLC 的 IP 持久化。"; color: "#7898b3"; font.pixelSize: 12 }
    Text { text: "最近 100 条通信报文"; color: "#e6f0fb"; font.pixelSize: 16 }
    ScrollView {
        width: parent.width; height: 190; clip: true
        TextArea { objectName: "plcWireLog"; readOnly: true; selectByMouse: true; text: nestPlc.log.join("\n"); color: "#bcd2e4"; font.pixelSize: 12; font.family: "Menlo"; wrapMode: TextEdit.WrapAnywhere; background: Rectangle { color: "#061522" } }
    }
}
