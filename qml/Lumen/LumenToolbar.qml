import QtQuick
import Lumen

// Translucent top bar. Sits above the scrolling canvas and lets it show
// through, which is what makes the window feel like one surface instead of
// three stacked boxes.
Item {
    id: root

    default property alias content: layout.data
    property alias spacing: layout.spacing

    implicitHeight: Tokens.toolbarHeight

    Rectangle {
        anchors.fill: parent
        color: Tokens.surface
        opacity: 0.82   // the Mica/acrylic backdrop supplies the rest
    }

    Row {
        id: layout
        anchors.fill: parent
        anchors.leftMargin: Tokens.space3
        anchors.rightMargin: Tokens.space3
        spacing: Tokens.space1
    }

    LumenSeparator {
        anchors.bottom: parent.bottom
        width: parent.width
    }
}
