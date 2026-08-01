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

    // Counted, not a flag. This used to set a bool and then report
    // `last - first + 1` regardless, so a selection spanning two pages where
    // only the first could be redacted told the user both were destroyed --
    // after a dialog promising nothing was recoverable. That is the worst
    // possible thing for this feature to be wrong about.
    //
    // It is reachable: redactRegions rasterises at 300 dpi and renderPage
    // refuses anything over 40 megapixels, so any page much beyond A1 fails
    // outright.
    int succeeded = 0;
    QVector<int> skipped;

    for (int page = first; page <= last; ++page) {
        const QVariantMap range = m_selection->rangeForPage(page);
        const int start = range.value(QStringLiteral("start")).toInt();
        const int count = range.value(QStringLiteral("count")).toInt();
        if (start < 0 || count <= 0)
            continue;   // nothing selected on this page; not a failure

        const QVector<QRectF> rects = m_document->rectsForRange(page, start, count);
        if (rects.isEmpty()) {
            skipped.append(page);
            continue;
        }

        const RedactionResult result = m_document->redactRegions(page, rects);
        if (!result.ok) {
            skipped.append(page);
            continue;
        }

        textRemoved += result.textObjectsRemoved;
        imagesRemoved += result.imageObjectsRemoved;
        ++succeeded;
        emit pageInvalidated(page);
    }

    if (succeeded == 0) {
        emit failed(tr("Nothing could be redacted there."));
        return false;
    }

    // The selection overlay would otherwise sit on top of the black box, and
    // the text it refers to no longer exists.
    m_selection->clear();

    emit redacted(textRemoved, imagesRemoved);
    emit flattenedPages(succeeded);

    // A partial result is reported as a failure even though some pages were
    // destroyed, because the part that matters to the user is the part that
    // was not. Emitted after redacted()/flattenedPages() so the toast that
    // survives is the warning.
    if (!skipped.isEmpty()) {
        QStringList numbers;
        for (int page : skipped)
            numbers.append(QString::number(page + 1));

        emit failed(tr("Page %1 could not be redacted and still contains the "
                       "selected text. Pages larger than about A1 cannot be "
                       "redacted, because flattening them would need more "
                       "memory than is safe to allocate.",
                       "", skipped.size())
                        .arg(numbers.join(QStringLiteral(", "))));
    }

    return true;
}

} // namespace lumen
