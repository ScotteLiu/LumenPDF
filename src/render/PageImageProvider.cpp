#include "render/PageImageProvider.h"

#include "core/PdfDocument.h"

#include <QMutexLocker>
#include <QQuickImageResponse>
#include <QRunnable>
#include <QThread>
#include <QUrlQuery>

namespace lumen {

namespace {

// Guard rails: a runaway zoom must not try to allocate a gigapixel bitmap.
constexpr int kMaxRenderWidth = 8192;
constexpr int kMinRenderWidth = 16;

struct PageRequest {
    int pageIndex = -1;
    int width = 0;
    bool valid = false;
};

// id looks like "12?w=1400" -- page index, then the pixel width to render at.
PageRequest parseRequest(const QString &id)
{
    PageRequest request;

    const int separator = id.indexOf(u'?');
    const QString indexPart = separator < 0 ? id : id.left(separator);

    bool ok = false;
    request.pageIndex = indexPart.toInt(&ok);
    if (!ok || request.pageIndex < 0)
        return request;

    if (separator >= 0) {
        const QUrlQuery query(id.mid(separator + 1));
        request.width = query.queryItemValue(QStringLiteral("w")).toInt();
    }

    request.width = qBound(kMinRenderWidth, request.width, kMaxRenderWidth);
    request.valid = true;
    return request;
}

class PageResponse : public QQuickImageResponse, public QRunnable {
public:
    PageResponse(const PageRequest &request,
                 QSharedPointer<PdfDocument> document,
                 QSharedPointer<PageRenderCache> cache)
        : m_request(request)
        , m_document(std::move(document))
        , m_cache(std::move(cache))
    {
        setAutoDelete(false);
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override { return m_error; }

    void run() override
    {
        if (!m_request.valid || !m_document || !m_document->isValid()) {
            m_error = QStringLiteral("No document loaded");
            emit finished();
            return;
        }

        m_image = m_cache->take(m_request.pageIndex, m_request.width);
        if (!m_image.isNull()) {
            emit finished();
            return;
        }

        const auto info = m_document->pageInfo(m_request.pageIndex);
        if (info.sizePoints.width() <= 0.0) {
            m_error = QStringLiteral("Invalid page geometry");
            emit finished();
            return;
        }

        const qreal aspect = info.sizePoints.height() / info.sizePoints.width();
        const QSize pixelSize(m_request.width,
                              qMax(1, qRound(m_request.width * aspect)));

        m_image = m_document->renderPage(m_request.pageIndex, pixelSize);
        if (m_image.isNull()) {
            m_error = QStringLiteral("Render failed for page %1").arg(m_request.pageIndex);
            emit finished();
            return;
        }

        m_cache->insert(m_request.pageIndex, m_request.width, m_image);
        emit finished();
    }

private:
    PageRequest m_request;
    QSharedPointer<PdfDocument> m_document;
    QSharedPointer<PageRenderCache> m_cache;
    QImage m_image;
    QString m_error;
};

} // namespace

PageImageProvider::PageImageProvider()
    : m_cache(QSharedPointer<PageRenderCache>::create())
{
    // Leave at least one core for the GUI and scene-graph threads.
    m_pool.setMaxThreadCount(qMax(1, QThread::idealThreadCount() - 1));
    m_pool.setExpiryTimeout(30'000);
}

PageImageProvider::~PageImageProvider()
{
    m_pool.waitForDone();
}

void PageImageProvider::setDocument(const QSharedPointer<PdfDocument> &document)
{
    {
        QMutexLocker locker(&m_documentMutex);
        m_document = document;
    }
    m_cache->clear();
}

QSharedPointer<PdfDocument> PageImageProvider::document() const
{
    QMutexLocker locker(&m_documentMutex);
    return m_document;
}

QQuickImageResponse *PageImageProvider::requestImageResponse(const QString &id,
                                                             const QSize &requestedSize)
{
    PageRequest request = parseRequest(id);

    // Fall back to the size QML asked for when the URL carried no explicit width.
    if (request.width == kMinRenderWidth && requestedSize.width() > 0)
        request.width = qBound(kMinRenderWidth, requestedSize.width(), kMaxRenderWidth);

    auto *response = new PageResponse(request, document(), m_cache);

    // The QML engine owns the response and deletes it once it has taken the
    // texture -- hence autoDelete off on the QRunnable side, and no deleteLater
    // here. (QQuickImageProvider is not a QObject, so there is nothing to
    // parent it to either.)
    m_pool.start(response);

    return response;
}

} // namespace lumen
