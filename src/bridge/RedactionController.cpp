#include "bridge/RedactionController.h"

#include "bridge/SelectionController.h"
#include "core/PdfDocument.h"

#include <QRectF>

namespace lumen {

RedactionController::RedactionController(SelectionController *selection, QObject *parent)
    : QObject(parent)
    , m_selection(selection)
{
    if (m_selection) {
        connect(m_selection, &SelectionController::changed,
                this, &RedactionController::canRedactChanged);
    }
}

void RedactionController::setDocument(const QSharedPointer<PdfDocument> &document)
{
    m_document = document;
    emit canRedactChanged();
}

bool RedactionController::canRedact() const
{
    return m_document && m_document->isValid()
        && m_selection && !m_selection->isEmpty();
}

bool RedactionController::redactSelection()
{
    if (!canRedact()) {
        emit failed(tr("Select the text to redact first."));
        return false;
    }

    const int first = m_selection->firstPage();
    const int last = m_selection->lastPage();
    if (first < 0 || last < first)
        return false;

    int textRemoved = 0;
    int imagesRemoved = 0;
    bool any = false;

    for (int page = first; page <= last; ++page) {
        const QVariantMap range = m_selection->rangeForPage(page);
        const int start = range.value(QStringLiteral("start")).toInt();
        const int count = range.value(QStringLiteral("count")).toInt();
        if (start < 0 || count <= 0)
            continue;

        const QVector<QRectF> rects = m_document->rectsForRange(page, start, count);
        if (rects.isEmpty())
            continue;

        const RedactionResult result = m_document->redactRegions(page, rects);
        if (!result.ok)
            continue;

        textRemoved += result.textObjectsRemoved;
        imagesRemoved += result.imageObjectsRemoved;
        any = true;
        emit pageInvalidated(page);
    }

    if (!any) {
        emit failed(tr("Nothing could be redacted there."));
        return false;
    }

    // The selection overlay would otherwise sit on top of the black box, and
    // the text it refers to no longer exists.
    m_selection->clear();

    emit redacted(textRemoved, imagesRemoved);
    emit flattenedPages(last - first + 1);
    return true;
}

} // namespace lumen
