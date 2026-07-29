pragma Singleton

import QtQuick

// Icon geometry, as SVG path data on a 24x24 grid.
//
// Drawn rather than borrowed: a font glyph (Segoe MDL2, emoji, arbitrary
// Unicode) renders differently on every platform and several of the ones that
// look right on Windows are simply absent on macOS and Linux. Paths render
// identically everywhere and stay crisp at any DPI.
//
// House style, kept consistent so the set reads as one family:
//   * 24x24 box, 2px optical margin
//   * stroked, never filled, 1.75 stroke width
//   * round caps and joins
//   * no detail smaller than 2 units
QtObject {
    readonly property string menu:      "M4 7h16 M4 12h16 M4 17h16"

    readonly property string open:      "M5 20a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h4l2 2.5h6a2 2 0 0 1 2 2V18a2 2 0 0 1-2 2z"

    readonly property string zoomIn:    "M11 18a7 7 0 1 0 0-14 7 7 0 0 0 0 14z M20 20l-4-4 M11 8v6 M8 11h6"
    readonly property string zoomOut:   "M11 18a7 7 0 1 0 0-14 7 7 0 0 0 0 14z M20 20l-4-4 M8 11h6"
    readonly property string search:    "M11 18a7 7 0 1 0 0-14 7 7 0 0 0 0 14z M20 20l-4-4"

    readonly property string fitWidth:  "M3 6v12 M21 6v12 M7 12h10 M7 12l3-3 M7 12l3 3 M17 12l-3-3 M17 12l-3 3"
    readonly property string fitPage:   "M4 9V5h4 M20 9V5h-4 M4 15v4h4 M20 15v4h-4"

    readonly property string sun:       "M12 16a4 4 0 1 0 0-8 4 4 0 0 0 0 8z M12 3v2 M12 19v2 M5.6 5.6l1.4 1.4 M17 17l1.4 1.4 M3 12h2 M19 12h2 M5.6 18.4L7 17 M17 7l1.4-1.4"
    readonly property string moon:      "M20 14.5A8.5 8.5 0 0 1 9.5 4a7 7 0 1 0 10.5 10.5z"

    readonly property string chevronLeft:  "M14.5 6l-6 6 6 6"
    readonly property string chevronRight: "M9.5 6l6 6-6 6"
    readonly property string close:        "M6.5 6.5l11 11 M17.5 6.5l-11 11"

    readonly property string thumbnails:   "M4 4h6v6H4z M14 4h6v6h-6z M4 14h6v6H4z M14 14h6v6h-6z"
    readonly property string outline:      "M4 6h3 M10 6h10 M4 12h3 M10 12h10 M4 18h3 M10 18h10"
}
