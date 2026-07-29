import QtQuick
import QtQuick.Controls.Basic
import Lumen
import Lumen.Backend

// Search results.
//
// Results stream in while the scan is still running, so the list is useful
// before it is complete. The count and the thin progress line at the top are
// the only feedback -- no spinner, no modal, no blocking.
Item {
    id: root

    readonly property var search: Document.search

    // -- Header: count, options, navigation ---------------------------------
    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Tokens.controlHeightLarge + Tokens.space2

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Tokens.space4
            anchors.verticalCenter: parent.verticalCenter
            font.family: Tokens.fontFamily
            font.pixelSize: Tokens.textSmall
            color: Tokens.textSecondary
            text: {
                if (root.search.query.length < 2)
                    return qsTr("Type to search");
                if (root.search.count === 0)
                    return root.search.status === SearchStatus.Running
                           ? qsTr("Searching…") : qsTr("No matches");
                const n = root.search.count;
                const pos = root.search.currentIndex + 1;
                return qsTr("%1 of %2").arg(pos).arg(n)
                     + (root.search.status === SearchStatus.Running ? "…" : "");
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Tokens.space2
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0

            LumenIconButton {
                iconPath: Icons.chevronLeft
                tooltip: qsTr("Previous match  (Shift+Enter)")
                enabled: root.search.count > 0
                onClicked: root.search.previous()
            }

            LumenIconButton {
                iconPath: Icons.chevronRight
                tooltip: qsTr("Next match  (Enter)")
                enabled: root.search.count > 0
                onClicked: root.search.next()
            }
        }
    }

    // Progress hairline. Only visible while a scan is in flight.
    Rectangle {
        id: progressTrack
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 2
        color: "transparent"

        Rectangle {
            width: parent.width * root.search.progress
            height: parent.height
            color: Tokens.accent
            opacity: root.search.status === SearchStatus.Running ? 1.0 : 0.0

            Behavior on width { NumberAnimation { duration: Motion.fast } }
            Behavior on opacity { NumberAnimation { duration: Motion.normal } }
        }
    }

    // -- Results ------------------------------------------------------------
    ListView {
        id: list

        anchors.top: progressTrack.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: Tokens.space1

        model: root.search
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds

        currentIndex: root.search.currentIndex
        highlightMoveDuration: Motion.normal

        ScrollBar.vertical: LumenScrollBar {}

        delegate: Item {
            id: hit

            required property int index
            required property int pageIndex
            required property string snippet
            required property int matchStart
            required property int matchLength

            width: list.width
            height: snippetText.implicitHeight + Tokens.space4

            readonly property bool isCurrent: index === root.search.currentIndex

            Squircle {
                anchors.fill: parent
                anchors.leftMargin: Tokens.space2
                anchors.rightMargin: Tokens.space2
                anchors.topMargin: 1
                anchors.bottomMargin: 1
                radius: Tokens.radiusSmall
                fillColor: hit.isCurrent ? Tokens.accentSubtle
                         : hover.hovered ? Tokens.hoverOverlay
                         : "transparent"
                Behavior on fillColor { ColorAnimation { duration: Motion.instant } }
            }

            Text {
                id: pageLabel
                x: Tokens.space4
                y: Tokens.space2
                text: qsTr("Page %1").arg(hit.pageIndex + 1)
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textCaption
                font.weight: Tokens.weightMedium
                font.features: ({ "tnum": 1 })
                color: hit.isCurrent ? Tokens.accent : Tokens.textTertiary
            }

            Text {
                id: snippetText
                x: Tokens.space4
                y: pageLabel.y + pageLabel.height + 2
                width: parent.width - Tokens.space4 - Tokens.space4
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textSmall
                color: Tokens.textSecondary

                // The matched substring is emphasised inline. Built as rich
                // text rather than three Text items so it wraps as one run.
                textFormat: Text.StyledText
                text: {
                    const s = hit.snippet;
                    if (hit.matchLength <= 0)
                        return root._escape(s);
                    const a = root._escape(s.substring(0, hit.matchStart));
                    const b = root._escape(s.substring(hit.matchStart,
                                                       hit.matchStart + hit.matchLength));
                    const c = root._escape(s.substring(hit.matchStart + hit.matchLength));
                    return a + "<b><font color=\"" + Tokens.textPrimary + "\">"
                         + b + "</font></b>" + c;
                }
            }

            HoverHandler { id: hover }
            TapHandler { onTapped: root.search.currentIndex = hit.index }
        }
    }

    // StyledText would otherwise treat document content as markup.
    function _escape(text) {
        return text.replace(/&/g, "&amp;")
                   .replace(/</g, "&lt;")
                   .replace(/>/g, "&gt;");
    }
}
