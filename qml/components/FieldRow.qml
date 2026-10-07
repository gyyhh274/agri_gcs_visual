import QtQuick 2.15
import QtQuick.Controls 2.15

Row {
    id: fieldRow
    property var configuration: ({})
    signal edited(string key, var value)
    property string label: ""
    property string configKey: ""
    property int labelWidth: 94
    height: 31; width: parent.width
    FormLabel { width: fieldRow.labelWidth; text: fieldRow.label }
    ValueField {
        width: fieldRow.width - fieldRow.labelWidth
        text: String(fieldRow.configuration[fieldRow.configKey] === undefined ? "" : fieldRow.configuration[fieldRow.configKey])
        onTextEdited: fieldRow.edited(fieldRow.configKey, text)
    }
}

