import QtQuick
import Lumen

// Collapsible left panel.
//
// Collapse animates width, not visibility, and the content fades slightly
// ahead of the edge so the panel never looks like it is being squashed.
Item {
    id: root

    default property alias content: container.data
    property bool expanded: true

    implicitWidth: expanded ? Tokens.sidebarWidth : 0
    clip: true

    Behavior on implicitWidth {
        NumberAnimation {
            duration: Motion.normal
            easing.type: Easing.Bezier
            easing.bezierCurve: Motion.standard
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Tokens.surface
        opacity: 0.9
    }

    Item {
        id: container
        anchors.fill: parent
        width: Tokens.sidebarWidth   // fixed, so content does not reflow while collapsing
        opacity: root.expanded ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation {
                duration: Motion.fast
                easing.type: Easing.OutCubic
            }
        }
    }

    LumenSeparator {
        vertical: true
        anchors.right: parent.right
        height: parent.height
    }
}
