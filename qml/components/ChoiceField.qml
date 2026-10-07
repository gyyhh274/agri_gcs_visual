import QtQuick 2.15
import QtQuick.Controls 2.15

ComboBox {
    id: choice
    height: 31; font.pixelSize: 13; padding: 8
    contentItem: Text { text: choice.displayText; color: "#e3edf7"; font: choice.font; verticalAlignment: Text.AlignVCenter; rightPadding: 16; elide: Text.ElideRight }
    background: Rectangle { color: "#061522"; radius: 3; border.color: choice.activeFocus ? "#087dff" : "#173a52" }
    indicator: Text { x: choice.width - 23; y: 6; text: "⌄"; color: "#bfd3e7"; font.pixelSize: 16 }
    delegate: ItemDelegate {
        width: choice.width; height: 34
        contentItem: Text { text: modelData; color: "#e3edf7"; font.pixelSize: 13; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { color: highlighted ? "#0c4a79" : "#0b2539" }
        highlighted: choice.highlightedIndex === index
    }
    popup: Popup {
        y: choice.height + 2; width: choice.width; padding: 1; implicitHeight: contentItem.implicitHeight + 2
        contentItem: ListView { clip: true; implicitHeight: contentHeight; model: choice.popup.visible ? choice.delegateModel : null; currentIndex: choice.highlightedIndex }
        background: Rectangle { color: "#0b2539"; border.color: "#245471"; radius: 3 }
    }
}

