#include "bridge/PageListModel.h"

#include "core/PdfDocument.h"

namespace lumen {

PageListModel::PageListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void PageListModel::setDocument(const QSharedPointer<PdfDocument> &document)
{
    beginResetModel();
    m_document = document;
    m_count = (m_document && m_document->isValid()) ? m_document->pageCount() : 0;
    endResetModel();
}

int PageListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_count;
}

QVariant PageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_count || !m_document)
        return {};

    const auto info = m_document->pageInfo(index.row());

    switch (role) {
    case PageIndexRole:
        return index.row();
    case WidthPointsRole:
        return info.sizePoints.width();
    case HeightPointsRole:
        return info.sizePoints.height();
    case AspectRatioRole:
        return info.sizePoints.width() > 0.0
            ? info.sizePoints.height() / info.sizePoints.width()
            : 1.294; // US Letter
    default:
        return {};
    }
}

QHash<int, QByteArray> PageListModel::roleNames() const
{
    return {
        { PageIndexRole, "pageIndex" },
        { WidthPointsRole, "widthPoints" },
        { HeightPointsRole, "heightPoints" },
        { AspectRatioRole, "aspectRatio" },
    };
}

} // namespace lumen
