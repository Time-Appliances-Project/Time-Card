import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property string title: ""
    property string subtitle: ""
    property color accent: "#31d7d1"
    default property alias content: contentColumn.data

    color: "#0d202a"
    border.color: "#183844"
    border.width: 1
    radius: 14
    implicitHeight: layout.implicitHeight + 32

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 4
                Layout.preferredHeight: 30
                radius: 2
                color: root.accent
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                Label {
                    text: root.title
                    color: "#eef9fb"
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }

                Label {
                    visible: root.subtitle.length > 0
                    text: root.subtitle
                    color: "#7796a2"
                    font.pixelSize: 11
                }
            }
        }

        ColumnLayout {
            id: contentColumn
            Layout.fillWidth: true
            spacing: 8
        }
    }
}
