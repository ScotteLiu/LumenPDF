import QtQuick
import Lumen
import Lumen.Backend

// Sheet for making a scanned document searchable.
//
// Shows what OCR would actually change before running it — how many pages have
// no text and which language will be used. Recognition takes minutes on a long
// document, so starting it by accident is expensive, and "how long and on what"
// is the question the user has before pressing the button.
Item {
    id: root

    readonly property var ocr: Document.ocr

    anchors.fill: parent
    visible: opacity > 0
    opacity: 0
    z: 440

    function open() {
        root.state = "open";
        root.forceActiveFocus();
    }
    function close() {
        if (ocr.busy)
            return;   // closing mid-run would hide progress, not stop it
        root.state = "";
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

    Keys.onEscapePressed: root.close()

    Rectangle {
        anchors.fill: parent
        color: Tokens.scrim
        MouseArea { anchors.fill: parent; onClicked: root.close() }
    }

    Squircle {
        anchors.centerIn: parent
        width: Math.min(parent.width - Tokens.space6 * 2, 480)
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

        MouseArea { anchors.fill: parent }

        Column {
            id: content
            x: Tokens.space5
            y: Tokens.space5
            width: parent.width - Tokens.space5 * 2
            spacing: Tokens.space4

            Text {
                text: qsTr("Recognise text")
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textTitle
                font.weight: Tokens.weightSemiBold
                color: Tokens.textPrimary
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                lineHeight: 1.35
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textBody
                color: Tokens.textSecondary
                text: {
                    if (!root.ocr.available)
                        return qsTr("Windows has no OCR language installed. Add one in "
                                  + "Settings › Time & language › Language & region, "
                                  + "under a language's options.");
                    if (root.ocr.pagesNeedingText === 0)
                        return qsTr("Every page in this document already has text. "
                                  + "Recognition would only add guesses on top of "
                                  + "text that is already correct.");
                    return qsTr("%n page(s) have no text layer. Recognition adds one "
                              + "underneath the image, so the scan looks identical "
                              + "but becomes searchable and selectable.",
                                "", root.ocr.pagesNeedingText);
                }
            }

            // Language. Windows decides which are available, so the list is
            // read from it rather than hard-coded and hoped for.
            Column {
                width: parent.width
                spacing: Tokens.space2
                visible: root.ocr.available && root.ocr.languages.length > 1

                Text {
                    text: qsTr("Language")
                    font.family: Tokens.fontFamily
                    font.pixelSize: Tokens.textSmall
                    font.weight: Tokens.weightMedium
                    color: Tokens.textSecondary
                }

                Flow {
                    width: parent.width
                    spacing: Tokens.space2

                    Repeater {
                        model: root.ocr.languages

                        delegate: Squircle {
                            required property var modelData

                            readonly property bool current: modelData === root.ocr.language

                            width: tag.implicitWidth + Tokens.space4 * 2
                            height: Tokens.controlHeight
                            radius: Tokens.radiusSmall
                            fillColor: current ? Tokens.accentSubtle
                                     : hover.hovered ? Tokens.hoverOverlay
                                     : "transparent"
                            strokeColor: current ? Tokens.accent : Tokens.separator
                            strokeWidth: 1

                            Behavior on fillColor { ColorAnimation { duration: Motion.instant } }

                            Text {
                                id: tag
                                anchors.centerIn: parent
                                text: modelData
                                font.family: Tokens.fontFamily
                                font.pixelSize: Tokens.textSmall
                                color: current ? Tokens.accent : Tokens.textPrimary
                            }

                            HoverHandler { id: hover }
                            TapHandler { onTapped: root.ocr.language = modelData }
                        }
                    }
                }
            }

            // Progress. A thin determinate bar: recognition is slow enough that
            // an indeterminate spinner would say nothing useful.
            Column {
                width: parent.width
                spacing: Tokens.space2
                visible: root.ocr.busy

                Text {
                    text: qsTr("Recognising… %1%").arg(Math.round(root.ocr.progress * 100))
                    font.family: Tokens.fontFamily
                    font.pixelSize: Tokens.textSmall
                    color: Tokens.textSecondary
                }

                Rectangle {
                    width: parent.width
                    height: 3
                    radius: 1.5
                    color: Tokens.hoverOverlay

                    Rectangle {
                        width: parent.width * root.ocr.progress
                        height: parent.height
                        radius: parent.radius
                        color: Tokens.accent
                        Behavior on width { NumberAnimation { duration: Motion.fast } }
                    }
                }
            }

            Item { width: 1; height: Tokens.space1 }

            Row {
                anchors.right: parent.right
                spacing: Tokens.space2

                LumenButton {
                    text: root.ocr.busy ? qsTr("Cancel") : qsTr("Close")
                    variant: LumenButton.Variant.Secondary
                    onClicked: {
                        if (root.ocr.busy)
                            root.ocr.cancel();
                        else
                            root.close();
                    }
                }

                LumenButton {
                    text: qsTr("Recognise")
                    variant: LumenButton.Variant.Primary
                    enabled: root.ocr.available
                             && !root.ocr.busy
                             && root.ocr.pagesNeedingText > 0
                    onClicked: root.ocr.recogniseDocument()
                }
            }
        }
    }
}
