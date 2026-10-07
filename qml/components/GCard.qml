import QtQuick 2.15

Rectangle {
    id: card
    property string title: ""
    property string icon: "▌"
    property bool collapsible: false
    property bool collapsed: false
    default property alias contentData: body.data

    color: "#071a29"
    border.color: "#0d2c43"
    border.width: 1
    radius: 6
    clip: true

    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: card.title.length ? 42 : 0
        color: "#082033"

        Rectangle {
            visible: card.title.length > 0
            x: 10; width: 5; height: 22; radius: 2
            anchors.verticalCenter: parent.verticalCenter
            color: "#087dff"
        }
        Text {
            visible: card.title.length > 0
            x: 23
            anchors.verticalCenter: parent.verticalCenter
            text: card.title
            color: "#f3f8ff"
            font.pixelSize: 17
            font.bold: true
        }
        Text {
            visible: card.collapsible
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: card.collapsed ? "⌄" : "⌃"
            color: "#dcecff"
            font.pixelSize: 18
        }
        MouseArea {
            anchors.fill: parent
            enabled: card.collapsible
            onClicked: card.collapsed = !card.collapsed
        }
    }

    Item {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        visible: !card.collapsed
    }
}

