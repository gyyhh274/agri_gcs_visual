import QtQuick 2.15
import QtQuick.Controls 2.15

Row {
    id: switchRow
    property var configuration: ({})
    signal edited(string key, var value)
    property string label: ""
    property string configKey: ""
    property int labelWidth: 135
    height: 26
    FormLabel { width: switchRow.labelWidth; height: 24; text: switchRow.label }
    Toggle { checked: Boolean(switchRow.configuration[switchRow.configKey]); onToggled: switchRow.edited(switchRow.configKey, checked) }
}

