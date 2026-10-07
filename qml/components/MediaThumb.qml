import QtQuick 2.15

// Reference photography is displayed as an atlas; captions and checkboxes are QML.
Item {
    id: root
    property int photoIndex: 0
    clip: true
    Item {
        width: 124; height: 76
        scale: Math.max(root.width / width, root.height / height)
        transformOrigin: Item.TopLeft
        Image {
            source: "qrc:/assets/gallery-reference.png"
            width: 1536; height: 1024
            x: -(303 + (root.photoIndex % 6) * 135)
            y: -(584 + Math.floor((root.photoIndex % 18) / 6) * 125)
        }
    }
}
