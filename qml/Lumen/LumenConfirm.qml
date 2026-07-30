import QtQuick
import Lumen

// Modal confirmation for an action that cannot be undone.
//
// Deliberately plain and deliberately specific: the body says what will happen
// and what the cost is, and the confirm button is labelled with the verb rather
// than "OK". A dialog that says "Are you sure?" with an OK button teaches the
// user to click through without reading.
Item {
    id: root

    property string title: ""
    property string body: ""
    property string confirmText: qsTr("Continue")
    property string cancelText: qsTr("Cancel")

    // Marks the action as destructive, which colours the confirm button.
    property bool destructive: false

    signal confirmed()

    anchors.fill: parent
    visible: opacity > 0
    opacity: 0
    z: 450

    function open() {
        root.state = "open";
        root.forceActiveFocus();
    }
    function close() { root.state = "" }

    states: State {
        name: "open"
        PropertyChanges { root.opacity: 1 }
    }

    transitions: Transition {
        NumberAnimation {
            property: "opacity"
            duration: Motion.fast
            easing.type: Easing.OutCubic
        }
    }

    Keys.onEscapePressed: root.close()

    Rectangle {
        anchors.fill: parent
        color: Tokens.scrim
        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
        }
    }

    Squircle {
        anchors.centerIn: parent
        width: Math.min(parent.width - Tokens.space6 * 2, 440)
        height: content.implicitHeight + Tokens.space5 * 2

        radius: Tokens.radiusLarge
        fillColor: Tokens.surfaceElevated
        strokeColor: Tokens.separator
        strokeWidth: 1

        transformOrigin: Item.Center
        scale: root.state === "open" ? 1.0 : 0.96
        Behavior on scale {
            SpringAnimation {
                spring: Motion.spring
                damping: Motion.damping
                epsilon: Motion.epsilon
            }
        }

        MouseArea { anchors.fill: parent }

        Column {
            id: content
            x: Tokens.space5
            y: Tokens.space5
            width: parent.width - Tokens.space5 * 2
            spacing: Tokens.space3

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: root.title
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textLarge
                font.weight: Tokens.weightSemiBold
                color: Tokens.textPrimary
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: root.body
                lineHeight: 1.35
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textBody
                color: Tokens.textSecondary
            }

            Item { width: 1; height: Tokens.space1 }

            Row {
                anchors.right: parent.right
                spacing: Tokens.space2

                LumenButton {
                    text: root.cancelText
                    variant: LumenButton.Variant.Secondary
                    onClicked: root.close()
                }

                LumenButton {
                    text: root.confirmText
                    variant: LumenButton.Variant.Primary
                    onClicked: {
                        root.close();
                        root.confirmed();
                    }
                }
            }
        }
    }
}
