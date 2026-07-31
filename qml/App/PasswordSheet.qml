import QtQuick
import Lumen
import Lumen.Backend

// Asks for the password of an encrypted document.
//
// This is a modal sheet with no way past it except a correct password or
// cancelling, which is the honest shape: there is nothing to show behind it.
//
// The field is never pre-filled and the password is never stored. Remembering
// it would mean writing someone else's document password to disk, and this
// application has no keystore of its own to justify that.
Item {
    id: root

    property bool wrongPassword: false

    anchors.fill: parent
    visible: opacity > 0
    opacity: 0
    z: 460

    function ask(retry) {
        root.wrongPassword = retry === true;
        field.text = "";
        root.state = "open";
        field.forceActiveFocus();
    }

    function dismiss() {
        root.state = "";
        field.text = "";
        Document.cancelUnlock();
    }

    function submit() {
        if (field.text.length === 0)
            return;
        root.state = "";
        Document.unlock(field.text);
        field.text = "";
    }

    states: State {
        name: "open"
        PropertyChanges { root.opacity: 1 }
    }

    transitions: Transition {
        NumberAnimation {
            property: "opacity"; duration: Motion.fast; easing.type: Easing.OutCubic
        }
    }

    Connections {
        target: Document
        function onPasswordRequired(retry) { root.ask(retry) }
    }

    Rectangle {
        anchors.fill: parent
        color: Tokens.scrim
        // No click-to-dismiss: there is nothing underneath to get back to, and
        // dismissing by accident loses the file you were trying to open.
        MouseArea { anchors.fill: parent }
    }

    Squircle {
        // Positioned rather than anchored, because the shake animates x and an
        // anchor would fight it.
        x: (parent.width - width) / 2 + root.shakeOffset
        y: (parent.height - height) / 2
        width: Math.min(parent.width - Tokens.space6 * 2, 420)
        height: content.implicitHeight + Tokens.space5 * 2

        radius: Tokens.radiusLarge
        fillColor: Tokens.surfaceElevated
        strokeColor: Tokens.separator
        strokeWidth: 1

        transformOrigin: Item.Center
        scale: root.state === "open" ? 1.0 : 0.96
        Behavior on scale {
            SpringAnimation {
                spring: Motion.spring; damping: Motion.damping; epsilon: Motion.epsilon
            }
        }

        // A short shake on a rejected password, instead of a line of red text
        // that has to be read. It says "try again" without saying anything.
        SequentialAnimation {
            id: shake
            NumberAnimation { target: root; property: "shakeOffset"; to: 8; duration: 50 }
            NumberAnimation { target: root; property: "shakeOffset"; to: -8; duration: 50 }
            NumberAnimation { target: root; property: "shakeOffset"; to: 5; duration: 50 }
            NumberAnimation { target: root; property: "shakeOffset"; to: 0; duration: 50 }
        }

        Column {
            id: content
            x: Tokens.space5
            y: Tokens.space5
            width: parent.width - Tokens.space5 * 2
            spacing: Tokens.space4

            Row {
                spacing: Tokens.space3

                LumenIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    path: Icons.lock
                    size: 22
                    color: Tokens.textSecondary
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("This document is locked")
                    font.family: Tokens.fontFamily
                    font.pixelSize: Tokens.textTitle
                    font.weight: Tokens.weightSemiBold
                    color: Tokens.textPrimary
                }
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                lineHeight: 1.35
                text: root.wrongPassword
                      ? qsTr("That password was not accepted. Passwords are "
                           + "case-sensitive.")
                      : qsTr("Enter the password to open it. It is used once and "
                           + "not saved.")
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textBody
                color: root.wrongPassword ? Tokens.danger : Tokens.textSecondary
            }

            Squircle {
                width: parent.width
                height: Tokens.controlHeight
                radius: Tokens.radiusSmall
                fillColor: Tokens.surface
                strokeColor: field.activeFocus ? Tokens.accent : Tokens.separator
                strokeWidth: 1

                Behavior on strokeColor { ColorAnimation { duration: Motion.instant } }

                TextInput {
                    id: field
                    anchors.fill: parent
                    anchors.leftMargin: Tokens.space3
                    anchors.rightMargin: Tokens.space3

                    verticalAlignment: TextInput.AlignVCenter
                    echoMode: TextInput.Password
                    color: Tokens.textPrimary
                    font.family: Tokens.fontFamily
                    font.pixelSize: Tokens.textBody
                    selectByMouse: true
                    selectionColor: Tokens.accentSubtle
                    selectedTextColor: Tokens.textPrimary

                    onAccepted: root.submit()
                    Keys.onEscapePressed: root.dismiss()
                }
            }

            Item { width: 1; height: Tokens.space1 }

            Row {
                anchors.right: parent.right
                spacing: Tokens.space2

                LumenButton {
                    text: qsTr("Cancel")
                    variant: LumenButton.Variant.Secondary
                    onClicked: root.dismiss()
                }

                LumenButton {
                    text: qsTr("Open")
                    variant: LumenButton.Variant.Primary
                    enabled: field.text.length > 0
                    onClicked: root.submit()
                }
            }
        }
    }

    property real shakeOffset: 0

    onWrongPasswordChanged: if (wrongPassword) shake.start()
}
