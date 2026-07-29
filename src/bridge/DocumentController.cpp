#include "bridge/DocumentController.h"

#include "bridge/OutlineModel.h"
#include "bridge/PageListModel.h"
#include "bridge/SearchController.h"
#include "core/PdfDocument.h"
#include "render/PageImageProvider.h"

#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

namespace lumen {

DocumentController::DocumentController(QObject *parent)
    : QObject(parent)
    , m_pageModel(new PageListModel(this))
    , m_outlineModel(new OutlineModel(this))
    , m_search(new SearchController(this))
{
}

DocumentController::~DocumentController() = default;

void DocumentController::setImageProvider(PageImageProvider *provider)
{
    m_provider = provider;
}

int DocumentController::pageCount() const
{
    return (m_document && m_document->isValid()) ? m_document->pageCount() : 0;
}

QAbstractItemModel *DocumentController::pageModel() const
{
    return m_pageModel;
}

QAbstractItemModel *DocumentController::outlineModelAsItemModel() const
{
    return m_outlineModel;
}

double DocumentController::pageWidthPoints(int index) const
{
    if (!m_document || !m_document->isValid() || index < 0 || index >= m_document->pageCount())
        return 0.0;
    return m_document->pageInfo(index).sizePoints.width();
}

void DocumentController::open(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) {
        setStatus(Status::Error, QStringLiteral("Empty path"));
        return;
    }

    setStatus(Status::Loading);

    // Parsing the cross-reference table and page tree happens off the GUI
    // thread; a 2000-page file would otherwise stall the first frame.
    auto *watcher = new QFutureWatcher<QSharedPointer<PdfDocument>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        const auto document = watcher->result();
        watcher->deleteLater();

        if (!document || !document->isValid()) {
            setStatus(Status::Error,
                      document ? document->lastError()
                               : QStringLiteral("Could not open the document."));
            return;
        }

        adoptDocument(document);
    });

    watcher->setFuture(QtConcurrent::run([path]() -> QSharedPointer<PdfDocument> {
        auto document = QSharedPointer<PdfDocument>::create();
        document->load(path);
        return document;
    }));
}

void DocumentController::close()
{
    adoptDocument({});
    setStatus(Status::Empty);
}

void DocumentController::adoptDocument(const QSharedPointer<PdfDocument> &document)
{
    m_document = document;

    // Provider first: the model reset below makes QML request page images
    // immediately, and those requests must land on the new document.
    if (m_provider)
        m_provider->setDocument(m_document);

    m_pageModel->setDocument(m_document);
    m_outlineModel->setDocument(m_document);
    m_search->setDocument(m_document);

    if (m_document && m_document->isValid()) {
        m_filePath = m_document->filePath();
        m_title = QFileInfo(m_filePath).fileName();
        setStatus(Status::Ready);
    } else {
        m_filePath.clear();
        m_title.clear();
    }

    emit documentChanged();
}

void DocumentController::setStatus(Status status, const QString &error)
{
    if (m_status == status && m_errorString == error)
        return;

    m_status = status;
    m_errorString = error;
    emit statusChanged();
}

} // namespace lumen
