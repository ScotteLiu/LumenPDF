#include "core/PdfDocument.h"

#include "core/PdfEngine.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QPainter>
#include <QSet>

#ifdef LUMEN_HAS_PDFIUM
#include <fpdf_doc.h>
#include <fpdf_edit.h>
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
    return text;
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

    unsigned long flags = 0;
    if (matchCase)
        flags |= FPDF_MATCHCASE;
    if (wholeWord)
        flags |= FPDF_MATCHWHOLEWORD;

    // FPDF_WIDESTRING is UTF-16LE; QString's internal buffer already is.
    const auto needle = reinterpret_cast<FPDF_WIDESTRING>(query.utf16());

    FPDF_SCHHANDLE search = FPDFText_FindStart(textPage, needle, flags, 0);
    if (!search) {
        FPDFText_ClosePage(textPage);
        FPDF_ClosePage(page);
        return hits;
    }

    const double pageHeight = m_pages.at(index).sizePoints.height();

    while (FPDFText_FindNext(search)) {
        SearchHit hit;
        hit.pageIndex = index;
        hit.charIndex = FPDFText_GetSchResultIndex(search);
        hit.charCount = FPDFText_GetSchCount(search);

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
            const int from = qMax(0, hit.charIndex - kSnippetContext);
            const int to = qMin(pageBody.size(), hit.charIndex + hit.charCount + kSnippetContext);
            QString snippet = pageBody.mid(from, to - from);

            hit.snippetMatchStart = hit.charIndex - from;
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

    FPDFText_FindClose(search);
    FPDFText_ClosePage(textPage);
    FPDF_ClosePage(page);
#else
    Q_UNUSED(matchCase)
    Q_UNUSED(wholeWord)
#endif

    return hits;
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

