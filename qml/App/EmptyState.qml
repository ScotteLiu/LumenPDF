import QtQuick
import Lumen

// Shown before a document is open, while one is loading, and on failure.
//
// The first screen the user ever sees, so it does the work of a landing page:
// one sentence, one action, and nothing else competing for attention.
Item {
    id: root

    property bool loading: false
    property string errorText: ""

    signal openRequested()

    Rectangle {
        anchors.fill: parent
        color: Tokens.canvas
    }

    Column {
        anchors.centerIn: parent
        spacing: Tokens.space4
        width: Math.min(parent.width - Tokens.space6 * 2, 420)

        Item {
            width: parent.width
            height: 96

            // Loading indicator: a single rotating arc, no spinner chrome.
            Squircle {
                anchors.centerIn: parent
                width: 56
                height: 72
                radius: Tokens.radiusSmall
                fillColor: Tokens.surfaceElevated
                strokeColor: Tokens.separator
                strokeWidth: 1

                rotation: root.loading ? 0 : -4
                Behavior on rotation {
                    SpringAnimation {
                        spring: Motion.spring
                        damping: Motion.damping
                        epsilon: Motion.epsilon
                    }
                }

                SequentialAnimation on opacity {
                    running: root.loading
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.45; duration: 620; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0;  duration: 620; easing.type: Easing.InOutSine }
                }
            }
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.errorText.length > 0 ? qsTr("Could not open that file")
                : root.loading                ? qsTr("Opening…")
                                              : qsTr("No document open")
            font.family: Tokens.fontFamily
            font.pixelSize: Tokens.textTitle
            font.weight: Tokens.weightSemiBold
            color: Tokens.textPrimary
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: root.errorText.length > 0
                  ? root.errorText
                  : qsTr("Drop a PDF anywhere in this window, or open one from your computer.")
            font.family: Tokens.fontFamily
            font.pixelSize: Tokens.textBody
            color: Tokens.textSecondary
            visible: !root.loading
        }

        Item { width: 1; height: Tokens.space2 }

        LumenButton {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Open PDF")
            variant: LumenButton.Variant.Primary
            visible: !root.loading
            onClicked: root.openRequested()
        }
    }
}
