import QtQuick
import QtQuick.Controls

Button {
    id: root

    property bool selected: false
    property string symbol: ""

    implicitHeight: 44
    leftPadding: 14
    rightPadding: 12

    contentItem: Text {
        text: root.symbol + "   " + root.text
        color: root.selected ? "#eaffff" : "#86a2ad"
        font.pixelSize: 13
        font.weight: root.selected ? Font.DemiBold : Font.Normal
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 9
        color: root.selected ? "#173b43" : (root.hovered ? "#102a34" : "transparent")
        border.color: root.selected ? "#24606a" : "transparent"
    }
}
