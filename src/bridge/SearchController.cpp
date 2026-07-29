#include "bridge/SearchController.h"

#include "core/PdfDocument.h"

#include <QLoggingCategory>
#include <QtConcurrent/QtConcurrentMap>

Q_LOGGING_CATEGORY(lcSearch, "lumen.search")

namespace lumen {

namespace {
// Long enough that a fast typist runs one search, short enough that it still
// feels immediate.
constexpr int kDebounceMs = 180;

// Below this many characters a search matches almost everything and costs a
// full document scan for nothing.
constexpr int kMinQueryLength = 2;
} // namespace

SearchController::SearchController(QObject *parent)
    : QAbstractListModel(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &SearchController::runSearch);
}

SearchController::~SearchController()
{
    cancelSearch();
}

void SearchController::setDocument(const QSharedPointer<PdfDocument> &document)
{
    cancelSearch();
    m_document = document;
    clear();
}

void SearchController::setQuery(const QString &query)
{
    if (m_query == query)
        return;

    m_query = query;
    emit queryChanged();
    restart();
}

void SearchController::setMatchCase(bool on)
{
    if (m_matchCase == on)
        return;
    m_matchCase = on;
    emit optionsChanged();
    restart();
}

void SearchController::setWholeWord(bool on)
{
    if (m_wholeWord == on)
        return;
    m_wholeWord = on;
    emit optionsChanged();
    restart();
}

void SearchController::restart()
{
    cancelSearch();

    if (!m_hits.isEmpty()) {
        beginResetModel();
        m_hits.clear();
        endResetModel();
        emit countChanged();
    }

    setCurrentIndex(-1);
    m_progress = 0.0;
    emit progressChanged();

    if (m_query.size() < kMinQueryLength || !m_document || !m_document->isValid()) {
        setStatus(Idle);
        return;
    }

    m_debounce.start();
}

void SearchController::runSearch()
{
    if (!m_document || !m_document->isValid() || m_query.size() < kMinQueryLength)
        return;

    setStatus(Running);

    QList<int> pages;
    pages.reserve(m_document->pageCount());
    for (int i = 0; i < m_document->pageCount(); ++i)
        pages.append(i);

    // Captured by value: the search must keep the document alive even if the
    // user closes it mid-scan.
    const auto document = m_document;
    const QString query = m_query;
    const bool matchCase = m_matchCase;
    const bool wholeWord = m_wholeWord;

    m_pageTotal = pages.size();
    m_elapsed.start();
    m_watcher = new QFutureWatcher<QVector<SearchHit>>(this);

    connect(m_watcher, &QFutureWatcherBase::resultReadyAt, this, [this](int index) {
        if (!m_watcher)
            return;

        const QVector<SearchHit> pageHits = m_watcher->resultAt(index);

        if (m_pageTotal > 0) {
            m_progress = qBound(0.0, qreal(index + 1) / m_pageTotal, 1.0);
            emit progressChanged();
        }

        if (pageHits.isEmpty())
            return;

        beginInsertRows({}, m_hits.size(), m_hits.size() + pageHits.size() - 1);
        m_hits.append(pageHits);
        endInsertRows();
        emit countChanged();

        // Select the first match as soon as one exists, so Enter works before
        // the scan has finished.
        if (m_currentIndex < 0)
            setCurrentIndex(0);
    });

    connect(m_watcher, &QFutureWatcherBase::finished, this, [this] {
        m_progress = 1.0;
        emit progressChanged();
        setStatus(Complete);

        qCInfo(lcSearch) << "query" << m_query
                         << "->" << m_hits.size() << "hits across"
                         << m_pageTotal << "pages in" << m_elapsed.elapsed() << "ms";

        if (m_watcher) {
            m_watcher->deleteLater();
            m_watcher = nullptr;
        }
    });

    m_watcher->setFuture(QtConcurrent::mapped(
        pages,
        [document, query, matchCase, wholeWord](int page) {
            return document->searchPage(page, query, matchCase, wholeWord);
        }));
}

void SearchController::cancelSearch()
{
    m_debounce.stop();

    if (!m_watcher)
        return;

    // Detach the signals first: cancel() can fire finished() synchronously,
    // and the handler would then delete the watcher out from under us.
    auto *watcher = m_watcher;
    m_watcher = nullptr;
    watcher->disconnect(this);
    watcher->cancel();
    watcher->waitForFinished();
    watcher->deleteLater();
}

void SearchController::clear()
{
    cancelSearch();

    if (!m_query.isEmpty()) {
        m_query.clear();
        emit queryChanged();
    }

    if (!m_hits.isEmpty()) {
        beginResetModel();
        m_hits.clear();
        endResetModel();
        emit countChanged();
    }

    setCurrentIndex(-1);
    m_progress = 0.0;
    emit progressChanged();
    setStatus(Idle);
}

void SearchController::setCurrentIndex(int index)
{
    const int clamped = (index >= 0 && index < m_hits.size()) ? index : -1;
    if (m_currentIndex == clamped)
        return;

    m_currentIndex = clamped;
    emit currentIndexChanged();

    if (m_currentIndex >= 0)
        emit navigateTo(m_hits.at(m_currentIndex).pageIndex);
}

int SearchController::currentPage() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_hits.size())
        return -1;
    return m_hits.at(m_currentIndex).pageIndex;
}

void SearchController::next()
{
    if (m_hits.isEmpty())
        return;
    // Wraps, like every find bar the user has ever used.
    setCurrentIndex((m_currentIndex + 1) % m_hits.size());
}

void SearchController::previous()
{
    if (m_hits.isEmpty())
        return;
    setCurrentIndex((m_currentIndex - 1 + m_hits.size()) % m_hits.size());
}

int SearchController::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_hits.size();
}

QVariant SearchController::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_hits.size())
        return {};

    const SearchHit &hit = m_hits.at(index.row());

    switch (role) {
    case PageIndexRole:
        return hit.pageIndex;
    case SnippetRole:
        return hit.snippet;
    case MatchStartRole:
        return hit.snippetMatchStart;
    case MatchLengthRole:
        return hit.snippetMatchLength;
    default:
        return {};
    }
}

QHash<int, QByteArray> SearchController::roleNames() const
{
    return {
        { PageIndexRole, "pageIndex" },
        { SnippetRole, "snippet" },
        { MatchStartRole, "matchStart" },
        { MatchLengthRole, "matchLength" },
    };
}

QVariantList SearchController::rectsForPage(int pageIndex) const
{
    QVariantList out;
    for (const SearchHit &hit : m_hits) {
        if (hit.pageIndex != pageIndex)
            continue;
        for (const QRectF &r : hit.rects)
            out.append(QVariant::fromValue(r));
    }
    return out;
}

QVariantList SearchController::currentRectsForPage(int pageIndex) const
{
    QVariantList out;
    if (m_currentIndex < 0 || m_currentIndex >= m_hits.size())
        return out;

    const SearchHit &hit = m_hits.at(m_currentIndex);
    if (hit.pageIndex != pageIndex)
        return out;

    for (const QRectF &r : hit.rects)
        out.append(QVariant::fromValue(r));
    return out;
}

void SearchController::setStatus(Status status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

} // namespace lumen
