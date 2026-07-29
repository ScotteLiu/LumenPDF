#include "core/PdfDocument.h"

#include "core/PdfEngine.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QPainter>

#ifdef LUMEN_HAS_PDFIUM
#include <fpdf_edit.h>
#include <fpdfview.h>
#endif

Q_LOGGING_CATEGORY(lcDoc, "lumen.document")

namespace lumen {

namespace {
// US Letter, used by the stub build so layout has something plausible to work
// with before PDFium is wired up.
constexpr QSizeF kFallbackPageSize { 612.0, 792.0 };
constexpr int kFallbackPageCount = 8;
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
    qCInfo(lcDoc) << "opened" << filePath << "pages:" << m_pages.size();
    return true;
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
