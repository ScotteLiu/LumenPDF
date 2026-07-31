#include "bridge/PrintController.h"

#include "core/PdfDocument.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QPageSize>
#include <QPainter>
#include <QPrinter>
#include <QPrinterInfo>
#include <QPromise>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

Q_LOGGING_CATEGORY(lcPrint, "lumen.print")

namespace lumen {

namespace {

// The same ceiling the on-screen render path uses. A page can declare itself
// 200000 inches square, and at 600 dpi that is not a printout, it is an
// allocation failure.
constexpr qint64 kMaxPixels = 40LL * 1000 * 1000;

QSize rasterSize(const QSizeF &pointSize, int dpi)
{
    const double scale = static_cast<double>(dpi) / 72.0;
    double w = std::max(1.0, pointSize.width() * scale);
    double h = std::max(1.0, pointSize.height() * scale);

    const double pixels = w * h;
    if (pixels > static_cast<double>(kMaxPixels)) {
        const double shrink = std::sqrt(static_cast<double>(kMaxPixels) / pixels);
        w = std::max(1.0, w * shrink);
        h = std::max(1.0, h * shrink);
    }
    return QSize(static_cast<int>(w), static_cast<int>(h));
}

} // namespace

PrintController::PrintController(QObject *parent)
    : QObject(parent)
{
    m_printer = QPrinterInfo::defaultPrinterName();
}

PrintController::~PrintController()
{
    if (m_watcher) {
        m_watcher->waitForFinished();
    }
}

void PrintController::setDocument(const QSharedPointer<PdfDocument> &document)
{
    m_document = document;
    emit documentChanged();
}

QStringList PrintController::printers() const
{
    return QPrinterInfo::availablePrinterNames();
}

void PrintController::setPrinter(const QString &name)
{
    if (m_printer == name) {
        return;
    }
    m_printer = name;
    emit printerChanged();
}

int PrintController::pageCount() const
{
    return m_document && m_document->isValid() ? m_document->pageCount() : 0;
}

void PrintController::refreshPrinters()
{
    emit printersChanged();
    if (!printers().contains(m_printer)) {
        setPrinter(QPrinterInfo::defaultPrinterName());
    }
}

void PrintController::print(int firstPage, int lastPage, int copies,
                            bool greyscale, bool fitToPage)
{
    run(m_printer, QString(), firstPage, lastPage, copies, greyscale, fitToPage);
}

void PrintController::printToFile(const QUrl &url, int firstPage, int lastPage,
                                  bool greyscale, bool fitToPage)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) {
        emit failed(tr("No output file was chosen."));
        return;
    }
    run(QString(), path, firstPage, lastPage, 1, greyscale, fitToPage);
}

