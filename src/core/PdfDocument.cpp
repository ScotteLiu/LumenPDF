#include "core/PdfDocument.h"

#include "core/PdfEngine.h"

#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QPainter>
#include <QSet>

#include <limits>

#ifdef LUMEN_HAS_PDFIUM
#include <fpdf_annot.h>
#include <fpdf_doc.h>
#include <fpdf_formfill.h>
#include <fpdf_fwlevent.h>
#include <fpdf_edit.h>
#include <fpdf_ppo.h>
#include <fpdf_save.h>
#include <fpdf_text.h>
#include <fpdfview.h>
#endif

Q_LOGGING_CATEGORY(lcDoc, "lumen.document")

namespace lumen {

namespace {
// US Letter, used by the stub build so layout has something plausible to work
// with before PDFium is wired up.
constexpr QSizeF kFallbackPageSize { 612.0, 792.0 };
constexpr int kFallbackPageCount = 8;

// Characters of context to show on each side of a match in the results list.
constexpr int kSnippetContext = 42;

// Outlines deeper than this are almost always a malformed or hostile file; a
// cyclic bookmark tree would otherwise recurse until the stack runs out.
constexpr int kMaxOutlineDepth = 12;
constexpr int kMaxOutlineItems = 20000;

// Makes extracted PDF text fit to show in a UI list.
//
// Typeset documents are full of things that belong in a rendered page but not
// in a results list: ligature codepoints, soft hyphens left over from
// justification, zero-width joiners. Most UI fonts have no glyph for them and
// draw tofu instead. This is presentation only -- the page text and all search
// offsets into it are untouched.
QString expandLigatures(QString text);

// True for characters that stand alone as a selectable unit: Han ideographs,
// kana, Hangul. None of these are separated by spaces, so the Latin notion of
// a "word" does not apply to them.
bool isIdeographic(QChar c)
{
    switch (c.script()) {
    case QChar::Script_Han:
    case QChar::Script_Hiragana:
    case QChar::Script_Katakana:
    case QChar::Script_Hangul:
    case QChar::Script_Bopomofo:
        return true;
    default:
        return false;
    }
}

// Maps Kangxi Radicals and CJK Radicals Supplement to the unified ideographs
// they duplicate.
//
// PDFium extracts whatever the font's cmap points at, and CJK fonts routinely
// map glyphs through the radical blocks. The result reads correctly on screen
// and is wrong everywhere else: copying gives U+2F00 KANGXI RADICAL ONE instead
// of U+4E00, so pasting it somewhere else finds nothing, and searching for the
// character the user actually typed finds nothing either.
//
// Only 1:1 replacements are applied. That is essential, not an optimisation:
// character indices into this text are handed straight to
// FPDFText_CountRects/GetRect, so changing the length would misplace every
// highlight after the first substitution.
QString normalizeCjkRadicals(QString text)
{
    bool touched = false;

    for (int i = 0; i < text.size(); ++i) {
        const char16_t code = text.at(i).unicode();

        const bool inRadicalBlock =
            (code >= 0x2E80 && code <= 0x2EF3) ||   // CJK Radicals Supplement
            (code >= 0x2F00 && code <= 0x2FD5);     // Kangxi Radicals
        if (!inRadicalBlock)
            continue;

        // NFKC is what defines this mapping; applying it per character avoids
        // the rest of NFKC, which would also fold full-width punctuation to
        // ASCII -- wrong for Chinese, where full-width forms are correct.
        const QString folded = QString(text.at(i)).normalized(QString::NormalizationForm_KC);
        if (folded.size() != 1)
            continue;

        text[i] = folded.at(0);
        touched = true;
    }

    Q_UNUSED(touched)
    return text;
}

QString sanitizeForDisplay(QString text)
{
    // Invisible formatting characters: drop entirely.
    static const char16_t kDrop[] = {
        0x00AD,  // soft hyphen
        0x200B,  // zero-width space
        0x200C,  // zero-width non-joiner
        0x200D,  // zero-width joiner
        0xFEFF,  // byte order mark / zero-width no-break space
        0x0000,
    };
    for (const char16_t *c = kDrop; *c; ++c)
        text.remove(QChar(*c));

    // Anything else with no glyph in a normal UI font -- private use area,
    // unassigned control codes -- would draw as a box.
    text.removeIf([](QChar c) {
        return !c.isPrint() && c != u' ';
    });

    return expandLigatures(std::move(text));
}

// Ligature codepoints back to their component letters.
QString expandLigatures(QString text)
{
    // Written as codepoints, not character literals: the source would
    // otherwise depend on its own encoding surviving every tool that touches
    // it, and one round-trip through the wrong codec silently corrupts them.
    static const struct { char16_t from; const char16_t *to; } kLigatures[] = {
        { 0xFB00, u"ff" },
        { 0xFB01, u"fi" },
        { 0xFB02, u"fl" },
        { 0xFB03, u"ffi" },
        { 0xFB04, u"ffl" },
        { 0xFB05, u"st" },
        { 0xFB06, u"st" },
    };

    for (const auto &ligature : kLigatures) {
        if (text.contains(QChar(ligature.from)))
            text.replace(QChar(ligature.from), QString::fromUtf16(ligature.to));
    }
    return text;
}

// Trims a snippet back to whole words, so results never begin mid-syllable
// ("mbedded real-time systems"). Only trims when it does not eat the match.
void trimToWordBoundaries(QString &snippet, int &matchStart, int matchLength)
{
    // Leading: drop the partial first word, unless that would eat the match.
    const int firstSpace = snippet.indexOf(u' ');
    if (firstSpace >= 0 && firstSpace < matchStart) {
        int cut = firstSpace + 1;
        while (cut < matchStart && snippet.at(cut) == u' ')
            ++cut;
        snippet.remove(0, cut);
        matchStart -= cut;
    }

    // Trailing: drop back to the last space after the match ends.
    const int matchEnd = matchStart + matchLength;
    const int lastSpace = snippet.lastIndexOf(u' ');
    if (lastSpace > matchEnd)
        snippet.truncate(lastSpace);

    // Trailing whitespace only. trimmed() would also strip leading spaces and
    // silently invalidate matchStart.
    while (!snippet.isEmpty() && snippet.back() == u' ')
        snippet.chop(1);

    matchStart = qBound(0, matchStart, snippet.size());
}

#ifdef LUMEN_HAS_PDFIUM
// PDFium hands back UTF-16 into a caller-supplied buffer, and reports sizes in
// *bytes* including the terminator. This wraps that convention once.
template <typename Fn>
QString readUtf16(Fn &&fn)
{
    const unsigned long bytes = fn(nullptr, 0);
    if (bytes <= 2)
        return {};

    QByteArray buffer(static_cast<int>(bytes), Qt::Uninitialized);
    fn(reinterpret_cast<unsigned short *>(buffer.data()), bytes);

    return QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.constData()));
}
#endif

} // namespace

PdfDocument::PdfDocument() = default;

PdfDocument::~PdfDocument()
{
    close();
}

bool PdfDocument::load(const QString &filePath, const QString &password)
{
    close();

    QMutexLocker locker(&m_mutex);

    const QFileInfo info(filePath);
    if (!info.exists() || !info.isReadable()) {
        m_lastError = QStringLiteral("File is missing or unreadable: %1").arg(filePath);
        return false;
    }

    m_filePath = filePath;

#ifdef LUMEN_HAS_PDFIUM
    const QByteArray utf8Path = filePath.toUtf8();
    const QByteArray utf8Password = password.toUtf8();

    FPDF_DOCUMENT doc = FPDF_LoadDocument(
        utf8Path.constData(),
        password.isEmpty() ? nullptr : utf8Password.constData());

    if (!doc) {
        switch (FPDF_GetLastError()) {
        case FPDF_ERR_PASSWORD:
            m_lastError = QStringLiteral("This document is password protected.");
            break;
        case FPDF_ERR_FORMAT:
            m_lastError = QStringLiteral("This file is not a valid PDF.");
            break;
        default:
            m_lastError = QStringLiteral("Could not open the document.");
            break;
        }
        m_filePath.clear();
        return false;
    }

    m_handle = doc;

    const int count = FPDF_GetPageCount(doc);
    m_pages.reserve(count);
    for (int i = 0; i < count; ++i) {
        // GetPageSizeByIndexF avoids loading the full page object, which keeps
        // opening a 1000-page file close to instant.
        FS_SIZEF size {};
        if (FPDF_GetPageSizeByIndexF(doc, i, &size))
            m_pages.append(PageInfo { QSizeF(size.width, size.height), 0 });
        else
            m_pages.append(PageInfo { kFallbackPageSize, 0 });
    }
#else
    Q_UNUSED(password)
    m_pages.reserve(kFallbackPageCount);
    for (int i = 0; i < kFallbackPageCount; ++i)
        m_pages.append(PageInfo { kFallbackPageSize, 0 });
#endif

    m_valid = true;
    m_lastError.clear();

    buildOutline();
    initForms();

    qCInfo(lcDoc) << "opened" << filePath
                  << "pages:" << m_pages.size()
                  << "outline:" << m_outline.size();
    return true;
}

void PdfDocument::buildOutline()
{
    // Caller holds m_mutex.
    m_outline.clear();

#ifdef LUMEN_HAS_PDFIUM
    if (!m_handle)
        return;

    auto doc = static_cast<FPDF_DOCUMENT>(m_handle);

    // Iterative depth-first, pre-order. Recursion is avoided deliberately:
    // this runs on untrusted input and a crafted file can nest bookmarks
    // arbitrarily deep. `seen` additionally breaks sibling/child cycles,
    // which a malformed outline can contain.
    struct Frame {
        FPDF_BOOKMARK bookmark;
        int depth;
    };

    QSet<FPDF_BOOKMARK> seen;

    const auto childrenOf = [&](FPDF_BOOKMARK parent) {
        QList<FPDF_BOOKMARK> out;
        for (FPDF_BOOKMARK b = FPDFBookmark_GetFirstChild(doc, parent);
             b && out.size() < kMaxOutlineItems;
             b = FPDFBookmark_GetNextSibling(doc, b)) {
            if (seen.contains(b))
                break;
            seen.insert(b);
            out.append(b);
        }
        return out;
    };

    QList<Frame> stack;
    const QList<FPDF_BOOKMARK> roots = childrenOf(nullptr);
    // Pushed in reverse so popping yields document order.
    for (int i = roots.size() - 1; i >= 0; --i)
        stack.append({ roots.at(i), 0 });

    while (!stack.isEmpty() && m_outline.size() < kMaxOutlineItems) {
        const Frame frame = stack.takeLast();

        OutlineItem item;
        item.depth = frame.depth;
        item.title = readUtf16([&](unsigned short *buf, unsigned long len) {
            return FPDFBookmark_GetTitle(frame.bookmark, buf, len);
        });

        if (FPDF_DEST dest = FPDFBookmark_GetDest(doc, frame.bookmark))
            item.pageIndex = FPDFDest_GetDestPageIndex(doc, dest);

        const QList<FPDF_BOOKMARK> kids = (frame.depth + 1 < kMaxOutlineDepth)
            ? childrenOf(frame.bookmark)
            : QList<FPDF_BOOKMARK>();

        item.hasChildren = !kids.isEmpty();
        m_outline.append(item);

        for (int i = kids.size() - 1; i >= 0; --i)
            stack.append({ kids.at(i), frame.depth + 1 });
    }
#endif
}

