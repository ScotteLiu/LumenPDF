#pragma once

#include "core/PdfTypes.h"

#include <QAbstractListModel>
#include <QSharedPointer>

namespace lumen {

class PdfDocument;

// The document outline as a flat, collapsible list.
//
// The source array is the full depth-first flattening; `m_visible` is an index
// map into it. Expanding or collapsing rebuilds the map and emits the row
// insert/remove -- no tree model, no persistent indices, no proxy.
class OutlineModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool empty READ isEmpty NOTIFY countChanged)

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        PageIndexRole,
        DepthRole,
        HasChildrenRole,
        ExpandedRole,
    };
    Q_ENUM(Roles)

    explicit OutlineModel(QObject *parent = nullptr);

    void setDocument(const QSharedPointer<PdfDocument> &document);

    int count() const { return m_visible.size(); }
    bool isEmpty() const { return m_items.isEmpty(); }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE void expandAll();
    Q_INVOKABLE void collapseAll();

signals:
    void countChanged();

private:
    void rebuildVisible();

    // How many levels are open when a document is first shown. Two is the
    // sweet spot: chapters and their sections, without a wall of subsections.
    static constexpr int kDefaultExpandedDepth = 2;

    QVector<OutlineItem> m_items;
    QVector<bool> m_expanded;   // parallel to m_items
    QVector<int> m_visible;     // rows -> indices into m_items

    QSharedPointer<PdfDocument> m_document;
};

} // namespace lumen
