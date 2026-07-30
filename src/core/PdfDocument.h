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
    // -- Forms ---------------------------------------------------------------
    //
    // AcroForm filling runs through PDFium's form-fill environment, which owns
    // the field widgets' appearance and all their editing behaviour. Everything
    // here is serialised by the same mutex as rendering, because the form
    // environment and the document are one unit as far as PDFium is concerned.

    // Number of AcroForm fields on a page.
    int formFieldCount(int pageIndex) const;

    bool hasForms() const { return m_hasForms; }

    // Field type under a point (PDF points, top-left origin), or -1 for none.
    // The values are PDFium's FPDF_FORMFIELD_* constants.
    int formFieldTypeAt(int pageIndex, const QPointF &point) const;

    // Pointer and keyboard input, forwarded to the focused field. Each returns
    // true when PDFium consumed the event, which is the caller's cue that the
    // page needs redrawing.
    bool formMousePress(int pageIndex, const QPointF &point, int modifiers);
    bool formMouseRelease(int pageIndex, const QPointF &point, int modifiers);
    bool formMouseMove(int pageIndex, const QPointF &point, int modifiers);
    bool formKeyPress(int pageIndex, int pdfiumKeyCode, int modifiers);
    bool formTextInput(int pageIndex, const QString &text);
    void formClearFocus();

    // Called from inside a PDFium form callback. Public only because the
    // callback is a free function; not part of the intended API.
    void markModifiedByForm();

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

    // -- Text editing ------------------------------------------------------
    //
    // PDFium can replace the string of a text object but cannot split one, so
    // the editable unit is the whole object. What that means in practice
    // depends entirely on how the producing application chose to group glyphs,
    // which is why the UI shows the user exactly what it is about to replace.

    // The topmost text object containing `point` (PDF points, top-left origin).
    TextObjectInfo textObjectAt(int pageIndex, const QPointF &point) const;

    QString textObjectString(int pageIndex, int objectIndex) const;

    // Replaces a text object's string. The glyphs are re-laid out from the
    // object's origin using the font's own advances, so any hand-placed
    // spacing inside the original run is lost -- callers must say so.
    bool setTextObjectString(int pageIndex, int objectIndex, const QString &text);

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

    // Creates and destroys the form-fill environment. Caller holds m_mutex.
    void initForms();
    void closeForms();

    // The page that currently holds form focus, kept loaded.
    //
    // FORM_OnAfterLoadPage / FORM_OnBeforeClosePage must bracket an entire
    // editing session, not each event: closing the page destroys the widget
    // state, including which field has focus. Bracketing per event means a
    // click focuses a field and the very next keystroke arrives with nothing
    // focused -- which is exactly the bug this cache exists to fix.
    // Caller holds m_mutex.
    void *acquireFormPage(int pageIndex) const;
    void releaseFormPage() const;

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

    bool m_hasForms = false;
    void *m_formHost = nullptr;    // FormHost, which wraps FPDF_FORMFILLINFO
    void *m_formHandle = nullptr;  // FPDF_FORMHANDLE

    mutable int m_formPageIndex = -1;
    mutable void *m_formPage = nullptr;   // FPDF_PAGE

    mutable int m_textCacheIndex = -1;
    mutable void *m_textCachePage = nullptr;      // FPDF_PAGE
    mutable void *m_textCacheTextPage = nullptr;  // FPDF_TEXTPAGE
};

} // namespace lumen