void PdfDocument::close()
{
    QMutexLocker locker(&m_mutex);

    // All of these hold references into the document and must go first, and the
    // form page before the form environment that brackets it.
    releaseTextCache();
    releaseFormPage();
    closeForms();

#ifdef LUMEN_HAS_PDFIUM
    if (m_handle) {
        FPDF_CloseDocument(static_cast<FPDF_DOCUMENT>(m_handle));
        m_handle = nullptr;
    }
#endif

    m_pages.clear();
    m_outline.clear();
    m_filePath.clear();
    m_valid = false;
}

PdfDocument::PageInfo PdfDocument::pageInfo(int index) const
{
    // No lock: m_pages is only mutated in load()/close(), both of which run
    // before the page list is published to the UI.
    if (index < 0 || index >= m_pages.size())
        return PageInfo { kFallbackPageSize, 0 };
    return m_pages.at(index);
}

QImage PdfDocument::renderPage(int index, const QSize &pixelSize) const
{
    if (!m_valid || index < 0 || index >= m_pages.size())
        return {};
    if (pixelSize.width() <= 0 || pixelSize.height() <= 0)
        return {};

    // Total pixels, not just width. A page can declare any size it likes, and a
    // crafted one declaring 200000 inches square drove a single raster to
    // 268 MB even with the width already clamped -- the width guard alone does
    // nothing about an extreme aspect ratio. 40 megapixels is far more than any
    // real page at any usable zoom, and bounds what one hostile file can cost.
    constexpr qint64 kMaxRenderPixels = 40LL * 1000 * 1000;
    if (qint64(pixelSize.width()) * pixelSize.height() > kMaxRenderPixels) {
        qCWarning(lcDoc) << "refusing to render page" << index
                         << "at" << pixelSize << "-- over the pixel budget";
        return {};
    }

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return {};

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_handle), index);
    if (!page)
        return {};

    // ARGB32_Premultiplied is BGRA in memory on little-endian, which is exactly
    // PDFium's FPDFBitmap_BGRA layout -- so PDFium rasterises straight into the
    // QImage with no copy and no channel swap. The page is filled opaque white
    // first, so premultiplied and straight alpha coincide.
    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);

    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(
        pixelSize.width(),
        pixelSize.height(),
        FPDFBitmap_BGRA,
        image.bits(),
        static_cast<int>(image.bytesPerLine()));

    if (!bitmap) {
        FPDF_ClosePage(page);
        return {};
    }

    FPDFBitmap_FillRect(bitmap, 0, 0, pixelSize.width(), pixelSize.height(), 0xFFFFFFFF);
    FPDF_RenderPageBitmap(
        bitmap, page,
        0, 0, pixelSize.width(), pixelSize.height(),
        0,
        FPDF_ANNOT | FPDF_LCD_TEXT);

    // Form field widgets are drawn by the form-fill environment, not by the
    // page renderer. Without this pass a fillable PDF renders with empty holes
    // where its fields should be, and typing appears to do nothing.
    //
    // The page being edited must not be bracketed with
    // FORM_OnAfterLoadPage/OnBeforeClosePage here: PDFium caches page objects
    // per document, so this is the *same* page the editing session holds, and
    // closing it would drop the focused field mid-render. Rendering the edited
    // page therefore only draws; every other page gets the pair.
    if (m_formHandle) {
        auto form = static_cast<FPDF_FORMHANDLE>(m_formHandle);
        const bool isEditedPage = (m_formPageIndex == index && m_formPage);

        if (!isEditedPage)
            FORM_OnAfterLoadPage(page, form);

        FPDF_FFLDraw(form, bitmap, page,
                     0, 0, pixelSize.width(), pixelSize.height(),
                     0, FPDF_ANNOT | FPDF_LCD_TEXT);

        if (!isEditedPage)
            FORM_OnBeforeClosePage(page, form);
    }

    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);

    return image;
#else
    return renderPlaceholder(index, pixelSize);
#endif
}

QString PdfDocument::pageText(int index) const
{
    if (!m_valid || index < 0 || index >= m_pages.size())
        return {};

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return {};

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_handle), index);
    if (!page)
        return {};

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    if (!textPage) {
        FPDF_ClosePage(page);
        return {};
    }

    const int chars = FPDFText_CountChars(textPage);
    QString text;
    if (chars > 0) {
        // +1 for the terminator PDFium always writes.
        QVector<unsigned short> buffer(chars + 1, 0);
        const int written = FPDFText_GetText(textPage, 0, chars, buffer.data());
        if (written > 0) {
            text = QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.constData()),
                                      written - 1);
        }
    }

    FPDFText_ClosePage(textPage);
    FPDF_ClosePage(page);

    // Length-preserving, so indices into the result still line up with
    // PDFium's own character indices.
    return normalizeCjkRadicals(std::move(text));
#else
    return QStringLiteral("Page %1 placeholder text.").arg(index + 1);
#endif
}

QVector<SearchHit> PdfDocument::searchPage(int index,
                                           const QString &query,
                                           bool matchCase,
                                           bool wholeWord) const
{
    QVector<SearchHit> hits;

    if (!m_valid || query.isEmpty() || index < 0 || index >= m_pages.size())
        return hits;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return hits;

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_handle), index);
    if (!page)
        return hits;

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    if (!textPage) {
        FPDF_ClosePage(page);
        return hits;
    }

    // Extract the page text once, up front: it is needed for every snippet,
    // and pulling it per hit would re-walk the whole page each time.
    QString pageBody;
    const int charCount = FPDFText_CountChars(textPage);
    if (charCount > 0) {
        QVector<unsigned short> buffer(charCount + 1, 0);
        const int written = FPDFText_GetText(textPage, 0, charCount, buffer.data());
        if (written > 0) {
            pageBody = QString::fromUtf16(
                reinterpret_cast<const char16_t *>(buffer.constData()), written - 1);
        }
    }

    // Matching runs over our own normalised page text rather than
    // FPDFText_FindStart.
    //
    // PDFium searches the raw extracted text, which in CJK documents contains
    // Kangxi-radical duplicates -- so searching for the character the user typed
    // finds nothing, while the page visibly contains it. Normalisation is
    // length-preserving, so indices from QString::indexOf are still valid inputs
    // to FPDFText_CountRects, and matching now agrees with what the app copies
    // and displays.
    pageBody = normalizeCjkRadicals(std::move(pageBody));

    const QString needle = normalizeCjkRadicals(query);
    const auto sensitivity = matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;

    const double pageHeight = m_pages.at(index).sizePoints.height();

    for (int from = 0; from + needle.size() <= pageBody.size(); ) {
        const int at = pageBody.indexOf(needle, from, sensitivity);
        if (at < 0)
            break;

        from = at + 1;   // overlapping matches are still separate matches

        if (wholeWord) {
            // A boundary is the edge of the text, a non-word character, or the
            // change of script into or out of an ideographic run -- ideographs
            // have no spaces, so the Latin rule alone would reject every CJK
            // match.
            const auto isWordChar = [](QChar c) {
                return c.isLetterOrNumber() || c == u'_';
            };
            const bool leftOk = at == 0
                || !isWordChar(pageBody.at(at - 1))
                || isIdeographic(pageBody.at(at - 1)) != isIdeographic(needle.front());
            const int end = at + needle.size();
            const bool rightOk = end >= pageBody.size()
                || !isWordChar(pageBody.at(end))
                || isIdeographic(pageBody.at(end)) != isIdeographic(needle.back());

            if (!leftOk || !rightOk)
                continue;
        }

        SearchHit hit;
        hit.pageIndex = index;
        hit.charIndex = at;
        hit.charCount = needle.size();

        // Highlight geometry. A single match spans several rectangles when it
        // wraps across a line break.
        const int rectCount = FPDFText_CountRects(textPage, hit.charIndex, hit.charCount);
        hit.rects.reserve(rectCount);
        for (int r = 0; r < rectCount; ++r) {
            double left = 0, top = 0, right = 0, bottom = 0;
            if (!FPDFText_GetRect(textPage, r, &left, &top, &right, &bottom))
                continue;

            // PDF user space has its origin bottom-left and `top` above
            // `bottom`; flip into the top-left space the UI works in.
            hit.rects.append(QRectF(left,
                                    pageHeight - top,
                                    right - left,
                                    top - bottom));
        }

        // Snippet with the match kept inside it, trimmed to one line.
        if (!pageBody.isEmpty()) {
            // Not named `from`: that is the outer loop's cursor.
            const int snipFrom = qMax(0, hit.charIndex - kSnippetContext);
            const int snipTo = qMin(pageBody.size(),
                                    hit.charIndex + hit.charCount + kSnippetContext);
            QString snippet = pageBody.mid(snipFrom, snipTo - snipFrom);

            hit.snippetMatchStart = hit.charIndex - snipFrom;
            hit.snippetMatchLength = hit.charCount;

            // Collapse whitespace so multi-line matches read as one line. Done
            // in place, character for character, to keep the match offset
            // valid -- simplified() would silently shift it.
            for (int i = 0; i < snippet.size(); ++i) {
                if (snippet.at(i).isSpace())
                    snippet[i] = u' ';
            }

            // Ligature expansion changes the string length, so it has to run
            // before the offset is used for anything else. Expand the three
            // regions separately and recompute the offset from their sizes.
            const QString before = sanitizeForDisplay(snippet.left(hit.snippetMatchStart));
            const QString match = sanitizeForDisplay(
                snippet.mid(hit.snippetMatchStart, hit.snippetMatchLength));
            const QString after = sanitizeForDisplay(
                snippet.mid(hit.snippetMatchStart + hit.snippetMatchLength));

            snippet = before + match + after;
            hit.snippetMatchStart = before.size();
            hit.snippetMatchLength = match.size();

            trimToWordBoundaries(snippet, hit.snippetMatchStart, hit.snippetMatchLength);

            hit.snippet = snippet;
        }

        hits.append(hit);
    }

    FPDFText_ClosePage(textPage);
    FPDF_ClosePage(page);
