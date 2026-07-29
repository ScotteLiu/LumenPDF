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

    readonly property string copy:      "M9 9h9a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H9a2 2 0 0 1-2-2v-9a2 2 0 0 1 2-2z M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"
    readonly property string underline: "M7 4v7a5 5 0 0 0 10 0V4 M5 20h14"
    readonly property string strikeout: "M7 5.5A3.5 3.5 0 0 1 10.5 3h3a3.5 3.5 0 0 1 3.4 2.7 M4 12h16 M17 15a3.5 3.5 0 0 1-3.5 3.5h-3A3.5 3.5 0 0 1 7 15"
    readonly property string save:      "M5 3h11l3 3v13a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z M8 3v5h7 M8 21v-6h8v6"

    readonly property string rotateLeft:  "M4 8h9a6 6 0 1 1 0 12H8 M4 8l4-4 M4 8l4 4"
    readonly property string rotateRight: "M20 8h-9a6 6 0 1 0 0 12h5 M20 8l-4-4 M20 8l-4 4"
    readonly property string trash:       "M4 7h16 M9 7V5a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1v2 M6 7l1 12a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2l1-12 M10 11v6 M14 11v6"
    readonly property string undo:        "M4 9h11a5 5 0 0 1 0 10H9 M4 9l4-4 M4 9l4 4"
    readonly property string redo:        "M20 9H9a5 5 0 0 0 0 10h6 M20 9l-4-4 M20 9l-4 4"

    readonly property string thumbnails:   "M4 4h6v6H4z M14 4h6v6h-6z M4 14h6v6H4z M14 14h6v6h-6z"
    readonly property string outline:      "M4 6h3 M10 6h10 M4 12h3 M10 12h10 M4 18h3 M10 18h10"
}
