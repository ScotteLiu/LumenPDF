#include "bridge/OutlineModel.h"

#include "core/PdfDocument.h"

namespace lumen {

namespace {

// Which rows are visible, given which items are expanded.
//
// Single forward pass: `ancestorOpen[d]` remembers whether the most recent
// item at depth d was expanded, which is exactly the ancestor of anything
// deeper that follows it. O(items x depth) rather than the O(items^2) a
// backwards ancestor search would cost -- outlines run to thousands of rows.
QVector<int> computeVisible(const QVector<OutlineItem> &items,
                            const QVector<bool> &expanded)
{
    QVector<int> visible;
    visible.reserve(items.size());

    QVector<bool> ancestorOpen;

    for (int i = 0; i < items.size(); ++i) {
        const int depth = qMax(0, items.at(i).depth);
        if (ancestorOpen.size() <= depth)
            ancestorOpen.resize(depth + 1, true);

        bool isVisible = true;
        for (int d = 0; d < depth; ++d) {
            if (!ancestorOpen.at(d)) {
                isVisible = false;
                break;
            }
        }

        if (isVisible)
            visible.append(i);

        ancestorOpen[depth] = expanded.value(i, false);
    }

    return visible;
}

} // namespace

OutlineModel::OutlineModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void OutlineModel::setDocument(const QSharedPointer<PdfDocument> &document)
{
    beginResetModel();

    m_document = document;
    m_items = (m_document && m_document->isValid()) ? m_document->outline()
                                                    : QVector<OutlineItem>();

    m_expanded.resize(m_items.size());
    for (int i = 0; i < m_items.size(); ++i)
        m_expanded[i] = m_items.at(i).depth < kDefaultExpandedDepth - 1;

    // Computed inline rather than via rebuildVisible(): that helper emits its
    // own model signals, which would be wrong inside a reset.
    m_visible = computeVisible(m_items, m_expanded);

    endResetModel();
    emit countChanged();
}

void OutlineModel::rebuildVisible()
{
    const QVector<int> next = computeVisible(m_items, m_expanded);

    if (next == m_visible)
        return;

    beginResetModel();
    m_visible = next;
    endResetModel();
    emit countChanged();
}

int OutlineModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visible.size();
}

QVariant OutlineModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size())
        return {};

    const int source = m_visible.at(index.row());
    const OutlineItem &item = m_items.at(source);

    switch (role) {
    case TitleRole:
        return item.title;
    case PageIndexRole:
        return item.pageIndex;
    case DepthRole:
        return item.depth;
    case HasChildrenRole:
        return item.hasChildren;
    case ExpandedRole:
        return m_expanded.at(source);
    default:
        return {};
    }
}

QHash<int, QByteArray> OutlineModel::roleNames() const
{
    return {
        { TitleRole, "title" },
        { PageIndexRole, "pageIndex" },
        { DepthRole, "depth" },
        { HasChildrenRole, "hasChildren" },
        { ExpandedRole, "expanded" },
    };
}

void OutlineModel::toggle(int row)
{
    if (row < 0 || row >= m_visible.size())
        return;

    const int source = m_visible.at(row);
    if (!m_items.at(source).hasChildren)
        return;

    m_expanded[source] = !m_expanded.at(source);
    rebuildVisible();
}

void OutlineModel::expandAll()
{
    m_expanded.fill(true);
    rebuildVisible();
}

void OutlineModel::collapseAll()
{
    m_expanded.fill(false);
    rebuildVisible();
}

} // namespace lumen