#else
    Q_UNUSED(matchCase)
    Q_UNUSED(wholeWord)
#endif

    return hits;
}

// ---------------------------------------------------------------------------
// Text selection
// ---------------------------------------------------------------------------

void PdfDocument::releaseTextCache() const
{
    // Caller holds m_mutex.
#ifdef LUMEN_HAS_PDFIUM
    if (m_textCacheTextPage) {
        FPDFText_ClosePage(static_cast<FPDF_TEXTPAGE>(m_textCacheTextPage));
        m_textCacheTextPage = nullptr;
    }
    if (m_textCachePage) {
        FPDF_ClosePage(static_cast<FPDF_PAGE>(m_textCachePage));
        m_textCachePage = nullptr;
    }
#endif
    m_textCacheIndex = -1;
}

void *PdfDocument::acquireTextPage(int pageIndex) const
{
    // Caller holds m_mutex.
#ifdef LUMEN_HAS_PDFIUM
    if (m_textCacheIndex == pageIndex && m_textCacheTextPage)
        return m_textCacheTextPage;

    releaseTextCache();

    if (!m_handle)
        return nullptr;

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_handle), pageIndex);
    if (!page)
        return nullptr;

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    if (!textPage) {
        FPDF_ClosePage(page);
        return nullptr;
    }

    m_textCachePage = page;
    m_textCacheTextPage = textPage;
    m_textCacheIndex = pageIndex;
    return textPage;
#else
    Q_UNUSED(pageIndex)
    return nullptr;
#endif
}

int PdfDocument::characterCount(int pageIndex) const
{
    if (!m_valid || pageIndex < 0 || pageIndex >= m_pages.size())
        return 0;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    auto textPage = static_cast<FPDF_TEXTPAGE>(acquireTextPage(pageIndex));
    return textPage ? FPDFText_CountChars(textPage) : 0;
#else
    return 0;
#endif
}

int PdfDocument::characterAt(int pageIndex, const QPointF &point, double tolerance) const
{
    if (!m_valid || pageIndex < 0 || pageIndex >= m_pages.size())
        return -1;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    auto textPage = static_cast<FPDF_TEXTPAGE>(acquireTextPage(pageIndex));
    if (!textPage)
        return -1;

    // Back into PDFium's bottom-left user space.
    const double pageHeight = m_pages.at(pageIndex).sizePoints.height();
    return FPDFText_GetCharIndexAtPos(textPage,
                                      point.x(),
                                      pageHeight - point.y(),
                                      tolerance,
                                      tolerance);
#else
    Q_UNUSED(point)
    Q_UNUSED(tolerance)
    return -1;
#endif
}

int PdfDocument::insertionPointAt(int pageIndex, const QPointF &point) const
{
    if (!m_valid || pageIndex < 0 || pageIndex >= m_pages.size())
        return -1;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    auto textPage = static_cast<FPDF_TEXTPAGE>(acquireTextPage(pageIndex));
    if (!textPage)
        return -1;

    const double pageHeight = m_pages.at(pageIndex).sizePoints.height();
    const double userY = pageHeight - point.y();

    // Direct hit first -- the common case, and exact.
    const int direct = FPDFText_GetCharIndexAtPos(textPage, point.x(), userY, 4.0, 4.0);
    if (direct >= 0)
        return direct;

    // Otherwise find the nearest character. A drag spends much of its time in
    // the whitespace between words and past the end of a line, and selection
    // has to keep working there.
    const int count = FPDFText_CountChars(textPage);
    if (count <= 0)
        return -1;

    int best = -1;
    double bestScore = std::numeric_limits<double>::max();

    for (int i = 0; i < count; ++i) {
        double left = 0, right = 0, bottom = 0, top = 0;
        if (!FPDFText_GetCharBox(textPage, i, &left, &right, &bottom, &top))
            continue;

        const double centreY = (top + bottom) / 2.0;
        const double halfHeight = qMax(1.0, (top - bottom) / 2.0);

        // Vertical distance dominates: the character on the pointer's line is
        // always a better answer than a closer one on the line above.
        const double dy = qAbs(userY - centreY) / halfHeight;
        const double dx = (point.x() < left)  ? (left - point.x())
                        : (point.x() > right) ? (point.x() - right)
                                              : 0.0;

        const double score = dy * 1000.0 + dx;
        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }

    return best;
#else
    Q_UNUSED(point)
    return -1;
#endif
}

QVector<QRectF> PdfDocument::rectsForRange(int pageIndex, int start, int count) const
{
    QVector<QRectF> rects;
    if (!m_valid || count <= 0 || pageIndex < 0 || pageIndex >= m_pages.size())
        return rects;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    auto textPage = static_cast<FPDF_TEXTPAGE>(acquireTextPage(pageIndex));
    if (!textPage)
        return rects;

    const double pageHeight = m_pages.at(pageIndex).sizePoints.height();
    const int rectCount = FPDFText_CountRects(textPage, start, count);
    rects.reserve(rectCount);

    for (int i = 0; i < rectCount; ++i) {
        double left = 0, top = 0, right = 0, bottom = 0;
        if (!FPDFText_GetRect(textPage, i, &left, &top, &right, &bottom))
            continue;
        rects.append(QRectF(left, pageHeight - top, right - left, top - bottom));
    }
#else
    Q_UNUSED(start)
#endif

    return rects;
}

QString PdfDocument::textForRange(int pageIndex, int start, int count) const
{
    if (!m_valid || count <= 0 || pageIndex < 0 || pageIndex >= m_pages.size())
        return {};

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    auto textPage = static_cast<FPDF_TEXTPAGE>(acquireTextPage(pageIndex));
    if (!textPage)
        return {};

    QVector<unsigned short> buffer(count + 1, 0);
    const int written = FPDFText_GetText(textPage, start, count, buffer.data());
    if (written <= 1)
        return {};

    // Copying must give the characters the user believes they are copying, not
    // the radical-block duplicates the font happened to map through.
    return normalizeCjkRadicals(
        QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.constData()),
                           written - 1));
#else
    Q_UNUSED(start)
    return {};
#endif
}

void PdfDocument::expandToWord(int pageIndex, int &start, int &count) const
{
    const int total = characterCount(pageIndex);
    if (total <= 0 || start < 0 || start >= total)
        return;

    const QString page = pageText(pageIndex);
    if (page.isEmpty())
        return;

    int from = qBound(0, start, page.size() - 1);
    int to = qBound(0, start + qMax(1, count) - 1, page.size() - 1);

    // Ideographs are a unit on their own. Chinese, Japanese and Korean are not
    // space-separated, so the Latin rule "extend while the neighbour is a
    // letter" would swallow the entire run -- double-clicking one character
    // would select the whole line. Without a segmentation dictionary, one
    // character is the honest answer, and it is what the user can predict.
    if (isIdeographic(page.at(from))) {
        start = from;
        count = 1;
        return;
    }

    const auto isWordChar = [](QChar c) {
        return (c.isLetterOrNumber() || c == u'_') && !isIdeographic(c);
    };

    while (from > 0 && isWordChar(page.at(from - 1)))
        --from;
    while (to + 1 < page.size() && isWordChar(page.at(to + 1)))
        ++to;

    start = from;
    count = to - from + 1;
}

void PdfDocument::expandToLine(int pageIndex, int &start, int &count) const
{
    const QString page = pageText(pageIndex);
    if (page.isEmpty() || start < 0 || start >= page.size())
        return;

    int from = start;
    int to = qBound(0, start + qMax(1, count) - 1, page.size() - 1);

    while (from > 0 && page.at(from - 1) != u'\n' && page.at(from - 1) != u'\r')
        --from;
    while (to + 1 < page.size() && page.at(to + 1) != u'\n' && page.at(to + 1) != u'\r')
        ++to;

    start = from;
    count = to - from + 1;
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

void PdfDocument::invalidatePage(int pageIndex)
{
    // Caller holds m_mutex. Both caches hold a page object that editing may
    // have changed underneath them.
    if (m_textCacheIndex == pageIndex)
        releaseTextCache();
    if (m_formPageIndex == pageIndex)
        releaseFormPage();
}

bool PdfDocument::addTextMarkup(int pageIndex,
                                MarkupType type,
                                const QVector<QRectF> &rects,
                                const QColor &color)
{
    if (!m_valid || rects.isEmpty() || pageIndex < 0 || pageIndex >= m_pages.size())
        return false;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return false;

    invalidatePage(pageIndex);

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_handle), pageIndex);
    if (!page)
        return false;

    FPDF_ANNOTATION_SUBTYPE subtype = FPDF_ANNOT_HIGHLIGHT;
    switch (type) {
    case MarkupType::Highlight:  subtype = FPDF_ANNOT_HIGHLIGHT; break;
    case MarkupType::Underline:  subtype = FPDF_ANNOT_UNDERLINE; break;
    case MarkupType::StrikeOut:  subtype = FPDF_ANNOT_STRIKEOUT; break;
    case MarkupType::Squiggly:   subtype = FPDF_ANNOT_SQUIGGLY;  break;
    }

    FPDF_ANNOTATION annot = FPDFPage_CreateAnnot(page, subtype);
    if (!annot) {
        FPDF_ClosePage(page);
        return false;
    }

    const double pageHeight = m_pages.at(pageIndex).sizePoints.height();
    QRectF bounds;

    for (const QRectF &r : rects) {
        if (r.width() <= 0 || r.height() <= 0)
            continue;

        bounds = bounds.isNull() ? r : bounds.united(r);

        // Back to PDF user space (bottom-left origin). Quadpoint order is
        // fixed by the spec: upper-left, upper-right, lower-left, lower-right.
        const double top = pageHeight - r.top();
        const double bottom = pageHeight - r.bottom();

        FS_QUADPOINTSF quad {};
        quad.x1 = r.left();  quad.y1 = top;
        quad.x2 = r.right(); quad.y2 = top;
        quad.x3 = r.left();  quad.y3 = bottom;
        quad.x4 = r.right(); quad.y4 = bottom;

        FPDFAnnot_AppendAttachmentPoints(annot, &quad);
    }

    if (bounds.isNull()) {
        FPDFPage_CloseAnnot(annot);
        FPDF_ClosePage(page);
        return false;
    }

    FS_RECTF rect {};
    rect.left = bounds.left();
    rect.right = bounds.right();
    rect.top = pageHeight - bounds.top();
    rect.bottom = pageHeight - bounds.bottom();
    FPDFAnnot_SetRect(annot, &rect);

    FPDFAnnot_SetColor(annot, FPDFANNOT_COLORTYPE_Color,
                       color.red(), color.green(), color.blue(), color.alpha());

    FPDFPage_CloseAnnot(annot);

    // Without this the annotation exists in memory but is missing from the
    // page's object list when the document is written out.
    FPDFPage_GenerateContent(page);
    FPDF_ClosePage(page);

    m_modified = true;
    return true;
