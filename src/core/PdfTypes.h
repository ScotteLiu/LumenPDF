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

// A text-drawing object on a page, as PDFium models it.
//
// The unit of editing is the whole object, because PDFium can replace a text
// object's string but cannot split one. In most real PDFs an object is a line
// or a phrase, occasionally a whole paragraph -- so editing is honest about
// operating on a run of text rather than pretending to be a word processor.
struct TextObjectInfo {
    bool valid = false;
    int objectIndex = -1;
    QString text;
    QRectF bounds;        // PDF points, top-left origin
    double fontSize = 0.0;

    // True when the run is long enough that replacing it is likely to disturb
    // spacing the original author set by hand. Used to warn, not to refuse.
    bool spansMuchText = false;
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

// A link annotation resolved under the pointer.
//
// PDF links come in many action flavours; only two of them are worth honouring
// in a viewer. A jump inside the document is safe and immediate. A URI leaves
// the application entirely, so it is carried back as text and shown to the
// person before anything opens -- a link's visible label and its actual target
// are independent strings in a PDF, and a document is untrusted input.
// Everything else (launch a program, submit a form, run JavaScript) is
// deliberately reported as None.
struct LinkTarget {
    enum class Kind { None, Page, Uri };

    Kind kind = Kind::None;
    int pageIndex = -1;   // Kind::Page
    QString uri;          // Kind::Uri
    QRectF rect;          // the link's own area, PDF points, top-left origin
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
