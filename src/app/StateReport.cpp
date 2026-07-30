#include "app/StateReport.h"

#include "bridge/DocumentController.h"
#include "bridge/FormController.h"
#include "bridge/OutlineModel.h"
#include "bridge/SearchController.h"
#include "bridge/SelectionController.h"
#include "app/Timing.h"
#include "core/PdfDocument.h"
#include "platform/PlatformWindow.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace lumen::report {

bool write(const QString &filePath,
           DocumentController *controller,
           PlatformWindow *platform)
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
    for (int i = 0; i < reportedPages; ++i) {
        pageTextLengths.append(controller->pageTextLength(i));
        pageWidths.append(controller->pageWidthPoints(i));
    }
    root["reportedPages"] = reportedPages;
    root["pageTextLengths"] = pageTextLengths;
    root["pageWidthsPoints"] = pageWidths;

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