#else
    Q_UNUSED(type)
    Q_UNUSED(color)
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Forms
// ---------------------------------------------------------------------------

#ifdef LUMEN_HAS_PDFIUM
namespace {

// FPDF_FORMFILLINFO has no user-data field, but PDFium hands the struct pointer
// back to every callback -- so the struct is embedded as the first member here
// and the pointer is cast back. Same trick as FPDF_FILEWRITE above.
struct FormHost {
    FPDF_FORMFILLINFO base;
    PdfDocument *owner;
};

// PDFium null-checks most of these before calling, but not consistently across
// versions, and a missing callback is a crash rather than a degraded feature.
// They are all provided, and the ones that would need real plumbing do nothing
// on purpose -- see the notes.

void ffiInvalidate(FPDF_FORMFILLINFO *, FPDF_PAGE, double, double, double, double)
{
    // Nothing to do: every input call reports whether PDFium consumed the
    // event, and the caller redraws on that. Pushing invalidation through this
    // callback would mean crossing threads for information we already have.
}

void ffiOutputSelectedRect(FPDF_FORMFILLINFO *, FPDF_PAGE, double, double, double, double) {}

void ffiSetCursor(FPDF_FORMFILLINFO *, int) {}

int ffiSetTimer(FPDF_FORMFILLINFO *, int, TimerCallback)
{
    // No timer, so the text caret does not blink. A blinking caret would need a
    // QTimer owned by the GUI thread calling back into PDFium, and a still
    // caret is a much smaller cost than that machinery going wrong.
    return 0;
}

void ffiKillTimer(FPDF_FORMFILLINFO *, int) {}

FPDF_SYSTEMTIME ffiGetLocalTime(FPDF_FORMFILLINFO *)
{
    // Only read by form JavaScript, which is not enabled.
    return FPDF_SYSTEMTIME {};
}

void ffiOnChange(FPDF_FORMFILLINFO *pThis)
{
    if (auto *host = reinterpret_cast<FormHost *>(pThis))
        host->owner->markModifiedByForm();
}

FPDF_PAGE ffiGetPage(FPDF_FORMFILLINFO *, FPDF_DOCUMENT, int) { return nullptr; }
FPDF_PAGE ffiGetCurrentPage(FPDF_FORMFILLINFO *, FPDF_DOCUMENT) { return nullptr; }
int ffiGetRotation(FPDF_FORMFILLINFO *, FPDF_PAGE) { return 0; }
void ffiExecuteNamedAction(FPDF_FORMFILLINFO *, FPDF_BYTESTRING) {}
void ffiSetTextFieldFocus(FPDF_FORMFILLINFO *, FPDF_WIDESTRING, FPDF_DWORD, FPDF_BOOL) {}
void ffiDoURIAction(FPDF_FORMFILLINFO *, FPDF_BYTESTRING) {}
void ffiDoGoToAction(FPDF_FORMFILLINFO *, int, int, float *, int) {}

// Qt modifier bits to PDFium's.
int toPdfiumModifiers(int qtModifiers)
{
    int flags = 0;
    if (qtModifiers & Qt::ShiftModifier)
        flags |= FWL_EVENTFLAG_ShiftKey;
    if (qtModifiers & Qt::ControlModifier)
        flags |= FWL_EVENTFLAG_ControlKey;
    if (qtModifiers & Qt::AltModifier)
        flags |= FWL_EVENTFLAG_AltKey;
    return flags;
}

} // namespace
#endif

void PdfDocument::markModifiedByForm()
{
    // Called from inside a PDFium callback, so the mutex is already held by
    // whichever method triggered it -- do not try to take it again.
    m_modified = true;
}

void PdfDocument::initForms()
{
    // Caller holds m_mutex.
#ifdef LUMEN_HAS_PDFIUM
    if (!m_handle || m_formHandle)
        return;

    auto *host = new FormHost {};
    host->owner = this;

    host->base.version = 1;
    host->base.FFI_Invalidate = &ffiInvalidate;
    host->base.FFI_OutputSelectedRect = &ffiOutputSelectedRect;
    host->base.FFI_SetCursor = &ffiSetCursor;
    host->base.FFI_SetTimer = &ffiSetTimer;
    host->base.FFI_KillTimer = &ffiKillTimer;
    host->base.FFI_GetLocalTime = &ffiGetLocalTime;
    host->base.FFI_OnChange = &ffiOnChange;
    host->base.FFI_GetPage = &ffiGetPage;
    host->base.FFI_GetCurrentPage = &ffiGetCurrentPage;
    host->base.FFI_GetRotation = &ffiGetRotation;
    host->base.FFI_ExecuteNamedAction = &ffiExecuteNamedAction;
    host->base.FFI_SetTextFieldFocus = &ffiSetTextFieldFocus;
    host->base.FFI_DoURIAction = &ffiDoURIAction;
    host->base.FFI_DoGoToAction = &ffiDoGoToAction;

    FPDF_FORMHANDLE handle = FPDFDOC_InitFormFillEnvironment(
        static_cast<FPDF_DOCUMENT>(m_handle), &host->base);

    if (!handle) {
        delete host;
        return;
    }

    m_formHost = host;
    m_formHandle = handle;

    // Widgets are drawn by PDFium, so it needs to know they are fillable and
    // how to tint them. A faint blue wash reads as "you can type here" without
    // fighting the document.
    //
    // The byte order is BGR, not the RGB the header's "0xxxrrggbb" wording
    // suggests -- passing 0x3E8FFF produced orange on screen.
    FPDF_SetFormFieldHighlightColor(handle, FPDF_FORMFIELD_UNKNOWN, 0xFF8F3E);
    FPDF_SetFormFieldHighlightAlpha(handle, 38);

    // PDFium wants to know the document has been opened before fields behave.
    FORM_DoDocumentOpenAction(handle);

    // Does this document have any fields at all? Checked once so the UI can
    // stay entirely out of the way for the overwhelming majority of PDFs.
    m_hasForms = false;
    for (int i = 0; i < m_pages.size() && !m_hasForms; ++i) {
        FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_handle), i);
        if (!page)
            continue;

        const int annots = FPDFPage_GetAnnotCount(page);
        for (int a = 0; a < annots; ++a) {
            FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, a);
            if (!annot)
                continue;
            if (FPDFAnnot_GetSubtype(annot) == FPDF_ANNOT_WIDGET)
                m_hasForms = true;
            FPDFPage_CloseAnnot(annot);
            if (m_hasForms)
                break;
        }

        FPDF_ClosePage(page);
    }
#endif
}

void PdfDocument::closeForms()
{
    // Caller holds m_mutex.
#ifdef LUMEN_HAS_PDFIUM
    if (m_formHandle) {
        FPDFDOC_ExitFormFillEnvironment(static_cast<FPDF_FORMHANDLE>(m_formHandle));
        m_formHandle = nullptr;
    }
    if (m_formHost) {
        delete static_cast<FormHost *>(m_formHost);
        m_formHost = nullptr;
    }
#endif
    m_hasForms = false;
}

int PdfDocument::formFieldTypeAt(int pageIndex, const QPointF &point) const
{
    if (!m_valid || !m_hasForms || pageIndex < 0 || pageIndex >= m_pages.size())
        return -1;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);

    // Via the cached form page, never a fresh handle. PDFium caches page
    // objects per document, so loading and closing "another" handle for this
    // index closes the very page the editing session is holding -- which resets
    // the focused widget. That bug showed up as a second click failing to move
    // focus, so both fields' text ended up in the first one.
    auto page = static_cast<FPDF_PAGE>(acquireFormPage(pageIndex));
    if (!page)
        return -1;

    const double pageHeight = m_pages.at(pageIndex).sizePoints.height();
    return FPDFPage_HasFormFieldAtPoint(
        static_cast<FPDF_FORMHANDLE>(m_formHandle), page,
        point.x(), pageHeight - point.y());
#else
    Q_UNUSED(point)
    return -1;
#endif
}

void *PdfDocument::acquireFormPage(int pageIndex) const
{
    // Caller holds m_mutex.
#ifdef LUMEN_HAS_PDFIUM
    if (!m_handle || !m_formHandle)
        return nullptr;

    if (m_formPageIndex == pageIndex && m_formPage)
        return m_formPage;

    releaseFormPage();

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_handle), pageIndex);
    if (!page)
        return nullptr;

    FORM_OnAfterLoadPage(page, static_cast<FPDF_FORMHANDLE>(m_formHandle));

    m_formPage = page;
    m_formPageIndex = pageIndex;
    return page;
#else
    Q_UNUSED(pageIndex)
    return nullptr;
#endif
}

void PdfDocument::releaseFormPage() const
{
    // Caller holds m_mutex.
#ifdef LUMEN_HAS_PDFIUM
    if (m_formPage) {
        if (m_formHandle) {
            // This is also the point at which PDFium writes a field's edited
            // value back into the document, so it must not be skipped.
            FORM_OnBeforeClosePage(static_cast<FPDF_PAGE>(m_formPage),
                                   static_cast<FPDF_FORMHANDLE>(m_formHandle));
        }
        FPDF_ClosePage(static_cast<FPDF_PAGE>(m_formPage));
        m_formPage = nullptr;
    }
#endif
    m_formPageIndex = -1;
}

