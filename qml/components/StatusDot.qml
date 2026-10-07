import QtQuick 2.15

Row {
    id: root
    property string text: "已连接"
    property color dotColor: "#00db80"
    property color textColor: "#00e787"
    spacing: 8

    Rectangle {
        width: 12; height: 12; radius: 6
        anchors.verticalCenter: parent.verticalCenter
        color: root.dotColor
    }
    Text {
        text: root.text
        color: root.textColor
        font.pixelSize: 14
        font.bold: true
        anchors.verticalCenter: parent.verticalCenter
    }
}

