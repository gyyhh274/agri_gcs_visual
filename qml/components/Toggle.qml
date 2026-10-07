import QtQuick 2.15

Rectangle {
    id: root
    property bool checked: true
    signal toggled(bool checked)
    width: 44; height: 24; radius: 12
    color: checked ? "#087dff" : "#274258"
    border.color: checked ? "#2da0ff" : "#456078"

    Rectangle {
        width: 18; height: 18; radius: 9
        y: 3
        x: root.checked ? root.width - width - 3 : 3
        color: "white"
        Behavior on x { NumberAnimation { duration: 120 } }
    }
    MouseArea {
        anchors.fill: parent
        onClicked: {
            root.checked = !root.checked
            root.toggled(root.checked)
        }
    }
}

