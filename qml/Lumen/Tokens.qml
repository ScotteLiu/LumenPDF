pragma Singleton

import QtQuick

// The single source of truth for every visual constant in the app.
//
// No component may hard-code a colour, radius, or spacing value. Switching
// theme is therefore one property assignment, and a future accent-colour
// setting is one more.
QtObject {
    id: tokens

    // -- Theme -------------------------------------------------------------
    property bool dark: true

    // -- Colour ------------------------------------------------------------
    // Restraint is the point: a near-monochrome surface stack plus exactly one
    // accent. Anything that needs to stand out earns it through contrast and
    // spacing, not through a new hue.
    readonly property color accent:          dark ? "#0A84FF" : "#0071E3"
    readonly property color accentPressed:   dark ? "#0972D3" : "#0062C4"
    readonly property color accentSubtle:    Qt.rgba(tokens.accent.r, tokens.accent.g, tokens.accent.b, 0.14)

    readonly property color background:      dark ? "#1C1C1E" : "#FFFFFF"
    readonly property color surface:         dark ? "#242426" : "#F5F5F7"
    readonly property color surfaceElevated: dark ? "#2C2C2E" : "#FFFFFF"
    readonly property color canvas:          dark ? "#141416" : "#E8E8ED"  // behind the pages

    readonly property color textPrimary:     dark ? "#F5F5F7" : "#1D1D1F"
    readonly property color textSecondary:   dark ? "#98989D" : "#6E6E73"
    readonly property color textTertiary:    dark ? "#6C6C70" : "#A1A1A6"
    readonly property color textOnAccent:    "#FFFFFF"

    readonly property color separator:       dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.09)
    readonly property color hoverOverlay:    dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.05)
    readonly property color pressOverlay:    dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.09)
    readonly property color scrim:           dark ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(0, 0, 0, 0.28)

    readonly property color shadow:          dark ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(0, 0, 0, 0.18)

    // -- Spacing (8pt grid) ------------------------------------------------
    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 24
    readonly property int space6: 32
    readonly property int space7: 48

    // -- Corner radius -----------------------------------------------------
    // Feeding these to Squircle, not Rectangle.radius, is what separates the
    // app visually from every other Qt application.
    readonly property real radiusSmall:  7
    readonly property real radiusMedium: 11
    readonly property real radiusLarge:  16
    readonly property real radiusXLarge: 22

    // -- Typography --------------------------------------------------------
    // Inter is the redistributable stand-in for SF Pro; the system UI font is
    // the fallback so the app still looks right before fonts are bundled.
    readonly property string fontFamily: "Inter"
    readonly property string monoFamily: "JetBrains Mono"

    readonly property int textCaption: 11
    readonly property int textSmall:   12
    readonly property int textBody:    13
    readonly property int textLarge:   15
    readonly property int textTitle:   20
    readonly property int textDisplay: 28

    readonly property int weightRegular:  Font.Normal
    readonly property int weightMedium:   Font.Medium
    readonly property int weightSemiBold: Font.DemiBold

    // -- Control metrics ---------------------------------------------------
    readonly property int controlHeight:      30
    readonly property int controlHeightLarge: 36
    readonly property int toolbarHeight:      52
    readonly property int sidebarWidth:       248
    readonly property int iconSize:           18
}
