import QtQuick
import Lumen

// A labelled switch.
//
// The whole row is the target, not just the switch itself -- a 40-pixel hit
// area on the far right of a dialog is a needless test of aim.
Item {
    id: root

    property string label
    property string detail
    property bool checked: false

    signal toggled()

    implicitHeight: text.implicitHeight
                    + (detail.length > 0 ? hint.implicitHeight + Tokens.space1 : 0)

    Column {
        anchors.left: parent.left
        anchors.right: knob.left
        anchors.rightMargin: Tokens.space4
        anchors.verticalCenter: parent.verticalCenter
        spacing: Tokens.space1

        Text {
            id: text
            width: parent.width
            wrapMode: Text.WordWrap
            text: root.label
            font.family: Tokens.fontFamily
            font.pixelSize: Tokens.textBody
            color: Tokens.textPrimary
        }

        Text {
            id: hint
            width: parent.width
            wrapMode: Text.WordWrap
            visible: root.detail.length > 0
            text: root.detail
            font.family: Tokens.fontFamily
            font.pixelSize: Tokens.textSmall
            color: Tokens.textTertiary
        }
    }

    Squircle {
        id: knob
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 44
        height: 26
        radius: 13
        fillColor: root.checked ? Tokens.accent : Tokens.hoverOverlay
        strokeColor: root.checked ? Tokens.accent : Tokens.separator
        strokeWidth: 1

        Behavior on fillColor { ColorAnimation { duration: Motion.fast } }

        Rectangle {
            width: 20
            height: 20
            radius: 10
            color: "#FFFFFF"
            anchors.verticalCenter: parent.verticalCenter
            x: root.checked ? parent.width - width - 3 : 3

            // A spring, because this is direct manipulation: the thumb should
            // feel thrown rather than tweened.
            Behavior on x {
                SpringAnimation {
                    spring: Motion.spring; damping: Motion.damping; epsilon: Motion.epsilon
                }
            }
        }
    }

    HoverHandler { cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: root.toggled() }
}
