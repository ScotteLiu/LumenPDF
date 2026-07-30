#pragma once

#include "core/PdfTypes.h"

#include <QColor>
#include <QImage>
#include <QSharedPointer>
#include <QMutex>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector>

namespace lumen {

// A loaded PDF file.
//
// Threading contract: construction and destruction happen on the owning
// thread; pageSize() and renderPage() are safe to call from any thread. PDFium
// serialises access per document, so a mutex guards every backend call. Page
// geometry is cached up front precisely so layout never has to take that lock.
class PdfDocument {
public:
    struct PageInfo {
        QSizeF sizePoints; // 1 pt = 1/72 inch, PDF user space
        int rotation = 0;  // 0 / 90 / 180 / 270
    };

    PdfDocument();
    ~PdfDocument();

    PdfDocument(const PdfDocument &) = delete;
    PdfDocument &operator=(const PdfDocument &) = delete;

    bool load(const QString &filePath, const QString &password = {});
    void close();

    bool isValid() const { return m_valid; }
    QString filePath() const { return m_filePath; }
    QString lastError() const { return m_lastError; }

    int pageCount() const { return m_pages.size(); }
    PageInfo pageInfo(int index) const;

    // Rasterises one page at exactly `pixelSize`. Thread-safe.
    // Returns a null QImage on failure.
    QImage renderPage(int index, const QSize &pixelSize) const;

    // Document outline, flattened depth-first. Built once at load time.
    const QVector<OutlineItem> &outline() const { return m_outline; }

    // Full extracted text of one page. Thread-safe.
    QString pageText(int index) const;

    // Every match of `query` on one page. Thread-safe, and the unit of work
    // the search runs in -- one page per task keeps results streaming in
    // rather than arriving all at once at the end.
    QVector<SearchHit> searchPage(int index,
                                  const QString &query,
                                  bool matchCase,
                                  bool wholeWord) const;

    // -- Text selection ----------------------------------------------------
    //
    // All coordinates are PDF points with a top-left origin, matching the
    // search rectangles and what the page view draws in.

    int characterCount(int pageIndex) const;

    // Nearest character to a point, or -1 if none is within `tolerance`.
    int characterAt(int pageIndex, const QPointF &point, double tolerance = 6.0) const;

    // Index of the character whose box contains the point, else the closest
    // insertion position on that line. Used for drag selection, where the
    // pointer is regularly in the gutter between words.
    int insertionPointAt(int pageIndex, const QPointF &point) const;

    QVector<QRectF> rectsForRange(int pageIndex, int start, int count) const;
    QString textForRange(int pageIndex, int start, int count) const;

    // Expands a range to whole words / the whole line. Backs double- and
    // triple-click.
    void expandToWord(int pageIndex, int &start, int &count) const;
    void expandToLine(int pageIndex, int &start, int &count) const;

    // -- Editing -----------------------------------------------------------

    // Adds a text markup annotation covering `rects` (PDF points, top-left
    // origin -- the same space selection and search work in).
    bool addTextMarkup(int pageIndex,
                       MarkupType type,
                       const QVector<QRectF> &rects,
                       const QColor &color);

    // Number of annotations on a page, and removal by index. Index is
    // PDFium's own ordering, which is what hit-testing returns.
    int annotationCount(int pageIndex) const;
    bool removeAnnotation(int pageIndex, int annotationIndex);

    // Index of the topmost annotation whose rectangle contains `point`, or -1.
    int annotationAt(int pageIndex, const QPointF &point) const;

    // -- Page operations ---------------------------------------------------
    //
    // All of these renumber pages, so page geometry is rebuilt afterwards and
    // callers must reset any model they expose.

    bool rotatePage(int pageIndex, int quarterTurns);
    int pageRotation(int pageIndex) const;

    bool movePage(int from, int to);

    // Deletes a page, first copying it into `removedInto` so the operation can
    // be undone. Pass a document created with createScratch(); the page lands
    // at its end and the index is returned through `stashIndex`.
    bool deletePage(int pageIndex, PdfDocument *removedInto, int *stashIndex);

    // Copies one page out of `source` and inserts it at `atIndex`.
    bool insertPageFrom(const PdfDocument &source, int sourceIndex, int atIndex);

