#include "bridge/SelectionController.h"

#include "core/PdfDocument.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QRectF>

namespace lumen {

SelectionController::SelectionController(QObject *parent)
    : QObject(parent)
{
}

void SelectionController::setDocument(const QSharedPointer<PdfDocument> &document)
{
    m_document = document;
    clear();
}

void SelectionController::ordered(Anchor &from, Anchor &to) const
{
    from = m_anchor;
    to = m_focus;

    const bool reversed = (to.page < from.page)
        || (to.page == from.page && to.character < from.character);

    if (reversed)
        std::swap(from, to);
}

bool SelectionController::isEmpty() const
{
    return !m_hasSelection || !m_anchor.isValid() || !m_focus.isValid();
}

int SelectionController::firstPage() const
{
    if (isEmpty())
        return -1;
    Anchor from, to;
    ordered(from, to);
    return from.page;
}

int SelectionController::lastPage() const
{
    if (isEmpty())
        return -1;
    Anchor from, to;
    ordered(from, to);
    return to.page;
}

QVariantMap SelectionController::rangeForPage(int pageIndex) const
{
    QVariantMap result;
    result["start"] = -1;
    result["count"] = 0;

    if (!m_document || isEmpty())
        return result;

    Anchor from, to;
    ordered(from, to);

    if (pageIndex < from.page || pageIndex > to.page)
        return result;

    const int start = (pageIndex == from.page) ? from.character : 0;
    const int endExclusive = (pageIndex == to.page)
        ? to.character + 1
        : m_document->characterCount(pageIndex);

    const int count = endExclusive - start;
    if (count <= 0)
        return result;

    result["start"] = start;
    result["count"] = count;
    return result;
}

QString SelectionController::text() const
{
    if (!m_document || isEmpty())
        return {};

    Anchor from, to;
    ordered(from, to);

    QStringList parts;
    for (int page = from.page; page <= to.page; ++page) {
        const QVariantMap range = rangeForPage(page);
        const int start = range.value("start").toInt();
        const int count = range.value("count").toInt();
        if (start < 0 || count <= 0)
            continue;
        parts.append(m_document->textForRange(page, start, count));
    }

    // A page break is a paragraph break, not a space.
    return parts.join(QStringLiteral("\n\n"));
}

void SelectionController::begin(int pageIndex, const QPointF &point)
{
    if (!m_document || !m_document->isValid())
        return;

    const int character = m_document->insertionPointAt(pageIndex, point);
    if (character < 0) {
        clear();
        return;
    }

    m_anchor = Anchor { pageIndex, character };
    m_focus = m_anchor;
    m_dragging = true;
    m_granularity = Granularity::Character;

    // A press alone selects nothing; the drag has to move first. Otherwise
    // every click would leave one character highlighted.
    m_hasSelection = false;

    emit changed();
}

void SelectionController::extend(int pageIndex, const QPointF &point)
{
    if (!m_document || !m_anchor.isValid())
        return;

    const int character = m_document->insertionPointAt(pageIndex, point);
    if (character < 0)
        return;

    Anchor next { pageIndex, character };

    // Word- and line-granularity drags keep snapping, so that dragging after a
    // double-click extends whole words rather than characters.
    if (m_granularity != Granularity::Character) {
        int start = character;
        int count = 1;
        if (m_granularity == Granularity::Word)
            m_document->expandToWord(pageIndex, start, count);
        else
            m_document->expandToLine(pageIndex, start, count);

        const bool forward = (pageIndex > m_anchor.page)
            || (pageIndex == m_anchor.page && character >= m_anchor.character);
        next.character = forward ? (start + count - 1) : start;
    }

    if (next.page == m_focus.page && next.character == m_focus.character)
        return;

    m_focus = next;
    m_hasSelection = true;
    emit changed();
}

void SelectionController::end()
{
    if (!m_dragging)
        return;
    m_dragging = false;
    emit changed();
}

void SelectionController::selectWordAt(int pageIndex, const QPointF &point)
{
    if (!m_document || !m_document->isValid())
        return;

    const int character = m_document->insertionPointAt(pageIndex, point);
    if (character < 0)
        return;

    int start = character;
    int count = 1;
    m_document->expandToWord(pageIndex, start, count);

    m_anchor = Anchor { pageIndex, start };
    m_focus = Anchor { pageIndex, start + count - 1 };
    m_granularity = Granularity::Word;
    m_dragging = true;
    m_hasSelection = true;

    emit changed();
}

void SelectionController::selectLineAt(int pageIndex, const QPointF &point)
{
    if (!m_document || !m_document->isValid())
        return;

    const int character = m_document->insertionPointAt(pageIndex, point);
    if (character < 0)
        return;

    int start = character;
    int count = 1;
    m_document->expandToLine(pageIndex, start, count);

    m_anchor = Anchor { pageIndex, start };
    m_focus = Anchor { pageIndex, start + count - 1 };
    m_granularity = Granularity::Line;
    m_dragging = true;
    m_hasSelection = true;

    emit changed();
}

void SelectionController::selectAll()
{
    if (!m_document || !m_document->isValid() || m_document->pageCount() == 0)
        return;

    const int lastPage = m_document->pageCount() - 1;
    const int lastChar = qMax(0, m_document->characterCount(lastPage) - 1);

    m_anchor = Anchor { 0, 0 };
    m_focus = Anchor { lastPage, lastChar };
    m_granularity = Granularity::Character;
    m_dragging = false;
    m_hasSelection = true;

    emit changed();
}

void SelectionController::clear()
{
    if (!m_anchor.isValid() && !m_focus.isValid() && !m_dragging && !m_hasSelection)
        return;

    m_anchor = {};
    m_focus = {};
    m_dragging = false;
    m_hasSelection = false;
    m_granularity = Granularity::Character;

    emit changed();
}

QVariantList SelectionController::rectsForPage(int pageIndex) const
{
    QVariantList out;
    if (!m_document || isEmpty())
        return out;

    const QVariantMap range = rangeForPage(pageIndex);
    const int start = range.value("start").toInt();
    const int count = range.value("count").toInt();
    if (start < 0 || count <= 0)
        return out;

    const QVector<QRectF> rects = m_document->rectsForRange(pageIndex, start, count);
    out.reserve(rects.size());
    for (const QRectF &r : rects)
        out.append(QVariant::fromValue(r));

    return out;
}

void SelectionController::copyToClipboard()
{
    const QString selected = text();
    if (selected.isEmpty())
        return;

    QGuiApplication::clipboard()->setText(selected);
    emit copied(selected.size());
}

} // namespace lumen
