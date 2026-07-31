import QtQuick
import Lumen
import Lumen.Backend

// Shown before a document is open, while one is loading, and on failure.
//
// The first screen the user ever sees, so it does the work of a landing page:
// one sentence, one action, and nothing else competing for attention.
//
// Once there is a history, the fastest way back to a document is the document
// itself, so recent files appear underneath -- but only then. An empty list
// dressed up as a section is worse than no section.
Item {
    id: root

    property bool loading: false
    property string errorText: ""

    signal openRequested()
    signal fileRequested(string path)

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

        // -- Recent -----------------------------------------------------
        Column {
            width: parent.width
            spacing: Tokens.space2
            visible: !root.loading
                     && root.errorText.length === 0
                     && Prefs.recentFiles.length > 0

            Item { width: 1; height: Tokens.space4 }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Recent")
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textSmall
                font.weight: Tokens.weightMedium
                color: Tokens.textTertiary
            }

            Repeater {
                // Five is what fits without the panel becoming a file manager.
                model: Prefs.recentFiles.slice(0, 5)

                delegate: Squircle {
                    required property var modelData

                    width: parent.width
                    height: Tokens.controlHeight + Tokens.space1
                    radius: Tokens.radiusSmall
                    fillColor: hover.hovered ? Tokens.hoverOverlay : "transparent"

                    Behavior on fillColor { ColorAnimation { duration: Motion.instant } }

                    LumenIcon {
                        id: clock
                        anchors.left: parent.left
                        anchors.leftMargin: Tokens.space3
                        anchors.verticalCenter: parent.verticalCenter
                        path: Icons.clock
                        size: 15
                        color: Tokens.textTertiary
                    }

                    Text {
                        anchors.left: clock.right
                        anchors.leftMargin: Tokens.space3
                        anchors.right: forget.left
                        anchors.rightMargin: Tokens.space2
                        anchors.verticalCenter: parent.verticalCenter
                        // Elide from the left: the file name is the end of the
                        // path and the part anyone recognises.
                        elide: Text.ElideLeft
                        text: modelData.name
                        font.family: Tokens.fontFamily
                        font.pixelSize: Tokens.textBody
                        color: Tokens.textPrimary
                    }

                    LumenIconButton {
                        id: forget
                        anchors.right: parent.right
                        anchors.rightMargin: Tokens.space1
                        anchors.verticalCenter: parent.verticalCenter
                        iconPath: Icons.close
                        tooltip: qsTr("Remove from this list")
                        opacity: hover.hovered ? 1 : 0
                        visible: opacity > 0
                        Behavior on opacity { NumberAnimation { duration: Motion.instant } }
                        onClicked: Prefs.removeRecent(modelData.path)
                    }

                    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.fileRequested(modelData.path) }
                }
            }
        }
    }
}
