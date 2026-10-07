import QtQuick 2.15
import QtQuick.Controls 2.15

TextField {
    height: 31; color: "#e3edf7"; selectionColor: "#087dff"; selectedTextColor: "white"; font.pixelSize: 13; leftPadding: 9; rightPadding: 8
    background: Rectangle { color: "#061522"; radius: 3; border.color: parent.activeFocus ? "#087dff" : "#173a52" }
}

