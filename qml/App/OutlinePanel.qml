import QtQuick
import QtQuick.Controls.Basic
import Lumen
import Lumen.Backend

// Document outline (PDF bookmarks).
//
// Indentation carries the hierarchy; a disclosure chevron appears only on rows
// that actually have children, so the eye can skim the left edge and see the
// structure without reading any titles.
Item {
    id: root

    signal pageRequested(int index)

    property int currentPage: -1

    ListView {
        id: list

        anchors.fill: parent
        anchors.topMargin: Tokens.space2
        anchors.bottomMargin: Tokens.space2

        model: Document.outlineModel
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: LumenScrollBar {}

        delegate: Item {
            id: row

            required property int index
            required property string title
            required property int pageIndex
            required property int depth
            required property bool hasChildren
            required property bool expanded

            width: list.width
            height: Tokens.controlHeightLarge

            readonly property bool isCurrent:
                root.currentPage >= 0 && pageIndex === root.currentPage

            Squircle {
                anchors.fill: parent
                anchors.leftMargin: Tokens.space2
                anchors.rightMargin: Tokens.space2
                anchors.topMargin: 1
                anchors.bottomMargin: 1
                radius: Tokens.radiusSmall
                fillColor: row.isCurrent ? Tokens.accentSubtle
                         : hover.hovered ? Tokens.hoverOverlay
                         : "transparent"

                Behavior on fillColor { ColorAnimation { duration: Motion.instant } }
            }

            // Disclosure chevron. Rotates rather than swapping glyphs, so
            // expanding reads as one continuous motion.
            Item {
                id: chevron
                x: Tokens.space3 + row.depth * Tokens.space4
                width: 16
                height: parent.height
                visible: row.hasChildren

                LumenIcon {
                    anchors.centerIn: parent
                    path: Icons.chevronRight
                    size: 13
                    color: Tokens.textTertiary
                    rotation: row.expanded ? 90 : 0

                    Behavior on rotation {
                        NumberAnimation {
                            duration: Motion.fast
                            easing.type: Easing.Bezier
                            easing.bezierCurve: Motion.standard
                        }
                    }
                }

                TapHandler {
                    onTapped: Document.outline.toggle(row.index)
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: chevron.x + 18
                width: pageLabel.x - x - Tokens.space2
                text: row.title
                elide: Text.ElideRight
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textBody
                font.weight: row.depth === 0 ? Tokens.weightMedium : Tokens.weightRegular
                color: row.isCurrent ? Tokens.accent : Tokens.textPrimary

                Behavior on color { ColorAnimation { duration: Motion.instant } }
            }

            Text {
                id: pageLabel
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: Tokens.space4
                text: row.pageIndex >= 0 ? (row.pageIndex + 1) : ""
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textCaption
                font.features: ({ "tnum": 1 })
                color: Tokens.textTertiary
            }

            HoverHandler { id: hover }

            TapHandler {
                // The chevron has its own handler; this one covers the row.
                onTapped: if (row.pageIndex >= 0) root.pageRequested(row.pageIndex)
            }
        }
    }

    // Empty state -- many PDFs simply have no outline, and saying so is
    // better than showing a blank panel that looks broken.
    Column {
        anchors.centerIn: parent
        width: parent.width - Tokens.space5 * 2
        spacing: Tokens.space2
        visible: Document.outline.empty

        LumenIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            path: Icons.outline
            size: 26
            color: Tokens.textTertiary
            opacity: 0.6
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("This document has no outline")
            font.family: Tokens.fontFamily
            font.pixelSize: Tokens.textSmall
            color: Tokens.textTertiary
        }
    }
}
