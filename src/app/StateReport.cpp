#include "app/StateReport.h"

#include "bridge/DocumentController.h"
#include "bridge/FormController.h"
#include "bridge/OutlineModel.h"
#include "bridge/SearchController.h"
#include "bridge/SelectionController.h"
#include "app/Settings.h"
#include "app/Timing.h"
#include "core/PdfDocument.h"
#include "platform/PlatformWindow.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QVariantMap>

namespace lumen::report {

bool write(const QString &filePath,
           DocumentController *controller,
           PlatformWindow *platform,
           Settings *settings)
{
    if (filePath.isEmpty() || !controller)
        return false;

    QJsonObject root;
    root["filePath"] = controller->filePath();
    root["title"] = controller->title();
    root["status"] = int(controller->status());
    root["pageCount"] = controller->pageCount();
    root["modified"] = controller->isModified();

    if (auto *outline = controller->outlineModel())
        root["outlineCount"] = outline->rowCount();

    if (auto *search = controller->search()) {
        root["searchQuery"] = search->query();
        root["searchHits"] = search->count();
    }

    if (auto *selection = controller->selection()) {
        const QString text = selection->text();
        root["selectionText"] = text;
        root["selectionLength"] = text.size();
        root["selectionFirstPage"] = selection->firstPage();
        root["selectionLastPage"] = selection->lastPage();
    }

    if (auto *forms = controller->forms())
        root["hasForms"] = forms->hasForms();

    // Per-page text length is the assertion that proves redaction worked: a
    // black box is not evidence, a page whose text length went to zero is.
    //
    // Capped, because extracting text is real work: on a 1000-page document,
    // reporting every page took about a second and showed up in the benchmark
    // as if the app were slow. Instrumentation that changes the number it is
    // measuring is worse than no instrumentation.
    constexpr int kMaxReportedPages = 50;
    const int reportedPages = qMin(controller->pageCount(), kMaxReportedPages);

    QJsonArray pageTextLengths;
    QJsonArray pageWidths;
    QJsonArray pageLinks;
    for (int i = 0; i < reportedPages; ++i) {
        pageTextLengths.append(controller->pageTextLength(i));
        pageWidths.append(controller->pageWidthPoints(i));
        pageLinks.append(controller->linkCount(i));
    }
    root["reportedPages"] = reportedPages;
    root["pageTextLengths"] = pageTextLengths;
    root["pageWidthsPoints"] = pageWidths;
    root["pageLinkCounts"] = pageLinks;

    root["pdfiumProbes"] = controller->pdfiumProbes();

    // Simulates what the hover handler does, N times, and reports how many of
    // those had to reach PDFium. "<page>,<x>,<y>,<repeats>".
    if (qEnvironmentVariableIsSet("LUMEN_HOVER")) {
        const QStringList parts = qEnvironmentVariable("LUMEN_HOVER").split(u',');
        if (parts.size() == 4) {
            const int page = parts.at(0).toInt();
            const QPointF point(parts.at(1).toDouble(), parts.at(2).toDouble());
            const int repeats = parts.at(3).toInt();

            controller->resetPdfiumProbes();
            for (int i = 0; i < repeats; ++i) {
                // Nudged by a sub-point each time so this cannot be mistaken
                // for a cache of one answer to one identical query.
                controller->linkAt(page, point + QPointF(i % 3 * 0.25, 0));
            }
            root["hoverRepeats"] = repeats;
            root["hoverProbes"] = controller->pdfiumProbes();
        }
    }

    // Probe a single link, so a test can assert on what a click would resolve
    // to without synthesising one. "<page>,<x>,<y>" in PDF points.
    if (qEnvironmentVariableIsSet("LUMEN_LINK_PROBE")) {
        const QStringList parts = qEnvironmentVariable("LUMEN_LINK_PROBE").split(u',');
        if (parts.size() == 3) {
            const QVariantMap link = controller->linkAt(
                parts.at(0).toInt(),
                QPointF(parts.at(1).toDouble(), parts.at(2).toDouble()));
            root["linkProbe"] = QJsonObject::fromVariantMap(link);
        }
    }

    if (settings) {
        QJsonObject prefs;
        prefs["theme"] = settings->theme();
        prefs["language"] = settings->language();
        prefs["zoomMode"] = settings->zoomMode();
        prefs["recentCount"] = int(settings->recentFiles().size());
        if (!controller->filePath().isEmpty())
            prefs["restoredPage"] = settings->positionFor(controller->filePath());
        root["settings"] = prefs;
    }

    root["uiLanguage"] = QLocale().name();

    // Startup timeline. Reported as absolute milliseconds from the first line
    // of main(), so the phases are directly comparable to each other.
    QJsonObject timings;
    for (const auto &milestone : Timing::instance().milestones())
        timings[milestone.first] = milestone.second;
    root["timingsMs"] = timings;

    if (platform) {
        root["memoryMb"] = qRound(platform->memoryMegabytes() * 10) / 10.0;

        if (!platform->benchmarkName().isEmpty()) {
            QJsonObject bench;
            bench["name"] = platform->benchmarkName();
            bench["fps"] = qRound(platform->benchmarkFps() * 10) / 10.0;
            bench["frames"] = platform->benchmarkFrames();
            root["benchmark"] = bench;
        }
    }

    QFile out(filePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    out.close();
    return true;
}

} // namespace lumen::report
