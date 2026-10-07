import QtQuick 2.15
import QtQuick.Controls 2.15

Row {
    id: connectionMode
    property var configuration: ({})
    signal edited(string key, var value)
    property string configKey: ""
    width: parent.width; height: 29
    FormLabel { width: 94; text: "连接方式"; height: 29 }
    Repeater {
        model: ["自动连接", "手动配置"]
        delegate: Item {
            width: 111; height: 29
            Rectangle {
                x: 0; y: 7; width: 15; height: 15; radius: 8
                color: Boolean(connectionMode.configuration[connectionMode.configKey]) === (index === 0) ? "#087dff" : "transparent"
                border.color: "#5f8bab"
                Rectangle { anchors.centerIn: parent; width: 5; height: 5; radius: 3; color: "white"; visible: Boolean(connectionMode.configuration[connectionMode.configKey]) === (index === 0) }
            }
            FormLabel { x: 26; text: modelData; height: 29 }
            MouseArea { anchors.fill: parent; onClicked: connectionMode.edited(connectionMode.configKey, index === 0) }
        }
    }
}

