#pragma once

#include <QObject>
#include <QPointF>
#include <QSharedPointer>
#include <QVariantList>

namespace lumen {

class PdfDocument;

// Text selection across pages.
//
// A selection is two (page, character) anchors. Everything else -- which
// direction it runs, which pages it touches, what rectangles to draw -- is
// derived, so there is exactly one piece of state to keep consistent.
//
// Selection spanning pages works because page text indices are per-page: the
// range on the first page runs to its end, the last page runs from its start,
// and every page between is selected whole.
class SelectionController : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool active READ isActive NOTIFY changed)
    Q_PROPERTY(bool empty READ isEmpty NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(int firstPage READ firstPage NOTIFY changed)
    Q_PROPERTY(int lastPage READ lastPage NOTIFY changed)

public:
    explicit SelectionController(QObject *parent = nullptr);

    void setDocument(const QSharedPointer<PdfDocument> &document);

    bool isActive() const { return m_dragging; }
    bool isEmpty() const;
    QString text() const;
    int firstPage() const;
    int lastPage() const;

    // -- Pointer gestures --------------------------------------------------
    // `point` is in PDF points with a top-left origin, relative to the page.

    Q_INVOKABLE void begin(int pageIndex, const QPointF &point);
    Q_INVOKABLE void extend(int pageIndex, const QPointF &point);
    Q_INVOKABLE void end();

    Q_INVOKABLE void selectWordAt(int pageIndex, const QPointF &point);
    Q_INVOKABLE void selectLineAt(int pageIndex, const QPointF &point);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clear();

    // Highlight geometry for one page, in PDF points, top-left origin.
    Q_INVOKABLE QVariantList rectsForPage(int pageIndex) const;

    Q_INVOKABLE void copyToClipboard();

    // The character range selected on one page, as {start, count}. Used by
    // annotation creation, which needs the range rather than the pixels.
    Q_INVOKABLE QVariantMap rangeForPage(int pageIndex) const;

signals:
    void changed();
    void copied(int characters);

private:
    struct Anchor {
        int page = -1;
        int character = -1;
        bool isValid() const { return page >= 0 && character >= 0; }
    };

    // Anchors in document order, regardless of drag direction.
    void ordered(Anchor &from, Anchor &to) const;

    QSharedPointer<PdfDocument> m_document;

    Anchor m_anchor;
    Anchor m_focus;
    bool m_dragging = false;

    // Set by word/line selection so that dragging afterwards keeps snapping
    // to the same granularity, the way every text view behaves.
    enum class Granularity { Character, Word, Line };
    Granularity m_granularity = Granularity::Character;
};

} // namespace lumen