void PrintController::run(const QString &printerName, const QString &filePath,
                          int firstPage, int lastPage, int copies,
                          bool greyscale, bool fitToPage)
{
    if (m_watcher) {
        emit failed(tr("A print job is already running."));
        return;
    }
    if (!m_document || !m_document->isValid()) {
        emit failed(tr("No document is open."));
        return;
    }

    const int total = m_document->pageCount();
    const int from = firstPage < 0 ? 0 : std::clamp(firstPage, 0, total - 1);
    const int to = lastPage < 0 ? total - 1 : std::clamp(lastPage, from, total - 1);
    const int sheets = to - from + 1;
    const int copyCount = std::clamp(copies, 1, 99);

    if (filePath.isEmpty() && !QPrinterInfo::availablePrinterNames().contains(printerName)) {
        emit failed(printerName.isEmpty() ? tr("No printer is selected.")
                                          : tr("The printer “%1” is not available.").arg(printerName));
        return;
    }

    // The document is captured by shared pointer, so a job outlives the user
    // closing the file. PdfDocument::renderPage is thread-safe by contract.
    auto document = m_document;

    auto job = [document, printerName, filePath, from, to, copyCount,
                greyscale, fitToPage](QPromise<int> &promise) -> void {
        QPrinter printer(QPrinter::HighResolution);
        if (filePath.isEmpty()) {
            printer.setPrinterName(printerName);
        } else {
            printer.setOutputFormat(QPrinter::PdfFormat);
            printer.setOutputFileName(filePath);

            // Printing to a file has no paper, so the sheet takes the size of
            // the document's own first page. Otherwise "save as PDF" quietly
            // reflows a US Letter document onto A4 -- correct for a printer,
            // surprising for a file.
            const QSizeF points = document->pageInfo(from).sizePoints;
            if (points.width() > 1 && points.height() > 1) {
                printer.setPageSize(QPageSize(points, QPageSize::Point, QString(),
                                              QPageSize::ExactMatch));
            }
        }
        printer.setColorMode(greyscale ? QPrinter::GrayScale : QPrinter::Color);
        printer.setCopyCount(copyCount);
        printer.setFullPage(true);

        const int dpi = printer.resolution();
        const int sheets = to - from + 1;
        promise.setProgressRange(0, sheets);

        QPainter painter;
        if (!painter.begin(&printer)) {
            promise.addResult(-1);
            return;
        }

        int printed = 0;
        for (int page = from; page <= to; ++page) {
            if (promise.isCanceled()) {
                break;
            }
            if (printed > 0 && !printer.newPage()) {
                break;
            }

            const PdfDocument::PageInfo info = document->pageInfo(page);
            const QImage image = document->renderPage(page, rasterSize(info.sizePoints, dpi));
            ++printed;
            promise.setProgressValue(printed);

            if (image.isNull()) {
                continue; // A page that will not render still consumes a sheet.
            }

            // The sheet, in device pixels.
            const QRectF sheet(0, 0, printer.width(), printer.height());

            // Turn the page sideways when its aspect disagrees with the paper's.
            // Doing it here rather than by changing the printer's orientation
            // mid-job avoids driver-specific behaviour, and it is what makes a
            // landscape page in a portrait document come out readable.
            const bool pageIsWide = image.width() > image.height();
            const bool sheetIsWide = sheet.width() > sheet.height();
            const bool rotate = fitToPage && pageIsWide != sheetIsWide;

            QSizeF target(image.size());
            if (rotate) {
                target.transpose();
            }

            double scale = 1.0;
            if (fitToPage) {
                scale = std::min(sheet.width() / target.width(),
                                 sheet.height() / target.height());
            } else {
                // Actual size: one PDF point is 1/72 inch on paper. Shrink only
                // if the page genuinely does not fit, rather than silently
                // cropping it.
                scale = std::min({1.0,
                                  sheet.width() / target.width(),
                                  sheet.height() / target.height()});
            }

            const QSizeF drawn = target * scale;
            const QRectF where((sheet.width() - drawn.width()) / 2.0,
                               (sheet.height() - drawn.height()) / 2.0,
                               drawn.width(), drawn.height());

            painter.save();
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            if (rotate) {
                painter.translate(where.center());
                painter.rotate(90);
                painter.translate(-where.center());
                const QRectF unrotated(where.center().x() - where.height() / 2.0,
                                       where.center().y() - where.width() / 2.0,
                                       where.height(), where.width());
                painter.drawImage(unrotated, image);
            } else {
                painter.drawImage(where, image);
            }
            painter.restore();
        }

        painter.end();
        promise.addResult(printed);
    };

    m_watcher = new QFutureWatcher<int>(this);

    connect(m_watcher, &QFutureWatcher<int>::progressValueChanged, this, [this, sheets](int value) {
        m_progress = sheets > 0 ? static_cast<qreal>(value) / sheets : 0.0;
        emit progressChanged();
    });

    connect(m_watcher, &QFutureWatcher<int>::finished, this, [this, filePath] {
        const int printed = m_watcher->future().resultCount() > 0 ? m_watcher->result() : -1;
        m_watcher->deleteLater();
        m_watcher = nullptr;
        m_progress = 0.0;
        emit progressChanged();
        emit busyChanged();

        if (printed < 0) {
            qCWarning(lcPrint) << "could not start the print job";
            emit failed(filePath.isEmpty()
                            ? tr("The printer could not be opened.")
                            : tr("The output file could not be written."));
        } else {
            qCInfo(lcPrint) << "printed" << printed << "pages";
            emit finished(printed);
        }
    });

    m_watcher->setFuture(QtConcurrent::run(job));
    emit busyChanged();
}

} // namespace lumen
