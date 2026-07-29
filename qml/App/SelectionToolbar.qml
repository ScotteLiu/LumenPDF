import QtQuick
import Lumen
import Lumen.Backend

// The floating bar that appears over a finished text selection.
//
// Deliberately modeless and adjacent to the text rather than parked in the
// toolbar: the actions are about *this* selection, so they belong next to it.
// It appears only once the drag has ended -- popping up mid-drag would fight
// the pointer for the same screen space.
Squircle {
    id: root

    // Colour swatches offered for highlighting. Muted on purpose: a highlight
    // has to sit under black text without drowning it.
    readonly property var swatches: [
        Qt.rgba(1.00, 0.84, 0.25, 0.40),   // yellow
        Qt.rgba(0.45, 0.86, 0.55, 0.40),   // green
        Qt.rgba(0.42, 0.72, 1.00, 0.40),   // blue
        Qt.rgba(1.00, 0.55, 0.62, 0.40),   // pink
    ]

    signal dismissed()

    implicitWidth: row.implicitWidth + Tokens.space3 * 2
    implicitHeight: Tokens.controlHeightLarge + Tokens.space2

    radius: Tokens.radiusMedium
    fillColor: Tokens.surfaceElevated
    strokeColor: Tokens.separator
    strokeWidth: 1

    // Rises into place rather than blinking on, so it reads as attached to
    // the selection instead of overlaid on top of it.
    opacity: 0
    scale: 0.94
    transformOrigin: Item.Bottom

    Component.onCompleted: {
        opacity = 1;
        scale = 1;
    }

    Behavior on opacity {
        NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic }
    }
    Behavior on scale {
        SpringAnimation {
            spring: Motion.spring
            damping: Motion.damping
            epsilon: Motion.epsilon
        }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Tokens.space1

        LumenIconButton {
            iconPath: Icons.copy
            tooltip: qsTr("Copy  (Ctrl+C)")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: {
                Document.selection.copyToClipboard();
                root.dismissed();
            }
        }

        LumenSeparator {
            vertical: true
            height: Tokens.space5
            anchors.verticalCenter: parent.verticalCenter
        }

        // Highlight colours. Clicking a swatch both sets the colour and
        // applies it -- one gesture, not two.
        Repeater {
            model: root.swatches

            delegate: Item {
                required property int index
                required property var modelData

                width: Tokens.controlHeightLarge
                height: Tokens.controlHeightLarge
                anchors.verticalCenter: parent.verticalCenter

                Squircle {
                    anchors.centerIn: parent
                    width: 19
                    height: 19
                    radius: 6
                    fillColor: Qt.rgba(modelData.r, modelData.g, modelData.b, 0.95)
                    strokeColor: Qt.rgba(0, 0, 0, 0.18)
                    strokeWidth: 1

                    scale: swatchHover.hovered ? 1.18 : 1.0
                    Behavior on scale {
                        SpringAnimation {
                            spring: Motion.spring
                            damping: Motion.damping
                            epsilon: Motion.epsilon
                        }
                    }
                }

                HoverHandler { id: swatchHover }
                TapHandler {
                    onTapped: {
                        Document.annotate.color = modelData;
                        Document.annotate.applyToSelection(AnnotationType.Highlight);
                        root.dismissed();
                    }
                }
            }
        }

        LumenSeparator {
            vertical: true
            height: Tokens.space5
            anchors.verticalCenter: parent.verticalCenter
        }

        LumenIconButton {
            iconPath: Icons.underline
            tooltip: qsTr("Underline")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: {
                Document.annotate.applyToSelection(AnnotationType.Underline);
                root.dismissed();
            }
        }

        LumenIconButton {
            iconPath: Icons.strikeout
            tooltip: qsTr("Strike through")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: {
                Document.annotate.applyToSelection(AnnotationType.StrikeOut);
                root.dismissed();
            }
        }
    }
}
