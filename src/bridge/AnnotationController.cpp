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

bool AnnotationController::signPage(int pageIndex,
                                    const QVariantList &strokes,
                                    qreal aspect,
                                    qreal widthPoints)
{
    if (!m_document || !m_document->isValid())
        return false;

    // QML hands over a list of lists of points; flatten it into the shape the
    // core expects, dropping anything degenerate.
    QVector<QVector<QPointF>> converted;
    converted.reserve(strokes.size());

    for (const QVariant &strokeVariant : strokes) {
        const QVariantList points = strokeVariant.toList();
        if (points.size() < 2)
            continue;

        QVector<QPointF> stroke;
        stroke.reserve(points.size());
        for (const QVariant &pointVariant : points)
            stroke.append(pointVariant.toPointF());

        converted.append(stroke);
    }

    if (converted.isEmpty())
        return false;

    const auto info = m_document->pageInfo(pageIndex);
    const qreal pageWidth = info.sizePoints.width();
    const qreal pageHeight = info.sizePoints.height();
    if (pageWidth <= 0 || pageHeight <= 0)
        return false;

    // Sized relative to the page so a signature looks the same on A4 and on
    // Letter, and clamped so it cannot swamp a small page.
    const qreal width = qBound(60.0, widthPoints > 0 ? widthPoints : pageWidth * 0.32,
                               pageWidth * 0.8);
    const qreal height = width * qBound(0.12, aspect > 0 ? aspect : 0.35, 1.0);

    // One inch in from the bottom-right corner: clear of the trim, and where a
    // signature line sits on nearly every printed form.
    const qreal margin = 72.0;
    const QRectF target(pageWidth - width - margin,
                        pageHeight - height - margin,
                        width,
                        height);

    // Signature ink is near-black rather than pure black, which reads as ink
    // rather than as printed type.
    const QColor inkColor(24, 28, 48, 255);

    if (!m_document->addInkStrokes(pageIndex, converted, target, inkColor,
                                  qMax(0.8, width / 130.0))) {
        return false;
    }

    emit pageInvalidated(pageIndex);
    qCInfo(lcAnnot) << "signed page" << pageIndex << "with" << converted.size() << "strokes";
    return true;
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
