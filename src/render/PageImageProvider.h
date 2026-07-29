#pragma once

#include "render/PageRenderCache.h"

#include <QQuickAsyncImageProvider>
#include <QSharedPointer>
#include <QThreadPool>

namespace lumen {

class PdfDocument;

// Serves page rasters to QML under the "pdfpage" scheme:
//
//     Image { source: "image://pdfpage/<index>?w=<pixelWidth>" }
//
// Rendering happens on a dedicated pool, never on the GUI or scene-graph
// thread, so a slow page can stall neither input nor the current frame.
// QML's own Image element handles the virtualisation: a ListView only
// instantiates visible delegates, so only visible pages are ever requested.
class PageImageProvider : public QQuickAsyncImageProvider {
public:
    PageImageProvider();
    ~PageImageProvider() override;

    // Called from the GUI thread when a document is opened or closed.
    void setDocument(const QSharedPointer<PdfDocument> &document);

    // Drops every cached raster. Called after an edit, since annotations
    // change what a page looks like at every zoom level at once.
    void clearCache();

    QQuickImageResponse *requestImageResponse(const QString &id,
                                              const QSize &requestedSize) override;

private:
    QSharedPointer<PdfDocument> document() const;

    mutable QMutex m_documentMutex;
    QSharedPointer<PdfDocument> m_document;
    QSharedPointer<PageRenderCache> m_cache;
    QThreadPool m_pool;
};

} // namespace lumen
