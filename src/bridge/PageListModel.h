#pragma once

#include <QAbstractListModel>
#include <QSharedPointer>

namespace lumen {

class PdfDocument;

// Feeds page geometry to the QML view. Deliberately holds no pixels: the view
// asks the image provider for those, so scrolling never blocks on this model.
class PageListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        PageIndexRole = Qt::UserRole + 1,
        WidthPointsRole,
        HeightPointsRole,
        AspectRatioRole, // height / width
    };
    Q_ENUM(Roles)

    explicit PageListModel(QObject *parent = nullptr);

    void setDocument(const QSharedPointer<PdfDocument> &document);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    QSharedPointer<PdfDocument> m_document;
    int m_count = 0;
};

} // namespace lumen
