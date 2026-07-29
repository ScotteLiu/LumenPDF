import QtQuick
import QtQuick.Controls.Basic
import Lumen
import Lumen.Backend

// Page thumbnails.
//
// Deliberately requests a small render width: thumbnails are a separate cache
// bucket from the main view, so scrolling the rail never evicts the pages the
// reader is actually looking at.
Item {
    id: root

    property int currentIndex: 0
    signal pageRequested(int index)

    readonly property int thumbWidth: 132

    ListView {
        id: rail

        anchors.fill: parent
        anchors.topMargin: Tokens.space3
        anchors.bottomMargin: Tokens.space3
        spacing: Tokens.space3

        model: Document.pageModel
        cacheBuffer: 800
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: LumenScrollBar {}

        delegate: Item {
            required property int index
            required property real aspectRatio

            width: rail.width
            height: root.thumbWidth * aspectRatio + Tokens.space5

            Squircle {
                id: card

                x: (rail.width - root.thumbWidth) / 2
                width: root.thumbWidth
                height: root.thumbWidth * aspectRatio
                radius: Tokens.radiusSmall
                fillColor: "white"
                strokeColor: index === root.currentIndex ? Tokens.accent : Tokens.separator
                strokeWidth: index === root.currentIndex ? 2 : 1

                scale: hover.hovered && index !== root.currentIndex ? 1.03 : 1.0

                Behavior on strokeColor { ColorAnimation { duration: Motion.fast } }
                Behavior on scale {
                    SpringAnimation {
                        spring: Motion.spring
                        damping: Motion.damping
                        epsilon: Motion.epsilon
                    }
                }

                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    asynchronous: true
                    cache: false
                    fillMode: Image.PreserveAspectFit
                    source: "image://pdfpage/" + index + "?w=" + Math.round(root.thumbWidth * 2)
                    sourceSize: Qt.size(Math.round(root.thumbWidth * 2), 0)

                    opacity: status === Image.Ready ? 1.0 : 0.0
                    Behavior on opacity {
                        NumberAnimation { duration: Motion.normal; easing.type: Easing.OutCubic }
                    }
                }
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: card.bottom
                anchors.topMargin: Tokens.space1
                text: index + 1
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textCaption
                font.weight: index === root.currentIndex
                             ? Tokens.weightSemiBold : Tokens.weightRegular
                color: index === root.currentIndex
                       ? Tokens.accent : Tokens.textTertiary
            }

            HoverHandler { id: hover }

            TapHandler {
                onTapped: root.pageRequested(index)
            }
        }
    }
}
