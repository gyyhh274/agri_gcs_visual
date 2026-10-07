import QtQuick 2.15
import QtQuick.Controls 2.15
import "../components"

Item {
    id: page
    property int sectionIndex: 0
    property int pageNumber: 0
    property int pageSize: 24
    property int selectedEvent: 10
    property string keyword: ""
    property string deviceFilter: "全部"
    property string kindFilter: "全部"
    property bool videoMode: true
    property int mediaIndex: 2
    property var columns: [34, 100, 104, 96, 310, 190]
    property var filteredEvents: []
    property int pageCount: Math.max(1, Math.ceil(filteredEvents.length / pageSize))
    property var selected: selectedEvent >= 0 ? eventModel.get(selectedEvent) : null
    readonly property string taskName: "电力巡检_20241226"

    function notify(message) {
        toastText.text = message
        toast.opacity = 1
        toastTimer.restart()
    }
    function applyFilters() {
        var rows = []
        for (var i = 0; i < eventModel.count; ++i) {
            var e = eventModel.get(i)
            if (sectionIndex === 1 && e.device !== "无人机") continue
            if (sectionIndex === 2 && e.device !== "机巢") continue
            if (sectionIndex === 3 && e.kind !== "告警") continue
            if (sectionIndex === 4 && e.kind !== "拍照") continue
            if (deviceFilter !== "全部" && e.device !== deviceFilter) continue
            if (kindFilter !== "全部" && e.kind !== kindFilter) continue
            var query = keyword.toLowerCase().trim()
            if (query && (e.time + e.kind + e.device + e.detail + taskName).toLowerCase().indexOf(query) < 0) continue
            rows.push(i)
        }
        filteredEvents = rows
        pageNumber = 0
        if (rows.indexOf(selectedEvent) < 0) selectedEvent = rows.length ? rows[0] : -1
    }
    function query() {
        keyword = keywordField.text
        deviceFilter = deviceBox.currentText
        kindFilter = kindBox.currentText
        applyFilters()
    }
    onSectionIndexChanged: applyFilters()
    onPageSizeChanged: pageNumber = 0
    Component.onCompleted: applyFilters()

    ListModel {
        id: eventModel
        ListElement { time: "15:20:11"; kind: "任务"; device: "系统"; detail: "开始任务：电力巡检_20241226"; waypoint: "—" }
        ListElement { time: "15:20:12"; kind: "通信"; device: "机巢"; detail: "机巢连接成功（IP：192.168.1.100）"; waypoint: "—" }
        ListElement { time: "15:20:13"; kind: "机巢"; device: "机巢"; detail: "停止充电成功"; waypoint: "—" }
        ListElement { time: "15:20:15"; kind: "机巢"; device: "机巢"; detail: "打开舱门"; waypoint: "—" }
        ListElement { time: "15:20:17"; kind: "飞行"; device: "无人机"; detail: "解锁成功"; waypoint: "—" }
        ListElement { time: "15:20:20"; kind: "飞行"; device: "无人机"; detail: "起飞成功，高度 20 m"; waypoint: "—" }
        ListElement { time: "15:20:35"; kind: "航点"; device: "无人机"; detail: "到达航点 1，开始拍照"; waypoint: "1" }
        ListElement { time: "15:20:38"; kind: "拍照"; device: "无人机"; detail: "拍照完成（1/12）"; waypoint: "1" }
        ListElement { time: "15:22:10"; kind: "航点"; device: "无人机"; detail: "到达航点 2，开始拍照"; waypoint: "2" }
        ListElement { time: "15:22:12"; kind: "拍照"; device: "无人机"; detail: "拍照完成（2/12）"; waypoint: "2" }
        ListElement { time: "15:24:01"; kind: "航点"; device: "无人机"; detail: "到达航点 3，开始拍照"; waypoint: "3" }
        ListElement { time: "15:24:18"; kind: "拍照"; device: "无人机"; detail: "拍照完成（3/12）"; waypoint: "3" }
        ListElement { time: "15:26:35"; kind: "航点"; device: "无人机"; detail: "到达航点 4，开始拍照"; waypoint: "4" }
        ListElement { time: "15:28:02"; kind: "拍照"; device: "无人机"; detail: "拍照完成（4/12）"; waypoint: "4" }
        ListElement { time: "15:29:56"; kind: "航点"; device: "无人机"; detail: "到达航点 5，开始拍照"; waypoint: "5" }
        ListElement { time: "15:31:18"; kind: "拍照"; device: "无人机"; detail: "拍照完成（5/12）"; waypoint: "5" }
        ListElement { time: "15:32:05"; kind: "航点"; device: "无人机"; detail: "到达航点 6，开始拍照"; waypoint: "6" }
        ListElement { time: "15:32:45"; kind: "拍照"; device: "无人机"; detail: "拍照完成（6/12）"; waypoint: "6" }
        ListElement { time: "15:33:20"; kind: "航点"; device: "无人机"; detail: "到达航点 7，开始拍照"; waypoint: "7" }
        ListElement { time: "15:35:10"; kind: "飞行"; device: "无人机"; detail: "返航开始"; waypoint: "—" }
        ListElement { time: "15:37:02"; kind: "飞行"; device: "无人机"; detail: "降落成功"; waypoint: "—" }
        ListElement { time: "15:37:10"; kind: "机巢"; device: "机巢"; detail: "进入舱内"; waypoint: "—" }
        ListElement { time: "15:37:15"; kind: "机巢"; device: "机巢"; detail: "开始充电"; waypoint: "—" }
        ListElement { time: "15:37:30"; kind: "任务"; device: "系统"; detail: "任务完成"; waypoint: "—" }
    }

    Row {
        anchors.fill: parent; anchors.leftMargin: 13; anchors.rightMargin: 13
        anchors.topMargin: 10; anchors.bottomMargin: 25; spacing: 12
        Rectangle {
            width: 175; height: parent.height; color: "#061827"; border.color: "#0d2c43"; radius: 5
            Column { width: parent.width; topPadding: 7
                Repeater { model: [["☷", "任务日志"], ["♧", "飞行记录"], ["▣", "机巢记录"], ["♙", "告警记录"], ["◉", "图片/视频"], ["⬇", "数据导出"]]
                    delegate: Rectangle { width: 175; height: 59; color: page.sectionIndex === index ? "#0755c8" : "transparent"; radius: 4
                        Row { anchors.centerIn: parent; spacing: 13; Text { width: 25; text: modelData[0]; color: "white"; font.pixelSize: 21 } Text { text: modelData[1]; color: "white"; font.pixelSize: 16; font.bold: page.sectionIndex === index } }
                        MouseArea { anchors.fill: parent; onClicked: page.sectionIndex = index }
                    }
                }
            }
        }

        GCard {
            width: 850; height: parent.height
            title: ["任务日志", "飞行记录", "机巢记录", "告警记录", "图片/视频", "数据导出"][page.sectionIndex]
            Item {
                anchors.fill: parent
                Row { x: 8; y: 8; spacing: 8
                    GField { width: 244; height: 34; text: "2024-12-26   ~   2024-12-26"; readOnly: true }
                    Text { text: "任务"; color: "#afc4d6"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                    ComboBox {
                        id: taskBox; width: 95; height: 34; model: ["全部任务", "电力巡检"]
                        font.pixelSize: 12; palette.buttonText: "#dce9f7"; palette.text: "#dce9f7"; palette.base: "#082033"; palette.button: "#082033"; palette.highlight: "#087dff"
                        background: Rectangle { color: "#061522"; border.color: "#16405e"; radius: 4 }
                    }
                    ComboBox {
                        id: deviceBox; width: 83; height: 34; model: ["全部", "无人机", "机巢", "系统"]
                        font.pixelSize: 12; palette.buttonText: "#dce9f7"; palette.text: "#dce9f7"; palette.base: "#082033"; palette.button: "#082033"; palette.highlight: "#087dff"
                        background: Rectangle { color: "#061522"; border.color: "#16405e"; radius: 4 }
                        onActivated: page.query()
                    }
                    ComboBox {
                        id: kindBox; width: 93; height: 34; model: ["全部", "任务", "通信", "机巢", "飞行", "航点", "拍照", "告警"]
                        font.pixelSize: 12; palette.buttonText: "#dce9f7"; palette.text: "#dce9f7"; palette.base: "#082033"; palette.button: "#082033"; palette.highlight: "#087dff"
                        background: Rectangle { color: "#061522"; border.color: "#16405e"; radius: 4 }
                        onActivated: page.query()
                    }
                    GField { id: keywordField; width: 130; height: 34; placeholder: "请输入关键词"; onAccepted: page.query() }
                    GButton { width: 62; height: 34; text: "查询"; fill: "#087dff"; onClicked: page.query() }
                }
                Rectangle { x: 8; y: 58; width: parent.width - 16; height: 38; color: "#0a2234"; radius: 4
                    Row { anchors.fill: parent
                        Repeater { model: ["", "时间", "事件类型", "设备", "内容", "任务名称"]
                            delegate: Text { width: page.columns[index]; height: 38; text: modelData; color: "#aec1d2"; font.pixelSize: 13; font.bold: true; horizontalAlignment: index === 4 ? Text.AlignLeft : Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }
                ListView {
                    id: eventList
                    x: 8; y: 96; width: parent.width - 16; height: parent.height - 172
                    model: page.filteredEvents.slice(page.pageNumber * page.pageSize, (page.pageNumber + 1) * page.pageSize)
                    clip: true; boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Rectangle {
                        id: eventRow
                        property int eventIndex: modelData
                        property var event: eventModel.get(eventIndex)
                        width: ListView.view.width; height: 29.5
                        color: page.selectedEvent === eventIndex ? "#0757ad" : (index % 2 ? "#081d2d" : "#071725")
                        border.color: page.selectedEvent === eventIndex ? "#087dff" : "#102d42"
                        Row { anchors.fill: parent
                            Item { width: page.columns[0]; height: parent.height
                                Rectangle { anchors.centerIn: parent; width: 12; height: 12; radius: 6; color: eventRow.event.kind === "机巢" || eventRow.event.kind === "通信" || eventRow.event.kind === "航点" ? "#00db80" : "#087dff"
                                    Text { anchors.centerIn: parent; text: parent.color.toString() === "#087dff" ? "✓" : ""; font.pixelSize: 9; color: "white" }
                                }
                            }
                            Text { width: page.columns[1]; height: parent.height; text: eventRow.event.time; color: "#d7e5f1"; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter }
                            Text { width: page.columns[2]; height: parent.height; text: eventRow.event.kind; color: "#d7e5f1"; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter }
                            Text { width: page.columns[3]; height: parent.height; text: eventRow.event.device; color: "#d7e5f1"; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter }
                            Text { width: page.columns[4]; height: parent.height; text: eventRow.event.detail; color: eventRow.event.detail === "任务完成" ? "#00e787" : "#d7e5f1"; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                            Text { width: page.columns[5]; height: parent.height; text: page.taskName; color: "#d7e5f1"; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                        }
                        MouseArea { anchors.fill: parent; onClicked: page.selectedEvent = eventRow.eventIndex }
                    }
                    Text { anchors.centerIn: parent; visible: page.filteredEvents.length === 0; text: page.sectionIndex === 3 ? "当前演示任务没有告警记录" : "没有符合筛选条件的记录"; color: "#839fb5"; font.pixelSize: 15 }
                }
                Row {
                    anchors.bottom: parent.bottom; anchors.right: parent.right; anchors.rightMargin: 14; anchors.bottomMargin: 22; spacing: 8
                    GButton { visible: page.sectionIndex === 5; width: 94; height: 30; text: "导出 CSV"; fill: "#087dff"; onClicked: page.notify("演示数据导出接口尚未接入，未生成文件") }
                    Text { text: "共 " + page.filteredEvents.length + " 条"; color: "#9eb2c5"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                    GButton { width: 28; height: 30; text: "‹"; opacity: page.pageNumber > 0 ? 1 : 0.4; onClicked: if (page.pageNumber > 0) --page.pageNumber }
                    Repeater { model: page.pageCount
                        delegate: GButton { width: 30; height: 30; text: (index + 1).toString(); fill: page.pageNumber === index ? "#087dff" : "#071725"; checked: true; onClicked: page.pageNumber = index }
                    }
                    GButton { width: 28; height: 30; text: "›"; opacity: page.pageNumber + 1 < page.pageCount ? 1 : 0.4; onClicked: if (page.pageNumber + 1 < page.pageCount) ++page.pageNumber }
                    ComboBox {
                        width: 96; height: 30; model: ["24 条/页", "12 条/页", "6 条/页"]; font.pixelSize: 12
                        palette.buttonText: "#dce9f7"; palette.text: "#dce9f7"; palette.base: "#082033"; palette.button: "#082033"; palette.highlight: "#087dff"
                        background: Rectangle { color: "#061522"; border.color: "#16405e"; radius: 4 }
                        onActivated: page.pageSize = [24, 12, 6][currentIndex]
                    }
                }
            }
        }

        Item {
            width: parent.width - 1049; height: parent.height
            GCard { id: mapCard; width: parent.width; height: 292; title: "事件位置"
                MapPanel { anchors.fill: parent; anchors.margins: 5 }
            }
            GCard { id: mediaCard; width: parent.width; height: 382; anchors.top: mapCard.bottom; anchors.topMargin: 10; title: "关联视频/图片"
                Row { x: 8; y: 0; spacing: 2
                    GButton { width: 74; height: 32; text: "图片"; fill: page.videoMode ? "#102a3e" : "#087dff"; onClicked: page.videoMode = false }
                    GButton { width: 74; height: 32; text: "视频"; fill: page.videoMode ? "#087dff" : "#102a3e"; onClicked: page.videoMode = true }
                }
                VideoPanel { x: 8; y: 40; width: parent.width - 16; height: 220; visible: page.videoMode; clean: true }
                MediaThumb { x: 8; y: 40; width: parent.width - 16; height: 220; photoIndex: page.mediaIndex; visible: !page.videoMode }
                Rectangle {
                    x: 8; y: 224; width: parent.width - 16; height: 36; color: "#d002111b"
                    Rectangle { x: 10; y: 0; width: parent.width - 20; height: 3; color: "#87979f"
                        Rectangle { x: 0; y: -3; width: 9; height: 9; radius: 5; color: "white"; border.width: 2; border.color: "#087dff" }
                    }
                    Text { x: 13; anchors.verticalCenter: parent.verticalCenter; text: page.videoMode ? "▶    00:00 / 00:24" : "图片  " + (page.mediaIndex + 1) + " / 6"; color: "white"; font.pixelSize: 12 }
                    Text { anchors.right: parent.right; anchors.rightMargin: 13; anchors.verticalCenter: parent.verticalCenter; text: "◖))     ⛶"; color: "white"; font.pixelSize: 15 }
                    MouseArea { anchors.fill: parent; onClicked: page.notify(page.videoMode ? "当前为视频封面演示，尚未载入视频文件" : "当前为参考图像预览") }
                }
                Row { x: 8; y: 274; spacing: 5
                    GButton { width: 23; height: 36; text: "‹"; onClicked: page.mediaIndex = Math.max(0, page.mediaIndex - 1) }
                    Repeater { model: ["15:23:50", "15:23:58", "15:24:06", "15:24:14", "15:24:22", "15:24:30"]
                        delegate: Column { width: 59; spacing: 5
                            Rectangle { width: 59; height: 43; color: "transparent"; border.color: page.mediaIndex === index ? "#087dff" : "#244968"; border.width: 2; radius: 2
                                MediaThumb { anchors.fill: parent; anchors.margins: 2; photoIndex: index }
                                MouseArea { anchors.fill: parent; onClicked: { page.mediaIndex = index; page.videoMode = false } }
                            }
                            Text { text: modelData; color: "#a9bdcc"; font.pixelSize: 9; anchors.horizontalCenter: parent.horizontalCenter }
                        }
                    }
                    GButton { width: 23; height: 36; text: "›"; onClicked: page.mediaIndex = Math.min(5, page.mediaIndex + 1) }
                }
            }
            GCard {
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: mediaCard.bottom; anchors.topMargin: 10; anchors.bottom: parent.bottom; title: "事件详情"
                Column { x: 14; y: 4; spacing: 6
                    Repeater { model: [
                            ["时间", page.selected ? "2024-12-26 " + page.selected.time : "—"],
                            ["设备", page.selected ? page.selected.device : "—"],
                            ["任务名称", page.selected ? page.taskName : "—"],
                            ["航点序号", page.selected ? page.selected.waypoint : "—"],
                            ["经度", page.selected ? "121.487621" : "—"],
                            ["纬度", page.selected ? "31.236102" : "—"],
                            ["高度", page.selected ? "120 m" : "—"],
                            ["速度", page.selected ? "8.5 m/s" : "—"]]
                        delegate: Row { spacing: 8; Text { width: 86; text: modelData[0]; color: "#9fb4c8"; font.pixelSize: 12 } Text { text: modelData[1]; color: "#dce9f7"; font.pixelSize: 12 } }
                    }
                }
                Column { x: 280; y: 4; width: parent.width - 290; spacing: 9
                    Row { spacing: 8; Text { text: "事件类型"; color: "#9fb4c8"; font.pixelSize: 12 } Text { text: page.selected ? page.selected.kind : "—"; color: "#dce9f7"; font.pixelSize: 12 } }
                    Text { width: parent.width; text: "内容"; color: "#9fb4c8"; font.pixelSize: 12 }
                    Text { width: parent.width; text: page.selected ? page.selected.detail : "—"; wrapMode: Text.Wrap; color: "#dce9f7"; font.pixelSize: 12 }
                }
            }
        }
    }
    Rectangle {
        id: toast; z: 30; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 35
        width: toastText.width + 42; height: 44; radius: 5; color: "#ed0a273d"; border.color: "#087dff"; opacity: 0
        Text { id: toastText; anchors.centerIn: parent; color: "white"; font.pixelSize: 14 }
        Timer { id: toastTimer; interval: 3000; onTriggered: toast.opacity = 0 }
    }
}
