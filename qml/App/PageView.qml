import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import Lumen
import Lumen.Backend

// The document canvas.
//
// Performance notes, since this is the screen the product is judged on:
//   * ListView virtualises -- only visible delegates exist, so a 5000-page
//     file costs the same as a 5-page one.
//   * Page rasters come from the "pdfpage" image provider, which renders on a
//     worker pool. The GUI thread never touches PDFium.
//   * The requested pixel width is quantised, so dragging a zoom or resizing
//     the window does not queue a re-render on every single frame.
//   * cacheBuffer keeps a screen's worth of pages warm above and below, which
//     is what removes the white flash when scrolling fast.
Item {
    id: root

    property real zoom: 1.0
    readonly property int currentPage: pageList.currentIndex

    readonly property real minZoom: 0.15
    readonly property real maxZoom: 8.0

    // Quantising to 128px buckets means a smooth zoom re-renders a handful of
    // times instead of once per frame, and cache hits survive small resizes.
    readonly property int _renderBucket: 128

    function zoomBy(factor) {
        zoom = Math.max(minZoom, Math.min(maxZoom, zoom * factor));
    }

    function fitWidth() {
        if (pageList.count === 0)
            return;
        const available = root.width - Tokens.space6 * 2;
        const pointsWide = Document.pageWidthPoints(pageList.currentIndex);
        if (pointsWide > 0)
            zoom = available / (pointsWide * 96 / 72);
    }

    function goToPage(index) {
        pageList.positionViewAtIndex(index, ListView.Beginning);
        pageList.currentIndex = index;
    }

    Rectangle {
        anchors.fill: parent
        color: Tokens.canvas
    }

    ListView {
        id: pageList

        anchors.fill: parent
        topMargin: Tokens.space6
        bottomMargin: Tokens.space6
        spacing: Tokens.space5

        model: Document.pageModel

        // One viewport of pre-rendered pages in each direction.
        cacheBuffer: Math.round(root.height * 1.5)
        reuseItems: true

        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 2400
        maximumFlickVelocity: 6000

        ScrollBar.vertical: LumenScrollBar {}

        delegate: Item {
            id: pageSlot

            required property int index
            required property real aspectRatio
            required property real widthPoints

            // 96 dpi is the CSS reference; zoom multiplies on top of it.
            readonly property real pageWidth:
                Math.max(48, widthPoints * (96 / 72) * root.zoom)

            width: pageList.width
            height: pageWidth * aspectRatio

            Squircle {
                id: pageCard

                x: (pageList.width - pageSlot.pageWidth) / 2
                width: pageSlot.pageWidth
                height: pageSlot.height
                radius: Tokens.radiusSmall
                fillColor: "white"

                Image {
                    id: raster
                    anchors.fill: parent
                    asynchronous: true
                    cache: false            // the C++ side owns the LRU cache
                    smooth: true
                    mipmap: false
                    fillMode: Image.PreserveAspectFit

                    // Bucketed width keeps re-render churn off the worker pool.
                    readonly property int renderWidth:
                        Math.ceil(pageSlot.pageWidth * Screen.devicePixelRatio
                                  / root._renderBucket) * root._renderBucket

                    source: "image://pdfpage/" + pageSlot.index + "?w=" + renderWidth
                    sourceSize: Qt.size(renderWidth, 0)

                    opacity: status === Image.Ready ? 1.0 : 0.0
                    Behavior on opacity {
                        NumberAnimation {
                            duration: Motion.normal
                            easing.type: Easing.OutCubic
                        }
                    }
                }

                // Page number, floating just outside the sheet.
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.bottom
                    anchors.topMargin: Tokens.space2
                    text: pageSlot.index + 1
                    font.family: Tokens.fontFamily
                    font.pixelSize: Tokens.textCaption
                    color: Tokens.textTertiary
                }
            }

            // Drop shadow: two offset squircles at low opacity rather than a
            // real blur. A MultiEffect here would push every page raster
            // through an offscreen texture on every frame, which is exactly
            // the cost this view exists to avoid.
            Squircle {
                z: -1
                x: pageCard.x - 1
                y: 3
                width: pageCard.width + 2
                height: pageCard.height
                radius: Tokens.radiusSmall + 1
                fillColor: Tokens.shadow
                opacity: 0.35
            }
            Squircle {
                z: -2
                x: pageCard.x - 4
                y: 8
                width: pageCard.width + 8
                height: pageCard.height
                radius: Tokens.radiusSmall + 4
                fillColor: Tokens.shadow
                opacity: 0.18
            }
        }
    }

    // Ctrl+wheel zooms, plain wheel scrolls. Sits above the ListView so it
    // sees the event first, and declines anything without the modifier so
    // normal scrolling falls straight through.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        onWheel: (wheel) => {
            if (wheel.modifiers & Qt.ControlModifier) {
                root.zoomBy(wheel.angleDelta.y > 0 ? 1.1 : 1 / 1.1);
                wheel.accepted = true;
            } else {
                wheel.accepted = false;
            }
        }
    }

    // Zoom eases rather than snapping, so pinch and Ctrl+wheel feel continuous.
    Behavior on zoom {
        NumberAnimation {
            duration: Motion.fast
            easing.type: Easing.OutCubic
        }
    }
}
