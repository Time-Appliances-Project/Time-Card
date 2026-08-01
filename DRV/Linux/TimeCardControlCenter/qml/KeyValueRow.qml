import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root

    property string label: ""
    property string value: ""
    property color valueColor: "#d9e9ed"

    spacing: 12

    Label {
        Layout.preferredWidth: 135
        text: root.label
        color: "#7796a2"
        font.pixelSize: 12
    }

    Label {
        Layout.fillWidth: true
        text: root.value
        color: root.valueColor
        font.pixelSize: 12
        font.family: Qt.platform.os === "osx" ? "Menlo" : "monospace"
        elide: Text.ElideMiddle
        horizontalAlignment: Text.AlignRight
    }
}