bool PdfDocument::formMousePress(int pageIndex, const QPointF &point, int modifiers)
{
    if (!m_valid || !m_hasForms || pageIndex < 0 || pageIndex >= m_pages.size())
        return false;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    auto page = static_cast<FPDF_PAGE>(acquireFormPage(pageIndex));
    if (!page)
        return false;

    const double pageHeight = m_pages.at(pageIndex).sizePoints.height();
    const double userX = point.x();
    const double userY = pageHeight - point.y();
    auto form = static_cast<FPDF_FORMHANDLE>(m_formHandle);
    const int flags = toPdfiumModifiers(modifiers);

    // Move before pressing. PDFium's widgets resolve a click against whichever
    // widget it currently considers hovered, and that state persists now that
    // the page stays loaded. Without this, every click after the first is
    // delivered to the field that was hovered first -- which showed up as two
    // fields' worth of typing landing in one of them.
    FORM_OnMouseMove(form, page, flags, userX, userY);

    return FORM_OnLButtonDown(form, page, flags, userX, userY);
#else
    Q_UNUSED(point) Q_UNUSED(modifiers)
    return false;
#endif
}

bool PdfDocument::formMouseRelease(int pageIndex, const QPointF &point, int modifiers)
{
    if (!m_valid || !m_hasForms || pageIndex < 0 || pageIndex >= m_pages.size())
        return false;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    auto page = static_cast<FPDF_PAGE>(acquireFormPage(pageIndex));
    if (!page)
        return false;

    const double pageHeight = m_pages.at(pageIndex).sizePoints.height();
    return FORM_OnLButtonUp(static_cast<FPDF_FORMHANDLE>(m_formHandle), page,
                            toPdfiumModifiers(modifiers),
                            point.x(), pageHeight - point.y());
#else
    Q_UNUSED(point) Q_UNUSED(modifiers)
    return false;
#endif
}

bool PdfDocument::formMouseMove(int pageIndex, const QPointF &point, int modifiers)
{
    if (!m_valid || !m_hasForms || pageIndex < 0 || pageIndex >= m_pages.size())
        return false;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    auto page = static_cast<FPDF_PAGE>(acquireFormPage(pageIndex));
    if (!page)
        return false;

    const double pageHeight = m_pages.at(pageIndex).sizePoints.height();
    return FORM_OnMouseMove(static_cast<FPDF_FORMHANDLE>(m_formHandle), page,
                            toPdfiumModifiers(modifiers),
                            point.x(), pageHeight - point.y());
#else
    Q_UNUSED(point) Q_UNUSED(modifiers)
    return false;
#endif
}

bool PdfDocument::formKeyPress(int pageIndex, int pdfiumKeyCode, int modifiers)
{
    if (!m_valid || !m_hasForms || pageIndex < 0 || pageIndex >= m_pages.size())
        return false;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    auto page = static_cast<FPDF_PAGE>(acquireFormPage(pageIndex));
    if (!page)
        return false;

    return FORM_OnKeyDown(static_cast<FPDF_FORMHANDLE>(m_formHandle), page,
                          pdfiumKeyCode, toPdfiumModifiers(modifiers));
#else
    Q_UNUSED(pdfiumKeyCode) Q_UNUSED(modifiers)
    return false;
#endif
}

bool PdfDocument::formTextInput(int pageIndex, const QString &text)
{
    if (!m_valid || !m_hasForms || text.isEmpty()
        || pageIndex < 0 || pageIndex >= m_pages.size()) {
        return false;
    }

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    auto page = static_cast<FPDF_PAGE>(acquireFormPage(pageIndex));
    if (!page)
        return false;

    auto form = static_cast<FPDF_FORMHANDLE>(m_formHandle);

    // One character at a time: FORM_OnChar takes a single codepoint, and typed
    // text can be several.
    bool any = false;
    for (const QChar c : text) {
        if (FORM_OnChar(form, page, c.unicode(), 0))
            any = true;
    }
    return any;
#else
    return false;
#endif
}

void PdfDocument::formClearFocus()
{
#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (m_formHandle) {
        FORM_ForceToKillFocus(static_cast<FPDF_FORMHANDLE>(m_formHandle));
        // Releasing the page is what flushes the edited value into the
        // document's field dictionaries. Without it the value lives only in
        // PDFium's widget and never reaches the saved file.
        releaseFormPage();
    }
#endif
}

int PdfDocument::formFieldCount(int pageIndex) const
{
    if (!m_valid || pageIndex < 0 || pageIndex >= m_pages.size())
        return 0;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return 0;

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_handle), pageIndex);
    if (!page)
        return 0;

    int fields = 0;
    const int count = FPDFPage_GetAnnotCount(page);
    for (int i = 0; i < count; ++i) {
        FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
        if (!annot)
            continue;
        if (FPDFAnnot_GetSubtype(annot) == FPDF_ANNOT_WIDGET)
            ++fields;
        FPDFPage_CloseAnnot(annot);
    }

    FPDF_ClosePage(page);
    return fields;
#else
    return 0;
#endif
}

int PdfDocument::annotationCount(int pageIndex) const
{
    if (!m_valid || pageIndex < 0 || pageIndex >= m_pages.size())
        return 0;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return 0;

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_handle), pageIndex);
    if (!page)
        return 0;

    const int count = FPDFPage_GetAnnotCount(page);
    FPDF_ClosePage(page);
    return count;
#else
    return 0;
#endif
}

int PdfDocument::annotationAt(int pageIndex, const QPointF &point) const
{
    if (!m_valid || pageIndex < 0 || pageIndex >= m_pages.size())
        return -1;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return -1;

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_handle), pageIndex);
    if (!page)
        return -1;

    const double pageHeight = m_pages.at(pageIndex).sizePoints.height();
    const double userY = pageHeight - point.y();

    int found = -1;
    const int count = FPDFPage_GetAnnotCount(page);

    // Backwards: later annotations paint on top, so the topmost hit wins.
    for (int i = count - 1; i >= 0; --i) {
        FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
        if (!annot)
            continue;

        FS_RECTF rect {};
        if (FPDFAnnot_GetRect(annot, &rect)) {
            const double left = qMin(rect.left, rect.right);
            const double right = qMax(rect.left, rect.right);
            const double bottom = qMin(rect.top, rect.bottom);
            const double top = qMax(rect.top, rect.bottom);

            if (point.x() >= left && point.x() <= right
                && userY >= bottom && userY <= top) {
                found = i;
            }
        }

        FPDFPage_CloseAnnot(annot);
        if (found >= 0)
            break;
    }

    FPDF_ClosePage(page);
    return found;
#else
    Q_UNUSED(point)
    return -1;
#endif
}

bool PdfDocument::removeAnnotation(int pageIndex, int annotationIndex)
{
    if (!m_valid || annotationIndex < 0 || pageIndex < 0 || pageIndex >= m_pages.size())
        return false;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return false;

    invalidatePage(pageIndex);

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_handle), pageIndex);
    if (!page)
        return false;

    const bool ok = FPDFPage_RemoveAnnot(page, annotationIndex);
    if (ok) {
        FPDFPage_GenerateContent(page);
        m_modified = true;
    }

    FPDF_ClosePage(page);
    return ok;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Page operations
// ---------------------------------------------------------------------------

void PdfDocument::rebuildPageInfo()
{
    // Caller holds m_mutex.
    m_pages.clear();

#ifdef LUMEN_HAS_PDFIUM
    if (!m_handle)
        return;

    auto doc = static_cast<FPDF_DOCUMENT>(m_handle);
    const int count = FPDF_GetPageCount(doc);
    m_pages.reserve(count);

    for (int i = 0; i < count; ++i) {
        FS_SIZEF size {};
        if (FPDF_GetPageSizeByIndexF(doc, i, &size))
            m_pages.append(PageInfo { QSizeF(size.width, size.height), 0 });
        else
            m_pages.append(PageInfo { kFallbackPageSize, 0 });
    }
#endif
}

QSharedPointer<PdfDocument> PdfDocument::createScratch()
{
    auto scratch = QSharedPointer<PdfDocument>::create();

#ifdef LUMEN_HAS_PDFIUM
    FPDF_DOCUMENT doc = FPDF_CreateNewDocument();
    if (!doc)
        return scratch;

    scratch->m_handle = doc;
    scratch->m_valid = true;
#endif

    return scratch;
}

int PdfDocument::pageRotation(int pageIndex) const
{
    if (!m_valid || pageIndex < 0 || pageIndex >= m_pages.size())
        return 0;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return 0;

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_handle), pageIndex);
    if (!page)
        return 0;

    const int rotation = FPDFPage_GetRotation(page) * 90;
    FPDF_ClosePage(page);
    return rotation;
#else
    return 0;
#endif
}

bool PdfDocument::rotatePage(int pageIndex, int quarterTurns)
{
    if (!m_valid || pageIndex < 0 || pageIndex >= m_pages.size())
        return false;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return false;

    invalidatePage(pageIndex);

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_handle), pageIndex);
    if (!page)
        return false;

    // PDFium counts rotation in quarter turns, 0..3. Normalise into range so
    // negative deltas (rotate left) work without special-casing.
    const int current = FPDFPage_GetRotation(page);
    const int next = ((current + quarterTurns) % 4 + 4) % 4;
    FPDFPage_SetRotation(page, next);

    FPDFPage_GenerateContent(page);
    FPDF_ClosePage(page);

    // Rotating swaps width and height as far as everything downstream is
    // concerned, so the cached geometry is now wrong.
    rebuildPageInfo();

    m_modified = true;
    return true;
#else
    Q_UNUSED(quarterTurns)
    return false;
#endif
}

bool PdfDocument::movePage(int from, int to)
{
    if (!m_valid || from == to)
        return false;
    if (from < 0 || from >= m_pages.size() || to < 0 || to >= m_pages.size())
        return false;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return false;

    releaseTextCache();

    const int index = from;
    if (!FPDF_MovePages(static_cast<FPDF_DOCUMENT>(m_handle), &index, 1, to))
        return false;

    rebuildPageInfo();
    buildOutline();   // outline destinations are page indices

    m_modified = true;
    return true;
#else
    return false;
#endif
}

bool PdfDocument::deletePage(int pageIndex, PdfDocument *removedInto, int *stashIndex)
{
    if (!m_valid || pageIndex < 0 || pageIndex >= m_pages.size())
        return false;

    // Refusing to empty the document entirely: a zero-page PDF is invalid, and
    // the user almost certainly meant to close the file instead.
    if (m_pages.size() <= 1)
        return false;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return false;

    releaseTextCache();

    // Copy the page somewhere safe first, so this can be undone. PDFium has no
    // undo of its own -- the stash *is* the undo buffer.
    if (removedInto && removedInto->m_handle) {
        QMutexLocker stashLock(&removedInto->m_mutex);

        auto stashDoc = static_cast<FPDF_DOCUMENT>(removedInto->m_handle);
        const int before = FPDF_GetPageCount(stashDoc);

        const QByteArray range = QByteArray::number(pageIndex + 1);   // 1-based
        if (FPDF_ImportPages(stashDoc,
                             static_cast<FPDF_DOCUMENT>(m_handle),
                             range.constData(),
                             before)) {
            if (stashIndex)
                *stashIndex = before;
        } else if (stashIndex) {
            *stashIndex = -1;
        }
    } else if (stashIndex) {
        *stashIndex = -1;
    }

    FPDFPage_Delete(static_cast<FPDF_DOCUMENT>(m_handle), pageIndex);

    rebuildPageInfo();
    buildOutline();

    m_modified = true;
    return true;
#else
    Q_UNUSED(removedInto)
    Q_UNUSED(stashIndex)
    return false;
#endif
}

