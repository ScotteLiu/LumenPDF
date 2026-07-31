#include "bridge/DocumentController.h"

#include "bridge/OutlineModel.h"
#include "bridge/PageListModel.h"
#include "bridge/SearchController.h"
#include "core/PdfDocument.h"
#include "render/PageImageProvider.h"

#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

Q_LOGGING_CATEGORY(lcDocument, "lumen.document.bridge")

namespace lumen {

DocumentController::DocumentController(QObject *parent)
    : QObject(parent)
    , m_pageModel(new PageListModel(this))
    , m_outlineModel(new OutlineModel(this))
    , m_search(new SearchController(this))
    , m_selection(new SelectionController(this))
    , m_annotate(new AnnotationController(m_selection, this))
    , m_pageOps(new PageOperations(this))
    , m_exporter(new ExportController(this))
    , m_redact(new RedactionController(m_selection, this))
    , m_forms(new FormController(this))
    , m_ocr(new OcrController(this))
    , m_print(new PrintController(this))
    , m_network(new QNetworkAccessManager(this))
    , m_google(new GoogleAuth(m_network, this))
    , m_drive(new GoogleDrive(m_google, m_network, this))
{
    // A document downloaded from Drive is opened from the local cache: editing
    // over the network would turn every save into a network failure waiting to
    // happen. The local copy is authoritative until the user uploads.
    connect(m_drive, &GoogleDrive::downloaded, this,
            [this](const QString &localPath, const QString &) {
                open(QUrl::fromLocalFile(localPath));
            });

    // An OCR text layer changes what the page renders to, exactly like an
    // annotation, so it reuses the same invalidation path.
    connect(m_ocr, &OcrController::pageInvalidated,
            m_annotate, &AnnotationController::pageInvalidated);

    // Redaction and form editing both change rendered content exactly like an
    // annotation does, so they reuse the same invalidation path.
    connect(m_redact, &RedactionController::pageInvalidated,
            m_annotate, &AnnotationController::pageInvalidated);
    connect(m_forms, &FormController::pageInvalidated,
            m_annotate, &AnnotationController::pageInvalidated);

    // Reordering, rotating or deleting a page changes both the page list and
    // every rendered raster, so the models are reset and the whole cache is
    // dropped. Selection and search results refer to page indices that may no
    // longer mean the same thing, so they are cleared rather than migrated --
    // silently pointing at the wrong page would be worse than losing them.
    connect(m_pageOps, &PageOperations::structureChanged, this, [this] {
        if (m_provider)
            m_provider->clearCache();

        m_selection->clear();
        m_search->clear();
        m_pageModel->setDocument(m_document);
        m_outlineModel->setDocument(m_document);

        ++m_renderGeneration;
        emit renderGenerationChanged();
        emit documentChanged();
        emit modifiedChanged();
    });

    connect(m_annotate, &AnnotationController::pageInvalidated,
            this, [this](int) {
                // Page rasters are cached per (page, width); an edit makes all
                // of them stale, and edits are rare enough that dropping the
                // whole cache is simpler and safer than surgical eviction.
                if (m_provider)
                    m_provider->clearCache();

                ++m_renderGeneration;
                emit renderGenerationChanged();
                emit modifiedChanged();
            });
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

int DocumentController::pageTextLength(int index) const
{
    if (!m_document || !m_document->isValid() || index < 0 || index >= m_document->pageCount())
        return 0;
    return m_document->pageText(index).size();
}

QVariantMap DocumentController::textRunAt(int pageIndex, const QPointF &point) const
{
    QVariantMap result;
    result[QStringLiteral("valid")] = false;

    if (!m_document || !m_document->isValid())
        return result;

    const TextObjectInfo info = m_document->textObjectAt(pageIndex, point);
    if (!info.valid)
        return result;

    result[QStringLiteral("valid")] = true;
    result[QStringLiteral("objectIndex")] = info.objectIndex;
    result[QStringLiteral("text")] = info.text;
    result[QStringLiteral("x")] = info.bounds.x();
    result[QStringLiteral("y")] = info.bounds.y();
    result[QStringLiteral("width")] = info.bounds.width();
    result[QStringLiteral("height")] = info.bounds.height();
    result[QStringLiteral("fontSize")] = info.fontSize;
    result[QStringLiteral("longRun")] = info.spansMuchText;
    return result;
}

void DocumentController::open(const QUrl &url)
{
    openWithPassword(url, QString());
}

void DocumentController::unlock(const QString &password)
{
    if (m_pendingUrl.isEmpty()) {
        return;
    }
    openWithPassword(m_pendingUrl, password);
}

void DocumentController::cancelUnlock()
{
    m_pendingUrl.clear();
    setStatus(Status::Empty);
}

void DocumentController::openWithPassword(const QUrl &url, const QString &password)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) {
        setStatus(Status::Error, QStringLiteral("Empty path"));
        return;
    }

