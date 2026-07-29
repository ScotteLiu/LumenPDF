#pragma once

#include <QImage>
#include <QMutex>
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

private:
    QImage renderPlaceholder(int index, const QSize &pixelSize) const;

    mutable QMutex m_mutex;
    void *m_handle = nullptr; // FPDF_DOCUMENT
    bool m_valid = false;
    QString m_filePath;
    QString m_lastError;
    QVector<PageInfo> m_pages;
};

} // namespace lumen