bool PdfDocument::insertPageFrom(const PdfDocument &source, int sourceIndex, int atIndex)
{
    if (!m_valid || sourceIndex < 0)
        return false;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle || !source.m_handle)
        return false;

    releaseTextCache();

    const QByteArray range = QByteArray::number(sourceIndex + 1);   // 1-based
    const int target = qBound(0, atIndex, m_pages.size());

    if (!FPDF_ImportPages(static_cast<FPDF_DOCUMENT>(m_handle),
                          static_cast<FPDF_DOCUMENT>(source.m_handle),
                          range.constData(),
                          target)) {
        return false;
    }

    rebuildPageInfo();
    buildOutline();

    m_modified = true;
    return true;
#else
    Q_UNUSED(source)
    Q_UNUSED(atIndex)
    return false;
#endif
}

int PdfDocument::insertPagesFrom(const PdfDocument &source,
                                 const QString &pageRange,
                                 int atIndex)
{
    if (!m_valid)
        return -1;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle || !source.m_handle)
        return -1;

    releaseTextCache();

    const int before = FPDF_GetPageCount(static_cast<FPDF_DOCUMENT>(m_handle));
    const int target = qBound(0, atIndex, before);

    // A null range means "every page"; PDFium treats it that way explicitly,
    // which saves building "1-N" and getting the off-by-one wrong.
    const QByteArray range = pageRange.toLatin1();

    if (!FPDF_ImportPages(static_cast<FPDF_DOCUMENT>(m_handle),
                          static_cast<FPDF_DOCUMENT>(source.m_handle),
                          pageRange.isEmpty() ? nullptr : range.constData(),
                          target)) {
        return -1;
    }

    const int after = FPDF_GetPageCount(static_cast<FPDF_DOCUMENT>(m_handle));

    rebuildPageInfo();
    buildOutline();

    m_modified = true;
    return after - before;
#else
    Q_UNUSED(source)
    Q_UNUSED(pageRange)
    Q_UNUSED(atIndex)
    return -1;
#endif
}

bool PdfDocument::deletePageRange(int start, int count)
{
    if (!m_valid || count <= 0 || start < 0 || start + count > m_pages.size())
        return false;

    // Same rule as deletePage: never leave a zero-page document behind.
    if (m_pages.size() - count < 1)
        return false;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return false;

    releaseTextCache();

    // Back to front, so each deletion cannot shift the indices still to come.
    for (int i = start + count - 1; i >= start; --i)
        FPDFPage_Delete(static_cast<FPDF_DOCUMENT>(m_handle), i);

    rebuildPageInfo();
    buildOutline();

    m_modified = true;
    return true;
#else
    return false;
#endif
}

#ifdef LUMEN_HAS_PDFIUM
namespace {

// PDFium writes through a callback struct rather than to a path. The struct
// must stay valid for the whole call, and its first member must be the
// FPDF_FILEWRITE so the C side can cast between them.
struct FileWriter {
    FPDF_FILEWRITE base;
    QFile *file;
    bool failed;

    static int write(FPDF_FILEWRITE *self, const void *data, unsigned long size)
    {
        auto *writer = reinterpret_cast<FileWriter *>(self);
        if (writer->failed)
            return 0;

        const qint64 written = writer->file->write(static_cast<const char *>(data),
                                                   qint64(size));
        if (written != qint64(size)) {
            writer->failed = true;
            return 0;
        }
        return 1;
    }
};

} // namespace
#endif

bool PdfDocument::saveAs(const QString &filePath, bool compact)
{
    if (!m_valid || filePath.isEmpty())
        return false;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return false;

    // Writing over the file PDFium is still reading from would corrupt it:
    // FPDF_SaveAsCopy streams out object data that it pulls from the original
    // on demand. Callers save to a temporary and swap.
    if (QFileInfo(filePath) == QFileInfo(m_filePath)) {
        m_lastError = QStringLiteral("Cannot overwrite the file currently open.");
        return false;
    }

    QFile out(filePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_lastError = QStringLiteral("Could not write to %1").arg(filePath);
        return false;
    }

    FileWriter writer {};
    writer.base.version = 1;
    writer.base.WriteBlock = &FileWriter::write;
    writer.file = &out;
    writer.failed = false;

    // Incremental appends the changes and leaves the original bytes in place,
    // which is fast and keeps the file's history. A compact save rewrites
    // everything, which is the only way to actually reclaim space from
    // replaced images or deleted pages.
    const bool ok = FPDF_SaveAsCopy(static_cast<FPDF_DOCUMENT>(m_handle),
                                    &writer.base,
                                    compact ? 0 : FPDF_INCREMENTAL);

    out.close();

    if (!ok || writer.failed) {
        out.remove();
        m_lastError = QStringLiteral("PDFium failed to write the document.");
        return false;
    }

    m_modified = false;
    return true;
#else
    Q_UNUSED(filePath)
    return false;
#endif
}

bool PdfDocument::extractPagesTo(const QString &filePath, const QString &pageRange) const
{
    if (!m_valid || filePath.isEmpty())
        return false;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return false;

    // Build a fresh document holding only the wanted pages, then write that.
    // Copying out is the only safe direction: filtering in place would mean
    // deleting from the document the user still has open.
    FPDF_DOCUMENT out = FPDF_CreateNewDocument();
    if (!out)
        return false;

    const QByteArray range = pageRange.toLatin1();
    const bool imported = FPDF_ImportPages(out,
                                           static_cast<FPDF_DOCUMENT>(m_handle),
                                           pageRange.isEmpty() ? nullptr : range.constData(),
                                           0);

    if (!imported || FPDF_GetPageCount(out) == 0) {
        FPDF_CloseDocument(out);
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        FPDF_CloseDocument(out);
        return false;
    }

    FileWriter writer {};
    writer.base.version = 1;
    writer.base.WriteBlock = &FileWriter::write;
    writer.file = &file;
    writer.failed = false;

    const bool ok = FPDF_SaveAsCopy(out, &writer.base, 0);

    file.close();
    FPDF_CloseDocument(out);

    if (!ok || writer.failed) {
        file.remove();
        return false;
    }

    return true;
#else
    Q_UNUSED(pageRange)
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Text editing
// ---------------------------------------------------------------------------

namespace {
// Beyond this many characters, a run is almost certainly a full line or
// paragraph whose spacing the author positioned deliberately -- so replacing it
// is likely to shift things visibly. Used only to warn.
constexpr int kLongRunThreshold = 40;
} // namespace

TextObjectInfo PdfDocument::textObjectAt(int pageIndex, const QPointF &point) const
{
    TextObjectInfo info;
    if (!m_valid || pageIndex < 0 || pageIndex >= m_pages.size())
        return info;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return info;

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_handle), pageIndex);
    if (!page)
        return info;

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    const double pageHeight = m_pages.at(pageIndex).sizePoints.height();
    const double userY = pageHeight - point.y();

    // Backwards: later objects paint on top, so the topmost hit is the one the
    // user believes they clicked.
    const int count = FPDFPage_CountObjects(page);
    for (int i = count - 1; i >= 0; --i) {
        FPDF_PAGEOBJECT object = FPDFPage_GetObject(page, i);
        if (!object || FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_TEXT)
            continue;

        float left = 0, bottom = 0, right = 0, top = 0;
        if (!FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top))
            continue;

        if (point.x() < left || point.x() > right || userY < bottom || userY > top)
            continue;

        info.objectIndex = i;
        info.bounds = QRectF(left, pageHeight - top, right - left, top - bottom);

        float size = 0;
        if (FPDFTextObj_GetFontSize(object, &size))
            info.fontSize = size;

        if (textPage) {
            info.text = readUtf16([&](unsigned short *buf, unsigned long len) {
                return FPDFTextObj_GetText(object, textPage, buf, len);
            });
            info.text = normalizeCjkRadicals(std::move(info.text));
        }

        info.spansMuchText = info.text.size() > kLongRunThreshold;
        info.valid = true;
        break;
    }

    if (textPage)
        FPDFText_ClosePage(textPage);
    FPDF_ClosePage(page);
#else
    Q_UNUSED(point)
#endif

    return info;
}

QString PdfDocument::textObjectString(int pageIndex, int objectIndex) const
{
    if (!m_valid || objectIndex < 0 || pageIndex < 0 || pageIndex >= m_pages.size())
        return {};

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return {};

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_handle), pageIndex);
    if (!page)
        return {};

    QString text;
    FPDF_PAGEOBJECT object = FPDFPage_GetObject(page, objectIndex);
    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);

    if (object && textPage && FPDFPageObj_GetType(object) == FPDF_PAGEOBJ_TEXT) {
        text = readUtf16([&](unsigned short *buf, unsigned long len) {
            return FPDFTextObj_GetText(object, textPage, buf, len);
        });
        text = normalizeCjkRadicals(std::move(text));
    }

    if (textPage)
        FPDFText_ClosePage(textPage);
    FPDF_ClosePage(page);
    return text;
#else
    return {};
#endif
}