    const bool retry = !password.isEmpty();
    setStatus(Status::Loading);

    // Parsing the cross-reference table and page tree happens off the GUI
    // thread; a 2000-page file would otherwise stall the first frame.
    auto *watcher = new QFutureWatcher<QSharedPointer<PdfDocument>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, url, retry] {
        const auto document = watcher->result();
        watcher->deleteLater();

        if (!document || !document->isValid()) {
            // A missing or wrong password is a question, not a failure. The
            // URL is kept so unlock() can retry it without QML having to hold
            // the path across a dialog.
            if (document && document->needsPassword()) {
                m_pendingUrl = url;
                setStatus(Status::Locked, document->lastError());
                emit passwordRequired(retry);
                return;
            }

            m_pendingUrl.clear();
            setStatus(Status::Error,
                      document ? document->lastError()
                               : QStringLiteral("Could not open the document."));
            return;
        }

        m_pendingUrl.clear();
        adoptDocument(document);
    });

    watcher->setFuture(QtConcurrent::run([path, password]() -> QSharedPointer<PdfDocument> {
        auto document = QSharedPointer<PdfDocument>::create();
        document->load(path, password);
        return document;
    }));
}

QVariantMap DocumentController::linkAt(int pageIndex, const QPointF &point) const
{
    QVariantMap map;
    map[QStringLiteral("kind")] = QStringLiteral("none");
    if (!m_document)
        return map;

    const LinkTarget target = m_document->linkAt(pageIndex, point);
    switch (target.kind) {
    case LinkTarget::Kind::Page:
        map[QStringLiteral("kind")] = QStringLiteral("page");
        map[QStringLiteral("pageIndex")] = target.pageIndex;
        break;
    case LinkTarget::Kind::Uri:
        map[QStringLiteral("kind")] = QStringLiteral("uri");
        map[QStringLiteral("uri")] = target.uri;
        break;
    case LinkTarget::Kind::None:
        return map;
    }

    map[QStringLiteral("x")] = target.rect.x();
    map[QStringLiteral("y")] = target.rect.y();
    map[QStringLiteral("width")] = target.rect.width();
    map[QStringLiteral("height")] = target.rect.height();
    return map;
}

int DocumentController::linkCount(int pageIndex) const
{
    return m_document ? m_document->links(pageIndex).size() : 0;
}

