#pragma once

#include "core/PdfTypes.h"

#include <QColor>
#include <QObject>
#include <QPointF>
#include <QSharedPointer>
#include <QVariantList>

namespace lumen {

class PdfDocument;
class SelectionController;

// Creating and removing annotations.
//
// Markup annotations are created from the current text selection rather than
// from free-drawn rectangles: that is what makes a highlight land exactly on
// the words, wrap correctly across lines, and survive being re-read by other
// PDF software.
class AnnotationController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(bool canAnnotate READ canAnnotate NOTIFY canAnnotateChanged)

public:
    enum Type {
        Highlight,
        Underline,
        StrikeOut,
        Squiggly,
    };
    Q_ENUM(Type)

    explicit AnnotationController(SelectionController *selection,
                                  QObject *parent = nullptr);

    void setDocument(const QSharedPointer<PdfDocument> &document);

    QColor color() const { return m_color; }
    void setColor(const QColor &color);

    bool canAnnotate() const;

    // Applies markup to every page the selection touches, then clears it --
    // leaving the selection on top of a fresh highlight just hides the result.
    Q_INVOKABLE bool applyToSelection(Type type);

    // Deletes the topmost annotation under a point, if any.
    Q_INVOKABLE bool removeAt(int pageIndex, const QPointF &point);

    // Stamps ink onto a page.
    //
    // `strokes` is a list of lists of points normalised to the unit square, as
    // QML produces them. `widthPoints` sizes the signature; the height follows
    // from `aspect` so it is never distorted. Placement is bottom-right of the
    // page's text area, which is where a signature goes on almost every form.
    Q_INVOKABLE bool signPage(int pageIndex,
                              const QVariantList &strokes,
                              qreal aspect,
                              qreal widthPoints);

signals:
    void colorChanged();
    void canAnnotateChanged();

    // A page's rendered content changed and its raster must be re-fetched.
    void pageInvalidated(int pageIndex);

private:
    static MarkupType toMarkupType(Type type);

    QSharedPointer<PdfDocument> m_document;
    SelectionController *m_selection = nullptr;

    // Warm yellow at 40% -- the colour of an actual highlighter, and light
    // enough that the text underneath stays comfortably readable.
    QColor m_color { 255, 214, 64, 102 };
};

} // namespace lumen