bool PdfDocument::setTextObjectString(int pageIndex, int objectIndex, const QString &text)
{
    if (!m_valid || objectIndex < 0 || pageIndex < 0 || pageIndex >= m_pages.size())
        return false;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return false;

    invalidatePage(pageIndex);

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_handle), pageIndex);
    if (!page)
        return false;

    FPDF_PAGEOBJECT object = FPDFPage_GetObject(page, objectIndex);
    if (!object || FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_TEXT) {
        FPDF_ClosePage(page);
        return false;
    }

    // Keep the original so the edit can be rolled back if it does not survive.
    QString previous;
    if (FPDF_TEXTPAGE beforePage = FPDFText_LoadPage(page)) {
        previous = readUtf16([&](unsigned short *buf, unsigned long len) {
            return FPDFTextObj_GetText(object, beforePage, buf, len);
        });
        FPDFText_ClosePage(beforePage);
    }

    // FPDF_WIDESTRING is UTF-16LE, which QString already is internally.
    if (!FPDFText_SetText(object, reinterpret_cast<FPDF_WIDESTRING>(text.utf16()))) {
        FPDF_ClosePage(page);
        return false;
    }

    FPDFPage_GenerateContent(page);

    // Verify the edit round-trips, and roll it back if it does not.
    //
    // Most real PDFs embed fonts as *subsets* containing only the glyphs the
    // document already uses. Setting text that needs any other glyph produces
    // silent garbage -- editing "Latin fixture page 1" to "Edited by LumenPDF"
    // yielded "Latin fixite  Luen ure page 1". PDFium reports success either
    // way, so the only way to know is to read the text back.
    //
    // Corrupting someone's document and reporting success is far worse than
    // refusing an edit, so a mismatch reverts and fails.
    QString readBack;
    if (FPDF_TEXTPAGE afterPage = FPDFText_LoadPage(page)) {
        readBack = readUtf16([&](unsigned short *buf, unsigned long len) {
            return FPDFTextObj_GetText(object, afterPage, buf, len);
        });
        FPDFText_ClosePage(afterPage);
    }

    const auto squashed = [](QString value) {
        // Extraction can differ from the input in whitespace alone, which is
        // not a corrupted edit.
        return value.simplified();
    };

    if (squashed(normalizeCjkRadicals(readBack)) != squashed(text)) {
        qCWarning(lcDoc) << "text edit did not survive re-encoding on page" << pageIndex
                         << "object" << objectIndex
                         << "-- wanted" << text << "got" << readBack;

        FPDFText_SetText(object, reinterpret_cast<FPDF_WIDESTRING>(previous.utf16()));
        FPDFPage_GenerateContent(page);
        FPDF_ClosePage(page);
        return false;
    }

    m_modified = true;
    qCInfo(lcDoc) << "edited text object" << objectIndex << "on page" << pageIndex;

    FPDF_ClosePage(page);
    return true;
#else
    Q_UNUSED(text)
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Ink
// ---------------------------------------------------------------------------

bool PdfDocument::addInkStrokes(int pageIndex,
                                const QVector<QVector<QPointF>> &strokes,
                                const QRectF &target,
                                const QColor &color,
                                double strokeWidth)
{
    if (!m_valid || strokes.isEmpty() || pageIndex < 0 || pageIndex >= m_pages.size())
        return false;
    if (target.width() <= 0 || target.height() <= 0)
        return false;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return false;

    invalidatePage(pageIndex);

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_handle), pageIndex);
    if (!page)
        return false;

    const double pageHeight = m_pages.at(pageIndex).sizePoints.height();

    // Unit square -> target rect -> PDF user space (bottom-left origin), in one
    // step so rounding happens once.
    const auto mapPoint = [&](const QPointF &normalised) {
        const double x = target.left() + normalised.x() * target.width();
        const double yTopLeft = target.top() + normalised.y() * target.height();
        return QPointF(x, pageHeight - yTopLeft);
    };

    int drawn = 0;

    for (const QVector<QPointF> &stroke : strokes) {
        if (stroke.size() < 2)
            continue;

        const QPointF first = mapPoint(stroke.first());
        FPDF_PAGEOBJECT path = FPDFPageObj_CreateNewPath(float(first.x()), float(first.y()));
        if (!path)
            continue;

        for (int i = 1; i < stroke.size(); ++i) {
            const QPointF p = mapPoint(stroke.at(i));
            FPDFPath_LineTo(path, float(p.x()), float(p.y()));
        }

        FPDFPageObj_SetStrokeColor(path, color.red(), color.green(), color.blue(), color.alpha());
        FPDFPageObj_SetStrokeWidth(path, float(qBound(0.2, strokeWidth, 20.0)));
        FPDFPageObj_SetLineCap(path, FPDF_LINECAP_ROUND);
        FPDFPageObj_SetLineJoin(path, FPDF_LINEJOIN_ROUND);

        // Stroke only, no fill: a signature is a line, and filling it would
        // blob together every place the stroke crosses itself.
        FPDFPath_SetDrawMode(path, FPDF_FILLMODE_NONE, 1);

        FPDFPage_InsertObject(page, path);
        ++drawn;
    }

    FPDFPage_GenerateContent(page);
    FPDF_ClosePage(page);

    if (drawn == 0)
        return false;

    m_modified = true;
    qCInfo(lcDoc) << "inked" << drawn << "strokes on page" << pageIndex;
    return true;
#else
    Q_UNUSED(target)
    Q_UNUSED(color)
    Q_UNUSED(strokeWidth)
    return false;
#endif
}

// ---------------------------------------------------------------------------
// OCR text layer
// ---------------------------------------------------------------------------

bool PdfDocument::pageHasText(int pageIndex) const
{
    // A handful of stray characters is not a text layer -- scanned pages often
    // carry a page number or a stamp that was never part of the scan.
    return pageText(pageIndex).simplified().size() > 24;
}

bool PdfDocument::addInvisibleTextLayer(int pageIndex,
                                        const QVector<RecognisedWord> &words)
{
    if (!m_valid || words.isEmpty() || pageIndex < 0 || pageIndex >= m_pages.size())
        return false;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return false;

    invalidatePage(pageIndex);

    auto doc = static_cast<FPDF_DOCUMENT>(m_handle);
    FPDF_PAGE page = FPDF_LoadPage(doc, pageIndex);
    if (!page)
        return false;

    // Helvetica: one of the fourteen standard fonts, so nothing is embedded and
    // the layer costs almost nothing. It is never drawn, so its shapes do not
    // matter -- only its advances, which is what positions the selection.
    FPDF_FONT font = FPDFText_LoadStandardFont(doc, "Helvetica");
    if (!font) {
        FPDF_ClosePage(page);
        return false;
    }

    const double pageHeight = m_pages.at(pageIndex).sizePoints.height();
    int placed = 0;

    for (const RecognisedWord &word : words) {
        const QString text = word.text.trimmed();
        if (text.isEmpty() || word.box.width() <= 0 || word.box.height() <= 0)
            continue;

        // Font size from the box height. The exact value hardly matters because
        // the matrix below rescales it; starting close keeps that scale sane.
        const double fontSize = qMax(1.0, word.box.height() * 0.8);

        FPDF_PAGEOBJECT object = FPDFPageObj_CreateTextObj(doc, font, float(fontSize));
        if (!object)
            continue;

        if (!FPDFText_SetText(object, reinterpret_cast<FPDF_WIDESTRING>(text.utf16()))) {
            FPDFPageObj_Destroy(object);
            continue;
        }

        // Render mode 3 is "invisible": laid out and selectable, never painted.
        // Without it the OCR guesses would be printed on top of the scan.
        FPDFTextObj_SetTextRenderMode(object, FPDF_TEXTRENDERMODE_INVISIBLE);

        // Measure what the text actually occupies, then scale it to the box the
        // recogniser reported. Guessing the width from character counts drifts
        // badly on anything but even-width text.
        float left = 0, bottom = 0, right = 0, top = 0;
        if (!FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top)) {
            FPDFPageObj_Destroy(object);
            continue;
        }

        const double naturalWidth = right - left;
        const double scaleX = naturalWidth > 0.01 ? word.box.width() / naturalWidth : 1.0;

        // Into PDF user space, with the baseline set from the box's bottom.
        const double targetX = word.box.left();
        const double targetY = pageHeight - word.box.bottom() + word.box.height() * 0.18;

        FS_MATRIX matrix { float(scaleX), 0.0f, 0.0f, 1.0f,
                           float(targetX), float(targetY) };
        FPDFPageObj_SetMatrix(object, &matrix);

        FPDFPage_InsertObject(page, object);
        ++placed;
    }

    if (placed == 0) {
        FPDF_ClosePage(page);
        return false;
    }

    FPDFPage_GenerateContent(page);
    FPDF_ClosePage(page);

    m_modified = true;
    qCInfo(lcDoc) << "wrote an invisible text layer of" << placed
                  << "words on page" << pageIndex;
    return true;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Redaction
// ---------------------------------------------------------------------------

RedactionResult PdfDocument::redactRegions(int pageIndex, const QVector<QRectF> &regions)
{
    RedactionResult result;

    if (!m_valid || regions.isEmpty() || pageIndex < 0 || pageIndex >= m_pages.size())
        return result;

#ifdef LUMEN_HAS_PDFIUM
    const QSizeF pointSize = m_pages.at(pageIndex).sizePoints;
    if (pointSize.width() <= 0 || pointSize.height() <= 0)
        return result;

    // Why flattening, and not surgical object removal:
    //
    // The obvious implementation walks the page's objects, removes the ones
    // intersecting the region, and paints a black box. It was tried, and on a
    // typical LaTeX-produced PDF it wiped the entire page -- because in such
    // files a single text object is a whole paragraph or column, so *any*
    // overlap condemns all of it.
    //
    // PDFium offers no way to split a text object or delete individual glyphs,
    // so precise object-level redaction is not achievable through it. The
    // remaining choices are to destroy far more than was asked (visibly
    // mangling the page) or to render the page and replace it with the raster.
    // Rasterising is the one that both looks right and provably leaks nothing:
    // there is no text left under the box because there is no text at all.
    //
    // The cost -- the page loses selectable text everywhere -- is real, and the
    // caller is expected to tell the user about it rather than hide it.
    constexpr int kRedactionDpi = 300;
    const double scale = kRedactionDpi / 72.0;

    const QSize pixelSize(qMax(1, qRound(pointSize.width() * scale)),
                          qMax(1, qRound(pointSize.height() * scale)));

    // renderPage takes the lock itself, so it must be called before ours.
    QImage raster = renderPage(pageIndex, pixelSize);
    if (raster.isNull())
        return result;

    {
        QPainter painter(&raster);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);

        for (const QRectF &r : regions) {
            if (r.width() <= 0 || r.height() <= 0)
                continue;
            painter.drawRect(QRectF(r.left() * scale,
                                    r.top() * scale,
                                    r.width() * scale,
                                    r.height() * scale));
            result.blackedOut.append(r);
        }
    }

    if (result.blackedOut.isEmpty())
        return result;

    // PDFium's image objects want BGRA, which is this format's memory layout on
    // little-endian -- the same trick the page renderer uses.
    if (raster.format() != QImage::Format_ARGB32_Premultiplied)
        raster = raster.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return result;

    invalidatePage(pageIndex);

    auto doc = static_cast<FPDF_DOCUMENT>(m_handle);
    FPDF_PAGE page = FPDF_LoadPage(doc, pageIndex);
    if (!page)
        return result;

    // Strip the page bare. Backwards, because removing shifts the indices above.
    const int objectCount = FPDFPage_CountObjects(page);
    for (int i = objectCount - 1; i >= 0; --i) {
        FPDF_PAGEOBJECT object = FPDFPage_GetObject(page, i);
        if (!object)
            continue;

        const int type = FPDFPageObj_GetType(object);
        if (!FPDFPage_RemoveObject(page, object))
            continue;

        // RemoveObject detaches without freeing; the page no longer owns it.
        FPDFPageObj_Destroy(object);

        ++result.objectsRemoved;
        if (type == FPDF_PAGEOBJ_TEXT)
            ++result.textObjectsRemoved;
        else if (type == FPDF_PAGEOBJ_IMAGE)
            ++result.imageObjectsRemoved;
    }

    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(raster.width(),
                                             raster.height(),
                                             FPDFBitmap_BGRA,
                                             raster.bits(),
                                             static_cast<int>(raster.bytesPerLine()));
    if (!bitmap) {
        FPDF_ClosePage(page);
        return result;
    }

    FPDF_PAGEOBJECT imageObject = FPDFPageObj_NewImageObj(doc);
    if (!imageObject) {
        FPDFBitmap_Destroy(bitmap);
        FPDF_ClosePage(page);
        return result;
    }

    FPDFImageObj_SetBitmap(&page, 1, imageObject, bitmap);

    // Image objects are drawn into the unit square, so the matrix is simply the
    // page size. Placed at the origin, covering the page exactly.
    FS_MATRIX matrix { float(pointSize.width()), 0.0f,
                       0.0f, float(pointSize.height()),
                       0.0f, 0.0f };
    FPDFPageObj_SetMatrix(imageObject, &matrix);

    FPDFPage_InsertObject(page, imageObject);
    FPDFPage_GenerateContent(page);

    // Only after GenerateContent: the bitmap's pixels have to stay valid until
    // PDFium has encoded them into the page's content stream.
    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);

    m_modified = true;
    result.ok = true;

    qCInfo(lcDoc) << "redacted page" << pageIndex
                  << "-- flattened, destroying" << result.textObjectsRemoved
                  << "text and" << result.imageObjectsRemoved << "image objects";
    return result;
