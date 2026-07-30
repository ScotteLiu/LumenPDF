import QtQuick
import Lumen

// Popover menu.
//
// Grows from the edge nearest its anchor rather than fading in centred, so the
// eye keeps track of where it came from. Closes on click-away, on Escape, and
// on choosing anything -- a menu that needs dismissing twice is a bug.
Item {
    id: root

    // Items: { label, icon, shortcut, enabled, separator, action }
    property var items: []

    // The item the menu should appear under.
    property Item anchorItem: null

    property int preferredWidth: 248

    readonly property bool opened: state === "open"

    signal closed()

    anchors.fill: parent
    visible: opacity > 0
    opacity: 0
    z: 500

    function open() { root.state = "open" }
    function close() {
        if (root.state !== "open")
            return;
        root.state = "";
        root.closed();
    }

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

    // Click-away. Transparent rather than dimmed: a menu is not modal enough
    // to justify darkening the whole document behind it.
    MouseArea {
        anchors.fill: parent
        enabled: root.opened
        onClicked: root.close()
    }

    Keys.onEscapePressed: root.close()

    Squircle {
        id: panel

        // Positioned under the anchor, pulled back inside the window if it
        // would otherwise hang off the right edge.
        x: {
            if (!root.anchorItem)
                return Tokens.space4;
            const p = root.anchorItem.mapToItem(root, 0, 0);
            return Math.max(Tokens.space2,
                   Math.min(root.width - width - Tokens.space2, p.x));
        }
        y: {
            if (!root.anchorItem)
                return Tokens.space4;
            const p = root.anchorItem.mapToItem(root, 0, 0);
            return p.y + root.anchorItem.height + Tokens.space1;
        }

        width: root.preferredWidth
        height: column.implicitHeight + Tokens.space2 * 2

        radius: Tokens.radiusMedium
        fillColor: Tokens.surfaceElevated
        strokeColor: Tokens.separator
        strokeWidth: 1

        // Grows downward from its top edge, matching where it is anchored.
        transformOrigin: Item.Top
        scale: root.opened ? 1.0 : 0.96

        Behavior on scale {
            SpringAnimation {
                spring: Motion.spring
                damping: Motion.damping
                epsilon: Motion.epsilon
            }
        }

        Column {
            id: column
            width: parent.width
            y: Tokens.space2

            Repeater {
                model: root.items

                delegate: Loader {
                    required property int index
                    required property var modelData

                    width: column.width
                    sourceComponent: modelData.separator === true ? separatorRow : itemRow

                    Component {
                        id: separatorRow
                        Item {
                            width: column.width
                            height: Tokens.space3
                            LumenSeparator {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: Tokens.space3
                                anchors.rightMargin: Tokens.space3
                            }
                        }
                    }

                    Component {
                        id: itemRow
                        Item {
                            width: column.width
                            height: Tokens.controlHeightLarge

                            readonly property bool itemEnabled: modelData.enabled !== false

                            Squircle {
                                anchors.fill: parent
                                anchors.leftMargin: Tokens.space2
                                anchors.rightMargin: Tokens.space2
                                radius: Tokens.radiusSmall
                                fillColor: (rowHover.hovered && itemEnabled)
                                           ? Tokens.hoverOverlay : "transparent"
                                Behavior on fillColor {
                                    ColorAnimation { duration: Motion.instant }
                                }
                            }

                            LumenIcon {
                                id: rowIcon
                                x: Tokens.space4
                                anchors.verticalCenter: parent.verticalCenter
                                path: modelData.icon || ""
                                size: 16
                                color: itemEnabled ? Tokens.textSecondary : Tokens.textTertiary
                                visible: (modelData.icon || "") !== ""
                            }

                            Text {
                                x: rowIcon.visible ? rowIcon.x + rowIcon.width + Tokens.space3
                                                   : Tokens.space4
                                anchors.verticalCenter: parent.verticalCenter
                                width: shortcutLabel.x - x - Tokens.space2
                                elide: Text.ElideRight
                                text: modelData.label || ""
                                font.family: Tokens.fontFamily
                                font.pixelSize: Tokens.textBody
                                color: itemEnabled ? Tokens.textPrimary : Tokens.textTertiary
                            }

                            Text {
                                id: shortcutLabel
                                anchors.right: parent.right
                                anchors.rightMargin: Tokens.space4
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.shortcut || ""
                                font.family: Tokens.fontFamily
                                font.pixelSize: Tokens.textCaption
                                color: Tokens.textTertiary
                            }

                            HoverHandler { id: rowHover; enabled: itemEnabled }

                            TapHandler {
                                enabled: itemEnabled
                                onTapped: {
                                    // Close first: an action that opens a
                                    // dialog must not leave the menu sitting
                                    // behind it.
                                    root.close();
                                    if (modelData.action)
                                        modelData.action();
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
