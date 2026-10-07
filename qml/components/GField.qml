import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root
    property alias text: input.text
    property string placeholder: ""
    property bool password: false
    property bool readOnly: false
    signal accepted()

    implicitWidth: 220
    implicitHeight: 34
    color: "#061522"
    border.color: input.activeFocus ? "#087dff" : "#16405e"
    radius: 4

    TextField {
        id: input
        anchors.fill: parent
        leftPadding: 11
        rightPadding: 10
        color: "#dce9f7"
        placeholderText: root.placeholder
        placeholderTextColor: "#71869b"
        selectionColor: "#087dff"
        selectedTextColor: "white"
        echoMode: root.password ? TextInput.Password : TextInput.Normal
        readOnly: root.readOnly
        font.pixelSize: 14
        background: Item {}
        onAccepted: root.accepted()
    }
}

