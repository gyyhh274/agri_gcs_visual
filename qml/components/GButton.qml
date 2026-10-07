import QtQuick 2.15

Rectangle {
    id: control
    property string text: "按钮"
    property string icon: ""
    property color fill: "#163b5e"
    property color hoverFill: "#1d4f7c"
    property color textColor: "#f4f8ff"
    property bool checked: false
    property int pixelSize: 14
    signal clicked()

    implicitWidth: 112
    implicitHeight: 40
    radius: 4
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.onPressAction: clicked()
    Keys.onReturnPressed: clicked()
    Keys.onSpacePressed: clicked()
    color: mouse.pressed ? Qt.darker(fill, 1.2) : (mouse.containsMouse ? hoverFill : fill)
    border.color: checked ? "#0a8cff" : "#244968"
    border.width: checked || activeFocus ? 1 : 0

    Text {
        anchors.centerIn: parent
        text: (control.icon.length ? control.icon + "  " : "") + control.text
        color: control.textColor
        font.pixelSize: control.pixelSize
        font.bold: true
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: { control.forceActiveFocus(); control.clicked() }
    }
}
