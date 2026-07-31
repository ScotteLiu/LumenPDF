import QtQuick
import Lumen
import Lumen.Backend

// Scripted demo, recorded frame by frame for the product page.
//
// The demo drives the real application through its real controllers -- nothing
// here is a mock-up or a staged screenshot. If a feature breaks, the demo
// breaks with it, which is the point.
//
// Frames are grabbed synchronously after each step's animations have had time
// to settle, so the recording cannot drift out of step with what it is
// recording. Wall-clock timing would produce torn frames on a busy machine.
Item {
    id: root

    required property var view
    // Named `app`, not `window`: a property whose name matches the id it is
    // bound to resolves to itself and silently becomes undefined. This is the
    // third time that has cost an hour, so nothing here shares a name with an id.
    required property var app

    readonly property bool active: Platform.recordDir.length > 0

    // 25 fps is enough for UI motion and keeps the encode small. The grab
    // itself costs more than a frame at 60, so asking for 60 would produce
    // stutter that the application does not actually have.
    readonly property int fps: 25

    property int frameIndex: 0
    property int step: 0

    // Each entry: how many frames to hold, and what to do when it starts.
    // Written as data so the pacing can be read at a glance.
    readonly property var timeline: [
        { hold: 20, act: function () {} },                                  // settle
        { hold: 45, act: function () { root.view.scrollBy(900) } },         // scroll in
        { hold: 30, act: function () { root.view.zoomBy(1.25) } },
        { hold: 30, act: function () { root.view.zoomBy(0.8) } },


        { hold: 10, act: function () { Document.search.query = "cycle" } }, // search
        { hold: 55, act: function () { app.sidebarPanel.tab = 2 } },
        { hold: 30, act: function () { Document.search.next() } },
        { hold: 30, act: function () { Document.search.next() } },

        { hold: 10, act: function () { Document.search.query = "" } },
        { hold: 15, act: function () { app.sidebarPanel.tab = 0 } },

        // Select a paragraph, then highlight it. The floating bar appears on
        // its own once the drag ends, exactly as it does for a real pointer.
        { hold: 10, act: function () {
            Document.selection.begin(0, Qt.point(72, 150));
            Document.selection.extend(0, Qt.point(470, 205));
            Document.selection.end();
        } },
        { hold: 45, act: function () {} },
        { hold: 40, act: function () {
            Document.annotate.color = Qt.rgba(1.0, 0.84, 0.25, 0.40);
            Document.annotate.applyToSelection(AnnotationType.Highlight);
        } },

        { hold: 25, act: function () { root.view.scrollBy(700) } },
        { hold: 35, act: function () {} }
    ]

    property int stepFramesLeft: 0

    Component.onCompleted: if (active) start()

    function start() {
        step = 0;
        stepFramesLeft = 0;
        driver.running = true;
    }

    FrameAnimation {
        id: driver
        running: false

        onTriggered: {
            if (root.stepFramesLeft <= 0) {
                if (root.step >= root.timeline.length) {
                    driver.running = false;
                    Platform.markTiming("demo-complete");
                    Qt.callLater(Qt.quit);
                    return;
                }

                const entry = root.timeline[root.step];
                root.step += 1;
                root.stepFramesLeft = entry.hold;
                entry.act();
            }

            root.stepFramesLeft -= 1;
            Platform.captureFrame(root.app, root.frameIndex);
            root.frameIndex += 1;
        }
    }
}
