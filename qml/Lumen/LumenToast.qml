import QtQuick
import Lumen

// Transient confirmation.
//
// Rises from the bottom edge, holds, and leaves on its own. Never blocks, is
// never dismissed by the user, and never asks anything -- if there is a
// decision to make it needs a dialog, not this.
Item {
    id: root

    property int duration: 2200

    // Anchored by the caller; the toast itself only manages its own bubble.
    implicitHeight: bubble.height + Tokens.space6

    property string _text: ""
    property bool _isError: false

    function show(text, isError) {
        root._text = text;
        root._isError = isError === true;
        bubble.state = "visible";
        hideTimer.restart();
    }

    Timer {
        id: hideTimer
        interval: root.duration
        onTriggered: bubble.state = ""
    }

    Squircle {
        id: bubble

        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - height - Tokens.space5

        width: label.implicitWidth + Tokens.space5 * 2
        height: Tokens.controlHeightLarge + Tokens.space1

        radius: height / 2.4
        fillColor: Tokens.surfaceElevated
        strokeColor: root._isError ? Qt.rgba(1, 0.35, 0.35, 0.5) : Tokens.separator
        strokeWidth: 1

        opacity: 0
        visible: opacity > 0

        states: State {
            name: "visible"
            PropertyChanges {
                bubble.opacity: 1
                bubble.y: root.height - bubble.height - Tokens.space6
            }
        }

        transitions: Transition {
            NumberAnimation {
                properties: "opacity"
                duration: Motion.fast
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                properties: "y"
                duration: Motion.normal
                easing.type: Easing.Bezier
                easing.bezierCurve: Motion.standard
            }
        }

        Text {
            id: label
            anchors.centerIn: parent
            text: root._text
            font.family: Tokens.fontFamily
            font.pixelSize: Tokens.textBody
            font.weight: Tokens.weightMedium
            color: root._isError ? Qt.rgba(1, 0.5, 0.5, 1) : Tokens.textPrimary
        }
    }
}