#else
    return result;
#endif
}

// ---------------------------------------------------------------------------
// Compression
// ---------------------------------------------------------------------------

PdfDocument::CompressionReport PdfDocument::downsampleImages(int targetDpi)
{
    CompressionReport report;
    if (!m_valid)
        return report;

#ifdef LUMEN_HAS_PDFIUM
    QMutexLocker locker(&m_mutex);
    if (!m_handle)
        return report;

    releaseTextCache();

    const int dpi = qBound(72, targetDpi, 600);
    auto doc = static_cast<FPDF_DOCUMENT>(m_handle);

    for (int pageIndex = 0; pageIndex < m_pages.size(); ++pageIndex) {
        FPDF_PAGE page = FPDF_LoadPage(doc, pageIndex);
        if (!page)
            continue;

        bool pageChanged = false;
        const int objectCount = FPDFPage_CountObjects(page);

        for (int i = 0; i < objectCount; ++i) {
            FPDF_PAGEOBJECT object = FPDFPage_GetObject(page, i);
            if (!object || FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_IMAGE)
                continue;

            ++report.imagesExamined;

            // The matrix tells us how large the image is actually drawn. An
            // image is only oversized relative to its rendered size -- a
            // 4000px image is fine if it fills the page and wasteful if it is
            // a 20pt logo.
            FS_MATRIX matrix {};
            if (!FPDFPageObj_GetMatrix(object, &matrix))
                continue;

            const double drawnWidthPoints = qAbs(double(matrix.a));
            const double drawnHeightPoints = qAbs(double(matrix.d));
            if (drawnWidthPoints < 1.0 || drawnHeightPoints < 1.0)
                continue;

            FPDF_BITMAP bitmap = FPDFImageObj_GetBitmap(object);
            if (!bitmap)
                continue;

            const int srcWidth = FPDFBitmap_GetWidth(bitmap);
            const int srcHeight = FPDFBitmap_GetHeight(bitmap);
            const int stride = FPDFBitmap_GetStride(bitmap);
            const int format = FPDFBitmap_GetFormat(bitmap);
            void *buffer = FPDFBitmap_GetBuffer(bitmap);

            if (srcWidth <= 0 || srcHeight <= 0 || !buffer) {
                FPDFBitmap_Destroy(bitmap);
                continue;
            }

            report.pixelsBefore += qint64(srcWidth) * srcHeight;

            const int maxWidth = qMax(1, qRound(drawnWidthPoints * dpi / 72.0));
            const int maxHeight = qMax(1, qRound(drawnHeightPoints * dpi / 72.0));

            if (srcWidth <= maxWidth && srcHeight <= maxHeight) {
                // Already within budget. Re-encoding would cost quality and
                // gain nothing.
                report.pixelsAfter += qint64(srcWidth) * srcHeight;
                FPDFBitmap_Destroy(bitmap);
                continue;
            }

            // PDFium's bitmap formats map onto Qt's directly for the two cases
            // that actually occur; anything else is left alone rather than
            // guessed at.
            QImage::Format qtFormat = QImage::Format_Invalid;
            if (format == FPDFBitmap_BGRA)
                qtFormat = QImage::Format_ARGB32_Premultiplied;
            else if (format == FPDFBitmap_BGR)
                qtFormat = QImage::Format_RGB888;   // handled below

            if (qtFormat == QImage::Format_Invalid) {
                report.pixelsAfter += qint64(srcWidth) * srcHeight;
                FPDFBitmap_Destroy(bitmap);
                continue;
            }

            QImage source;
            if (format == FPDFBitmap_BGRA) {
                source = QImage(static_cast<uchar *>(buffer), srcWidth, srcHeight,
                                stride, QImage::Format_ARGB32_Premultiplied).copy();
            } else {
                // BGR, three bytes per pixel: Qt's RGB888 is the other order.
                source = QImage(static_cast<uchar *>(buffer), srcWidth, srcHeight,
                                stride, QImage::Format_BGR888).copy()
                             .convertToFormat(QImage::Format_ARGB32_Premultiplied);
            }

            FPDFBitmap_Destroy(bitmap);

            if (source.isNull()) {
                report.pixelsAfter += qint64(srcWidth) * srcHeight;
                continue;
            }

            const QImage scaled = source.scaled(maxWidth, maxHeight,
                                                Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation);
            if (scaled.isNull()) {
                report.pixelsAfter += qint64(srcWidth) * srcHeight;
                continue;
            }

            FPDF_BITMAP replacement = FPDFBitmap_CreateEx(
                scaled.width(), scaled.height(), FPDFBitmap_BGRA,
                const_cast<uchar *>(scaled.bits()),
                static_cast<int>(scaled.bytesPerLine()));

            if (!replacement) {
                report.pixelsAfter += qint64(srcWidth) * srcHeight;
                continue;
            }

            if (FPDFImageObj_SetBitmap(&page, 1, object, replacement)) {
                ++report.imagesDownsampled;
                report.pixelsAfter += qint64(scaled.width()) * scaled.height();
                pageChanged = true;
            } else {
                report.pixelsAfter += qint64(srcWidth) * srcHeight;
            }

            FPDFBitmap_Destroy(replacement);
        }

        if (pageChanged)
            FPDFPage_GenerateContent(page);

        FPDF_ClosePage(page);
    }

    report.ok = true;
    if (report.imagesDownsampled > 0)
        m_modified = true;

    qCInfo(lcDoc) << "downsampled" << report.imagesDownsampled << "of"
                  << report.imagesExamined << "images at" << dpi << "dpi;"
                  << report.pixelsBefore << "->" << report.pixelsAfter << "pixels";
    return report;
#else
    Q_UNUSED(targetDpi)
    return report;
#endif
}

bool PdfDocument::exportPageImage(int pageIndex,
                                  const QString &filePath,
                                  int dpi,
                                  int quality) const
{
    if (!m_valid || pageIndex < 0 || pageIndex >= m_pages.size() || filePath.isEmpty())
        return false;

    const QSizeF points = m_pages.at(pageIndex).sizePoints;
    if (points.width() <= 0 || points.height() <= 0)
        return false;

    // PDF user space is 72 units to the inch, so dpi/72 is the scale factor.
    const double scale = qBound(24, dpi, 1200) / 72.0;
    const QSize pixels(qMax(1, qRound(points.width() * scale)),
                       qMax(1, qRound(points.height() * scale)));

    // Guard against a page geometry plus a high DPI asking for a bitmap that
    // cannot be allocated. 256 megapixels is far past any real use.
    if (qint64(pixels.width()) * pixels.height() > 256LL * 1024 * 1024)
        return false;

    const QImage image = renderPage(pageIndex, pixels);
    if (image.isNull())
        return false;

    // JPEG has no alpha; the render is opaque anyway, but converting keeps the
    // written file from carrying a pointless alpha channel.
    const QImage out = filePath.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive)
                    || filePath.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive)
        ? image.convertToFormat(QImage::Format_RGB888)
        : image;

    return out.save(filePath, nullptr, quality);
}

QImage PdfDocument::renderPlaceholder(int index, const QSize &pixelSize) const
{
    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QColor(0, 0, 0, 40));

    const qreal margin = pixelSize.width() * 0.12;
    const qreal lineHeight = pixelSize.height() * 0.028;
    qreal y = margin;
    int line = 0;
    while (y < pixelSize.height() - margin) {
        const qreal width = (line % 7 == 6)
            ? (pixelSize.width() - 2 * margin) * 0.45
            : (pixelSize.width() - 2 * margin);
        painter.fillRect(QRectF(margin, y, width, lineHeight * 0.45),
                         QColor(0, 0, 0, line % 13 == 0 ? 55 : 24));
        y += lineHeight * 1.9;
        ++line;
    }

    painter.setPen(QColor(0, 0, 0, 90));
    QFont font = painter.font();
    font.setPixelSize(qMax(10, int(pixelSize.height() * 0.02)));
    painter.setFont(font);
    painter.drawText(QRectF(0, pixelSize.height() - margin, pixelSize.width(), margin),
                     Qt::AlignCenter,
                     QStringLiteral("Page %1 - stub renderer").arg(index + 1));

    return image;
}

} // namespace lumen

