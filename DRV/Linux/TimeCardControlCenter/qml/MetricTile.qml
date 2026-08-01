import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property string label: ""
    property string value: ""
    property string detail: ""
    property color tone: "#31d7d1"

    color: "#0d202a"
    border.color: "#183844"
    border.width: 1
    radius: 14
    implicitHeight: 124

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 7

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: root.label.toUpperCase()
                color: "#7897a3"
                font.pixelSize: 10
                font.letterSpacing: 1.2
                font.weight: Font.DemiBold
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: root.tone
            }
        }

        Label {
            Layout.fillWidth: true
            text: root.value
            color: "#f1fbfc"
            font.pixelSize: 22
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Label {
            Layout.fillWidth: true
            text: root.detail
            color: "#7897a3"
            font.pixelSize: 11
            elide: Text.ElideRight
        }
    }
}
