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

    // The page the reader is actually looking at, not ListView.currentIndex --
    // that only moves on keyboard navigation and would stay at 0 forever while
    // scrolling with the wheel.
    property int currentPage: 0

    readonly property real minZoom: 0.15
    readonly property real maxZoom: 8.0

    // Quantising to 128px buckets means a smooth zoom re-renders a handful of
    // times instead of once per frame, and cache hits survive small resizes.
    readonly property int _renderBucket: 128

    // rectsForPage() is a plain function call, so QML cannot know when its
    // answer changes. Bumping this counter is what re-evaluates the highlight
    // Repeaters -- cheaper and far more predictable than exposing one list
    // property per page.
    property int _highlightGeneration: 0

    Connections {
        target: Document.search
        function onCountChanged() { root._highlightGeneration++ }
        function onCurrentIndexChanged() { root._highlightGeneration++ }
    }

    // Same trick for the selection: it changes on every mouse move during a
    // drag, so the counter is what makes the overlay follow the pointer.
    property int _selectionGeneration: 0

    Connections {
        target: Document.selection
        function onChanged() { root._selectionGeneration++ }
    }

    function zoomBy(factor) {
        zoom = Math.max(minZoom, Math.min(maxZoom, zoom * factor));
    }

    function fitWidth() {
        if (pageList.count === 0)
            return;
        const available = root.width - Tokens.space6 * 2;
        const pointsWide = Document.pageWidthPoints(root.currentPage);
        if (pointsWide > 0)
            zoom = available / (pointsWide * 96 / 72);
    }

    // Scrolls to a page with an eased motion rather than teleporting.
    //
    // positionViewAtIndex is the only reliable way to find the target offset
    // (delegate heights vary), so: jump, read the resulting contentY, snap
    // back, and animate to it.
    function goToPage(index) {
        if (index < 0 || index >= pageList.count)
            return;

        const from = pageList.contentY;
        pageList.positionViewAtIndex(index, ListView.Beginning);
        const to = pageList.contentY - pageList.topMargin;

        root.currentPage = index;

        if (Math.abs(to - from) < 2)
            return;

        pageList.contentY = from;
        scrollTo.from = from;
        scrollTo.to = to;
        scrollTo.restart();
    }

    NumberAnimation {
        id: scrollTo
        target: pageList
        property: "contentY"
        duration: Motion.normal
        easing.type: Easing.Bezier
        easing.bezierCurve: Motion.standard
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

        // One viewport of pre-rendered pages in each direction. Clamped at
        // zero: during the first layout pass height is still -1.
        cacheBuffer: Math.max(0, Math.round(root.height * 1.5))
        reuseItems: true

        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 2400
        maximumFlickVelocity: 6000

        ScrollBar.vertical: LumenScrollBar {}

        // Which page counts as "current": the one under a point slightly
        // above centre, which is where the eye sits when reading.
        onContentYChanged: currentPageTick.restart()

        Timer {
            id: currentPageTick
            interval: 60      // coalesce: contentY changes every frame
            onTriggered: {
                const probeY = pageList.contentY + pageList.height * 0.35;
                const index = pageList.indexAt(pageList.width / 2, probeY);
                if (index >= 0)
                    root.currentPage = index;
            }
        }

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

                    // The generation suffix is what forces a reload after an
                    // edit: an Image whose source string has not changed will
                    // never re-fetch, however stale its pixels are.
                    source: "image://pdfpage/" + pageSlot.index
                            + "?w=" + renderWidth
                            + "&g=" + Document.renderGeneration
                    sourceSize: Qt.size(renderWidth, 0)

                    opacity: status === Image.Ready ? 1.0 : 0.0
                    Behavior on opacity {
                        NumberAnimation {
                            duration: Motion.normal
                            easing.type: Easing.OutCubic
                        }
                    }
                }

                // -- Text selection ---------------------------------------
                //
                // Drawn under the search highlights: a search hit inside a
                // selection should still read as a search hit.
                Repeater {
                    model: root._selectionGeneration >= 0
                           ? Document.selection.rectsForPage(pageSlot.index)
                           : []

                    delegate: Rectangle {
                        required property var modelData

                        readonly property real pointScale:
                            pageSlot.pageWidth / pageSlot.widthPoints

                        x: modelData.x * pointScale
                        y: modelData.y * pointScale
                        width: modelData.width * pointScale
                        height: modelData.height * pointScale

                        color: Tokens.accent
                        opacity: 0.28
                        radius: 1.5
                    }
                }

                // The floating action bar for the selection, shown on the
                // first page the selection touches and only once the drag has
                // finished. Loaded lazily -- it does not exist at all while
                // there is nothing selected.
                Loader {
                    id: selectionToolbar

                    readonly property var selRects:
                        root._selectionGeneration >= 0
                        ? Document.selection.rectsForPage(pageSlot.index)
                        : []

                    readonly property real pointScale:
                        pageSlot.pageWidth / pageSlot.widthPoints

                    active: !Document.selection.active
                            && selRects.length > 0
                            && pageSlot.index === Document.selection.firstPage

                    z: 50
                    asynchronous: false
                    sourceComponent: SelectionToolbar {
                        onDismissed: Document.selection.clear()
                    }

                    // Centred over the selection and floated above its top
                    // edge, clamped so it never leaves the page.
                    x: {
                        if (!item || selRects.length === 0)
                            return 0;
                        let left = Infinity, right = -Infinity;
                        for (const r of selRects) {
                            left = Math.min(left, r.x);
                            right = Math.max(right, r.x + r.width);
                        }
                        const centre = ((left + right) / 2) * pointScale;
                        return Math.max(Tokens.space2,
                               Math.min(pageSlot.pageWidth - item.width - Tokens.space2,
                                        centre - item.width / 2));
                    }

                    y: {
                        if (!item || selRects.length === 0)
                            return 0;
                        let top = Infinity;
                        for (const r of selRects)
                            top = Math.min(top, r.y);
                        return Math.max(Tokens.space2,
                                        top * pointScale - item.height - Tokens.space2);
                    }
                }

                // Pointer handling for this page. Coordinates are converted
                // from view pixels back to PDF points here, so nothing below
                // this line has to know about zoom.
                MouseArea {
                    id: textArea
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.IBeamCursor

                    // The ListView is a Flickable, and would otherwise steal
                    // the drag to scroll -- making text selection impossible
                    // with a mouse. Selection wins; the wheel still scrolls.
                    preventStealing: true

                    readonly property real toPoints:
                        pageSlot.widthPoints / pageSlot.pageWidth

                    function pointAt(mx, my) {
                        return Qt.point(mx * toPoints, my * toPoints);
                    }

                    onPressed: (mouse) => {
                        const p = pointAt(mouse.x, mouse.y);
                        Document.selection.begin(pageSlot.index, p);
                    }

                    onPositionChanged: (mouse) => {
                        if (!pressed)
                            return;
                        const p = pointAt(mouse.x, mouse.y);
                        Document.selection.extend(pageSlot.index, p);
                    }

                    onReleased: Document.selection.end()

                    onDoubleClicked: (mouse) => {
                        const p = pointAt(mouse.x, mouse.y);
                        Document.selection.selectWordAt(pageSlot.index, p);
                    }
                }

                // -- Search highlights ------------------------------------
                //
                // Rebuilt whenever the result set or the selection changes.
                // Rectangles arrive in PDF points with a top-left origin, so
                // the only transform needed is the points-to-pixels scale.
                Repeater {
                    model: root._highlightGeneration >= 0
                           ? Document.search.rectsForPage(pageSlot.index)
                           : []

                    delegate: Rectangle {
                        required property var modelData

                        // Not named `scale`: that is an existing writable
                        // Item property, and shadowing it makes the whole
                        // component fail to load.
                        readonly property real pointScale:
                            pageSlot.pageWidth / pageSlot.widthPoints

                        x: modelData.x * pointScale
                        y: modelData.y * pointScale
                        width: modelData.width * pointScale
                        height: modelData.height * pointScale

                        color: Tokens.accent
                        opacity: 0.22
                        radius: 2
                    }
                }

                // The selected match, drawn on top and much stronger.
                Repeater {
                    model: root._highlightGeneration >= 0
                           ? Document.search.currentRectsForPage(pageSlot.index)
                           : []

                    delegate: Rectangle {
                        required property var modelData

                        readonly property real pointScale:
                            pageSlot.pageWidth / pageSlot.widthPoints

                        x: modelData.x * pointScale
                        y: modelData.y * pointScale
                        width: modelData.width * pointScale
                        height: modelData.height * pointScale

                        color: Tokens.accent
                        opacity: 0.42
                        radius: 2

                        transformOrigin: Item.Center

                        // A single pulse on selection, so the eye finds the
                        // match immediately after a jump between pages.
                        SequentialAnimation on scale {
                            running: true
                            NumberAnimation { from: 1.0; to: 1.18; duration: 130; easing.type: Easing.OutCubic }
                            NumberAnimation { to: 1.0; duration: 200; easing.type: Easing.OutCubic }
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
