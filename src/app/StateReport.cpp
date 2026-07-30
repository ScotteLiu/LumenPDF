#include "app/StateReport.h"

#include "bridge/DocumentController.h"
#include "bridge/FormController.h"
#include "bridge/OutlineModel.h"
#include "bridge/SearchController.h"
#include "bridge/SelectionController.h"
#include "core/PdfDocument.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace lumen::report {

bool write(const QString &filePath, DocumentController *controller)
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
    QJsonArray pageTextLengths;
    QJsonArray pageWidths;
    for (int i = 0; i < controller->pageCount(); ++i) {
        pageTextLengths.append(controller->pageTextLength(i));
        pageWidths.append(controller->pageWidthPoints(i));
    }
    root["pageTextLengths"] = pageTextLengths;
    root["pageWidthsPoints"] = pageWidths;

    QFile out(filePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    out.close();
    return true;
}

} // namespace lumen::report
