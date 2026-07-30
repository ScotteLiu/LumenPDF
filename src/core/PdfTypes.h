#pragma once

#include <QRectF>
#include <QString>
#include <QVector>

namespace lumen {

// Text markup annotation kinds, in PDF's own terms.
//
// All four attach to a run of text via quadpoints, so they share one code
// path; only the subtype and the default colour differ.
enum class MarkupType {
    Highlight,
    Underline,
    StrikeOut,
    Squiggly,
};

// One search match.
//
// Rectangles are in PDF points with a **top-left** origin -- converted from
// PDFium's bottom-left user space at extraction time, so nothing downstream
// has to think about which way is up.
struct SearchHit {
    int pageIndex = -1;
    int charIndex = 0;
    int charCount = 0;
    QString snippet;      // surrounding text, for the results list
    int snippetMatchStart = 0;  // where the match begins inside `snippet`
    int snippetMatchLength = 0;
    QVector<QRectF> rects;
};

// Outcome of a redaction.
//
// `blackedOut` is what was actually destroyed, which can be larger than what
// was asked for -- see PdfDocument::redactRegions for why. Reporting it back is
// what lets the UI black out exactly the area whose content is gone, rather
// than the area the user happened to select.
struct RedactionResult {
    bool ok = false;
    int objectsRemoved = 0;
    int textObjectsRemoved = 0;
    int imageObjectsRemoved = 0;
    QVector<QRectF> blackedOut;
};

// One entry in the document outline (PDF "bookmarks"), pre-flattened.
//
// A flat list with a depth field beats a real tree here: the QML side needs a
// linear model anyway, and expand/collapse is just a visibility filter over
// the same array.
struct OutlineItem {
    QString title;
    int pageIndex = -1;   // -1 when the destination cannot be resolved
    int depth = 0;
    bool hasChildren = false;
};

} // namespace lumen