    // Copies pages out of `source` and inserts them at `atIndex`.
    //
    // `pageRange` uses PDFium's 1-based syntax ("1,3-5"); empty means every
    // page. Returns how many pages were inserted, or -1 on failure.
    int insertPagesFrom(const PdfDocument &source,
                        const QString &pageRange,
                        int atIndex);

    // Deletes a contiguous run without stashing it. Used to undo an insert,
    // where the pages can be re-imported from the source instead.
    bool deletePageRange(int start, int count);

    // Writes selected pages out as a new document. Does not modify this one.
    bool extractPagesTo(const QString &filePath, const QString &pageRange) const;

    // -- Ink -----------------------------------------------------------------

    // Draws stroked polylines onto a page as vector paths.
    //
    // Strokes arrive normalised to the unit square and are mapped into `target`
    // (PDF points, top-left origin). Normalised input keeps the coordinate
    // conversion in one place, and vector output means a signature stays crisp
    // at any zoom and adds a couple of kilobytes rather than a bitmap.
    bool addInkStrokes(int pageIndex,
                       const QVector<QVector<QPointF>> &strokes,
                       const QRectF &target,
                       const QColor &color,
                       double strokeWidth);

    // -- Redaction ---------------------------------------------------------

    // Permanently destroys page content inside `regions`, in PDF points with a
    // top-left origin.
    //
    // This removes content; it does not cover it up. A black rectangle drawn
    // over text leaves the text in the file for anyone who copies it out, which
    // is how redaction failures make the news.
    //
    // The page is flattened: rendered to a raster with the regions painted
    // black, and its objects replaced by that raster. The cost is that the page
    // loses selectable text everywhere, and callers must say so. See the
    // implementation for why the obvious alternative is worse.
    RedactionResult redactRegions(int pageIndex, const QVector<QRectF> &regions);

    // -- Compression -------------------------------------------------------

    struct CompressionReport {
        bool ok = false;
        int imagesExamined = 0;
        int imagesDownsampled = 0;
        qint64 pixelsBefore = 0;
        qint64 pixelsAfter = 0;
    };

    // Downsamples embedded images so that none exceeds `targetDpi` at the size
    // it is actually drawn. Images already at or below the target are left
    // exactly as they are -- re-encoding them would lose quality for nothing.
    CompressionReport downsampleImages(int targetDpi);

    // -- Export ------------------------------------------------------------

    // Renders one page to an image file at `dpi`. Format follows the file
    // extension. Thread-safe, so a range of pages can be exported in parallel.
    bool exportPageImage(int pageIndex,
                         const QString &filePath,
                         int dpi,
                         int quality = -1) const;

    // An empty in-memory document, used as the holding area for deleted pages.
    static QSharedPointer<PdfDocument> createScratch();

    bool isModified() const { return m_modified; }

    // Writes the document, annotations included. Saving over the original is
    // refused -- see the implementation for why.
    //
    // `compact` rewrites the file from scratch instead of appending an
    // incremental update. That is what actually reclaims space after edits, at
    // the cost of a slower write.
    bool saveAs(const QString &filePath, bool compact = false);

private:
    void buildOutline();
    void invalidatePage(int pageIndex);

    // Re-reads page sizes after an operation that renumbered pages.
    // Caller holds m_mutex.
    void rebuildPageInfo();

    // Text extraction handles for the most recently touched page, kept open.
    //
    // Hit-testing runs on every mouse move during a drag; loading and closing
    // a page plus its text layer each time would make selection visibly lag on
    // a dense page. Caller must hold m_mutex.
    void *acquireTextPage(int pageIndex) const;
    void releaseTextCache() const;
    QImage renderPlaceholder(int index, const QSize &pixelSize) const;

    mutable QMutex m_mutex;
    void *m_handle = nullptr; // FPDF_DOCUMENT
    bool m_valid = false;
    QString m_filePath;
    QString m_lastError;
    QVector<PageInfo> m_pages;
    QVector<OutlineItem> m_outline;

    bool m_modified = false;

    mutable int m_textCacheIndex = -1;
    mutable void *m_textCachePage = nullptr;      // FPDF_PAGE
    mutable void *m_textCacheTextPage = nullptr;  // FPDF_TEXTPAGE
};

} // namespace lumen