bool DocumentController::openExternalLink(const QString &uri)
{
    const QUrl url(uri);

    // A PDF is untrusted input and its links can name any scheme. Only the two
    // that a document has a legitimate reason to use are ever handed to the
    // shell -- "file:" would open anything on disk and custom schemes can hand
    // arguments to a registered program.
    static const QStringList allowed { QStringLiteral("http"),
                                       QStringLiteral("https"),
                                       QStringLiteral("mailto") };

    if (!url.isValid() || !allowed.contains(url.scheme().toLower())) {
        qCWarning(lcDocument) << "refused a link with scheme" << url.scheme();
        return false;
    }
    return QDesktopServices::openUrl(url);
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
    m_selection->setDocument(m_document);
    m_annotate->setDocument(m_document);
    m_pageOps->setDocument(m_document);
    m_exporter->setDocument(m_document);
    m_redact->setDocument(m_document);
    m_forms->setDocument(m_document);
    m_ocr->setDocument(m_document);
    m_print->setDocument(m_document);

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

bool DocumentController::isModified() const
{
    return m_document && m_document->isValid() && m_document->isModified();
}

bool DocumentController::saveAs(const QUrl &url)
{
    if (!m_document || !m_document->isValid()) {
        emit saveFailed(QStringLiteral("No document is open."));
        return false;
    }

    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) {
        emit saveFailed(QStringLiteral("No destination given."));
        return false;
    }
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
        path += QStringLiteral(".pdf");

    const bool overwritingOpenFile = QFileInfo(path) == QFileInfo(m_document->filePath());

    // Write beside the destination, not into the system temp directory: a
    // cross-volume rename is a copy, and this way the atomic swap stays atomic
    // even for a multi-hundred-megabyte file.
    const QString scratch = path + QStringLiteral(".lumen-tmp");

    if (!m_document->saveAs(scratch)) {
        QFile::remove(scratch);
        emit saveFailed(m_document->lastError().isEmpty()
                        ? QStringLiteral("Could not write the document.")
                        : m_document->lastError());
        return false;
    }

    if (overwritingOpenFile) {
        // PDFium still has the original open and reads from it lazily, so it
        // cannot be replaced underneath itself. Close, swap, reopen at the
        // same page -- and keep the original until the swap has succeeded.
        const QString backup = path + QStringLiteral(".lumen-bak");
        QFile::remove(backup);

        m_provider ? m_provider->setDocument({}) : void();
        m_document->close();

        if (!QFile::rename(path, backup)) {
            QFile::remove(scratch);
            m_document->load(path);
            adoptDocument(m_document);
            emit saveFailed(QStringLiteral("Could not replace the original file."));
            return false;
        }

        if (!QFile::rename(scratch, path)) {
            QFile::rename(backup, path);   // put it back
            QFile::remove(scratch);
            m_document->load(path);
            adoptDocument(m_document);
            emit saveFailed(QStringLiteral("Could not move the new file into place."));
            return false;
        }

        QFile::remove(backup);

        auto reopened = QSharedPointer<PdfDocument>::create();
        reopened->load(path);
        adoptDocument(reopened);
    } else {
        QFile::remove(path);
        if (!QFile::rename(scratch, path)) {
            QFile::remove(scratch);
            emit saveFailed(QStringLiteral("Could not move the new file into place."));
            return false;
        }
    }

    emit modifiedChanged();
    emit saved(path);
    return true;
}

bool DocumentController::compressTo(const QUrl &url, int targetDpi)
{
    if (!m_document || !m_document->isValid()) {
        emit saveFailed(QStringLiteral("No document is open."));
        return false;
    }

    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) {
        emit saveFailed(QStringLiteral("No destination given."));
        return false;
    }
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
        path += QStringLiteral(".pdf");

    const QString sourcePath = m_document->filePath();
    if (QFileInfo(path) == QFileInfo(sourcePath)) {
        // Downsampling is lossy and irreversible. Overwriting the source would
        // mean the original is gone the moment the user tries this once.
        emit saveFailed(QStringLiteral("Choose a different file — compression is lossy."));
        return false;
    }

    const qint64 originalBytes = QFileInfo(sourcePath).size();

    const auto report = m_document->downsampleImages(targetDpi);
    if (!report.ok) {
        emit saveFailed(QStringLiteral("Could not process the document's images."));
        return false;
    }

    // A compact save is what turns the smaller images into a smaller file; an
    // incremental one would append them and leave the originals behind.
    if (!m_document->saveAs(path, true)) {
        emit saveFailed(m_document->lastError().isEmpty()
                        ? QStringLiteral("Could not write the compressed copy.")
                        : m_document->lastError());
        return false;
    }

    // The in-memory document now holds downsampled images, so the open document
    // is genuinely modified even though the user saved elsewhere. Refresh
    // everything rather than pretend otherwise.
    if (m_provider)
        m_provider->clearCache();
    ++m_renderGeneration;
    emit renderGenerationChanged();
    emit modifiedChanged();

    emit compressed(path, originalBytes, QFileInfo(path).size(),
                    report.imagesDownsampled);
    return true;
}

bool DocumentController::save()
{
    if (!m_document || !m_document->isValid())
        return false;
    return saveAs(QUrl::fromLocalFile(m_document->filePath()));
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
