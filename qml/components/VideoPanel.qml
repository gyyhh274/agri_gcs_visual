import QtQuick 2.15

Rectangle {
    id: root
    property bool thermal: false
    property bool clean: false
    color: "#071a29"
    radius: 5
    clip: true

    Image {
        anchors.fill: parent
        source: "qrc:/assets/video.png"
        fillMode: Image.Stretch
        smooth: true
        visible: !root.clean
        opacity: root.thermal ? 0.55 : 1.0
    }
    Item {
        anchors.fill: parent
        visible: root.clean
        clip: true
        Image {
            source: "qrc:/assets/video.png"
            width: 404; height: 336
            scale: Math.max(root.width / 404, root.height / 218)
            transformOrigin: Item.TopLeft
            x: -(width * scale - root.width) / 2
            y: -82 * scale
            smooth: true
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: root.thermal
        color: "#50206c24"
    }
}
