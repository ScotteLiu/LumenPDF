import QtQuick
import Lumen
import Lumen.Backend

// Drives the performance benchmarks and reports what they measured.
//
// Frames are counted with FrameAnimation, which ticks once per rendered frame,
// rather than by timing a loop. A loop measures how fast the loop runs; this
// measures how many frames actually reached the screen, which is the number
// that corresponds to what scrolling feels like.
//
// Activated only by LUMEN_BENCH, so it does not exist during normal use.
Item {
    id: root

    // Named `view`, not `pageView`: a property with the same name as the id it is
    // bound to resolves to itself, and the binding silently becomes undefined.
    required property var view

    // Warm-up is discarded: the first second is spent rasterising the pages
    // that scroll into view for the first time, and averaging it in would
    // measure the cache filling rather than the steady state.
    readonly property int warmupMs: 1200
    readonly property int measureMs: 4000

    // Points per second. Roughly a fast but plausible flick, sustained.
    readonly property real scrollSpeed: 1400

    property int frames: 0
    property real elapsed: 0
    property bool measuring: false

    readonly property bool active: Platform.benchmark.length > 0

    Component.onCompleted: {
        if (active && Platform.benchmark === "scroll")
            warmup.start();
    }

    Timer {
        id: warmup
        interval: root.warmupMs
        onTriggered: {
            root.frames = 0;
            root.elapsed = 0;
            root.measuring = true;
            measureTimer.start();
        }
    }

    Timer {
        id: measureTimer
        interval: root.measureMs
        onTriggered: {
            root.measuring = false;
            Platform.reportFrameRate("scroll", root.frames, root.elapsed);
        }
    }

    FrameAnimation {
        // Runs through warm-up as well so scrolling starts immediately; only
        // the counters are gated on `measuring`.
        running: root.active && Platform.benchmark === "scroll"

        onTriggered: {
            // frameTime is the seconds since the previous frame, so scrolling
            // advances by distance rather than by frame count -- the scroll
            // speed stays the same whether the machine renders at 30 or 144 Hz.
            root.view.scrollBy(root.scrollSpeed * frameTime);

            if (root.measuring) {
                root.frames += 1;
                root.elapsed += frameTime;
            }
        }
    }
}
