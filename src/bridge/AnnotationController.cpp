#include "bridge/AnnotationController.h"

#include "bridge/SelectionController.h"
#include "core/PdfDocument.h"

#include <QLoggingCategory>
#include <QRectF>

Q_LOGGING_CATEGORY(lcAnnot, "lumen.annotation")

namespace lumen {

AnnotationController::AnnotationController(SelectionController *selection, QObject *parent)
    : QObject(parent)
    , m_selection(selection)
{
    if (m_selection) {
        connect(m_selection, &SelectionController::changed,
                this, &AnnotationController::canAnnotateChanged);
    }
}

void AnnotationController::setDocument(const QSharedPointer<PdfDocument> &document)
{
    m_document = document;
    emit canAnnotateChanged();
}

void AnnotationController::setColor(const QColor &color)
{
    if (m_color == color)
        return;
    m_color = color;
    emit colorChanged();
}

bool AnnotationController::canAnnotate() const
{
    return m_document && m_document->isValid()
        && m_selection && !m_selection->isEmpty();
}

MarkupType AnnotationController::toMarkupType(Type type)
{
    switch (type) {
    case Underline: return MarkupType::Underline;
    case StrikeOut: return MarkupType::StrikeOut;
    case Squiggly:  return MarkupType::Squiggly;
    case Highlight:
    default:        return MarkupType::Highlight;
    }
}

bool AnnotationController::applyToSelection(Type type)
{
    if (!canAnnotate())
        return false;

    const int first = m_selection->firstPage();
    const int last = m_selection->lastPage();
    if (first < 0 || last < first)
        return false;

    // Underlines and strikeouts read better fully opaque; a highlight has to
    // be translucent or it buries the text.
    QColor color = m_color;
    if (type != Highlight)
        color.setAlpha(255);

    bool anyApplied = false;

    for (int page = first; page <= last; ++page) {
        const QVariantMap range = m_selection->rangeForPage(page);
        const int start = range.value(QStringLiteral("start")).toInt();
        const int count = range.value(QStringLiteral("count")).toInt();
        if (start < 0 || count <= 0)
            continue;

        const QVector<QRectF> rects = m_document->rectsForRange(page, start, count);
        if (rects.isEmpty())
            continue;

        if (m_document->addTextMarkup(page, toMarkupType(type), rects, color)) {
            anyApplied = true;
            emit pageInvalidated(page);
        }
    }

    if (anyApplied) {
        qCInfo(lcAnnot) << "applied" << int(type) << "to pages" << first << ".." << last;
        // The selection overlay sits on top of the new annotation and would
        // hide exactly the thing the user just made.
        m_selection->clear();
    }

    return anyApplied;
}

bool AnnotationController::removeAt(int pageIndex, const QPointF &point)
{
    if (!m_document || !m_document->isValid())
        return false;

    const int index = m_document->annotationAt(pageIndex, point);
    if (index < 0)
        return false;

    if (!m_document->removeAnnotation(pageIndex, index))
        return false;

    emit pageInvalidated(pageIndex);
    return true;
}

} // namespace lumen
