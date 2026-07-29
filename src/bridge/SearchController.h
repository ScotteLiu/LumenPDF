#pragma once

#include "core/PdfTypes.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QSharedPointer>
#include <QTimer>

namespace lumen {

class PdfDocument;

// Full-document text search: both the results model and the controller.
//
// One object rather than two because QML wants exactly this shape -- a list to
// bind to, plus the properties and verbs that drive it.
//
// Results stream in page by page and always in document order, even though
// pages are searched concurrently: QtConcurrent::mapped preserves index order
// and reports each result as it lands, so a match on page 2 can never appear
// after one on page 900.
class SearchController : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentIndexChanged)
    Q_PROPERTY(bool matchCase READ matchCase WRITE setMatchCase NOTIFY optionsChanged)
    Q_PROPERTY(bool wholeWord READ wholeWord WRITE setWholeWord NOTIFY optionsChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)

public:
    enum Status {
        Idle,
        Running,
        Complete,
    };
    Q_ENUM(Status)

    enum Roles {
        PageIndexRole = Qt::UserRole + 1,
        SnippetRole,
        MatchStartRole,
        MatchLengthRole,
    };
    Q_ENUM(Roles)

    explicit SearchController(QObject *parent = nullptr);
    ~SearchController() override;

    void setDocument(const QSharedPointer<PdfDocument> &document);

    QString query() const { return m_query; }
    void setQuery(const QString &query);

    Status status() const { return m_status; }
    int count() const { return m_hits.size(); }
    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);
    int currentPage() const;

    bool matchCase() const { return m_matchCase; }
    void setMatchCase(bool on);
    bool wholeWord() const { return m_wholeWord; }
    void setWholeWord(bool on);

    qreal progress() const { return m_progress; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Highlight geometry for one page, in PDF points with a top-left origin.
    // The page view scales these by the current zoom.
    Q_INVOKABLE QVariantList rectsForPage(int pageIndex) const;

    // Geometry of the currently selected match only, so it can be drawn in a
    // stronger colour than the rest.
    Q_INVOKABLE QVariantList currentRectsForPage(int pageIndex) const;

    Q_INVOKABLE void clear();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();

signals:
    void queryChanged();
    void statusChanged();
    void countChanged();
    void currentIndexChanged();
    void optionsChanged();
    void progressChanged();

    // Emitted when navigation selects a match the view should scroll to.
    void navigateTo(int pageIndex);

private:
    void restart();
    void runSearch();
    void cancelSearch();
    void setStatus(Status status);

    QSharedPointer<PdfDocument> m_document;

    QString m_query;
    bool m_matchCase = false;
    bool m_wholeWord = false;

    Status m_status = Idle;
    QVector<SearchHit> m_hits;
    int m_currentIndex = -1;
    qreal m_progress = 0.0;
    int m_pageTotal = 0;
    QElapsedTimer m_elapsed;

    // Typing "annotation" should run one search, not ten.
    QTimer m_debounce;

    QFutureWatcher<QVector<SearchHit>> *m_watcher = nullptr;
};

} // namespace lumen
