import QtQuick
import QtQuick.Templates as T
import Lumen

// Search input. Grows slightly and brightens its border on focus -- the only
// place in the app where a control changes size on focus, which is what makes
// it read as "you are now typing here".
T.TextField {
    id: control

    property string placeholder: qsTr("Search")

    implicitWidth: 220
    implicitHeight: Tokens.controlHeight

    leftPadding: Tokens.space5 + Tokens.space2
    rightPadding: Tokens.space3
    verticalAlignment: Text.AlignVCenter

    font.family: Tokens.fontFamily
    font.pixelSize: Tokens.textBody
    color: Tokens.textPrimary
    selectionColor: Tokens.accentSubtle
    selectedTextColor: Tokens.textPrimary

    selectByMouse: true

    background: Squircle {
        radius: Tokens.radiusSmall
        fillColor: control.activeFocus ? Tokens.surfaceElevated : Tokens.hoverOverlay
        strokeColor: control.activeFocus ? Tokens.accent : Tokens.separator
        strokeWidth: control.activeFocus ? 1.5 : 1

        Behavior on fillColor { ColorAnimation { duration: Motion.fast } }
        Behavior on strokeColor { ColorAnimation { duration: Motion.fast } }

        Text {
            x: Tokens.space3
            anchors.verticalCenter: parent.verticalCenter
            text: "⌕"
            font.pixelSize: Tokens.iconSize - 2
            color: control.activeFocus ? Tokens.accent : Tokens.textTertiary
            Behavior on color { ColorAnimation { duration: Motion.fast } }
        }
    }

    Text {
        anchors.fill: parent
        leftPadding: control.leftPadding
        verticalAlignment: Text.AlignVCenter
        text: control.placeholder
        font: control.font
        color: Tokens.textTertiary
        visible: control.text.length === 0
        opacity: control.activeFocus ? 0.5 : 1.0
        Behavior on opacity { NumberAnimation { duration: Motion.fast } }
    }
}
