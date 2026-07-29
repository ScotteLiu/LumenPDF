pragma Singleton

import QtQuick

// Motion constants.
//
// The rule: things the user directly caused move on a spring; things that
// merely change state (colour, opacity) use an ease-out curve. Nothing is
// linear, and nothing lasts long enough to feel like waiting.
QtObject {
    // -- Durations (ms) ----------------------------------------------------
    readonly property int instant: 90    // hover tint, press feedback
    readonly property int fast:    140   // small state changes
    readonly property int normal:  220   // panel reveal, popovers
    readonly property int slow:    340   // full-view transitions

    // -- Curves ------------------------------------------------------------
    // Decelerating: fast start, soft landing. This is the curve that reads as
    // "Apple" more than any other single choice.
    readonly property var standard: [0.32, 0.72, 0.0, 1.0, 1.0, 1.0]
    // For things leaving the screen -- accelerate away, no soft landing.
    readonly property var exit:     [0.4, 0.0, 0.9, 0.55, 1.0, 1.0]
    // Slight overshoot, for elements that "pop" into place.
    readonly property var emphasis: [0.34, 1.36, 0.64, 1.0, 1.0, 1.0]

    // -- Spring ------------------------------------------------------------
    // Damping just under critical: one barely-visible overshoot, then settle.
    readonly property real springValue:   0.0
    readonly property real spring:        3.0
    readonly property real damping:       0.32
    readonly property real epsilon:       0.25

    // -- Press feedback ----------------------------------------------------
    readonly property real pressScale:  0.97
    readonly property real hoverScale:  1.0
}
