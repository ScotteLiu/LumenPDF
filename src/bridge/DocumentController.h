#pragma once

// Full definitions, not forward declarations: moc needs complete types for any
// class used as a Q_PROPERTY pointer.
#include "bridge/AnnotationController.h"
#include "bridge/OutlineModel.h"
#include "bridge/SearchController.h"
#include "bridge/ExportController.h"
#include "bridge/FormController.h"
#include "bridge/PageOperations.h"
#include "bridge/RedactionController.h"
#include "bridge/SelectionController.h"

#include <QAbstractItemModel>
#include <QObject>
#include <QPointF>
#include <QSharedPointer>
#include <QUrl>
#include <QVariantMap>

namespace lumen {

class PdfDocument;
class PageImageProvider;
class PageListModel;

// The single object QML talks to. Opening a document is asynchronous so a
// large file never freezes the window; QML reacts to the status change.
class DocumentController : public QObject {
    Q_OBJECT

    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString title READ title NOTIFY documentChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY documentChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY documentChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)
    // Typed as QAbstractItemModel* rather than QObject* so QML views accept it
    // directly as a `model`.
    Q_PROPERTY(QAbstractItemModel *pageModel READ pageModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *outlineModel READ outlineModelAsItemModel CONSTANT)
    Q_PROPERTY(lumen::OutlineModel *outline READ outlineModel CONSTANT)
    Q_PROPERTY(lumen::SearchController *search READ search CONSTANT)
    Q_PROPERTY(lumen::SelectionController *selection READ selection CONSTANT)
    Q_PROPERTY(lumen::AnnotationController *annotate READ annotate CONSTANT)
    Q_PROPERTY(lumen::PageOperations *pages READ pages CONSTANT)
    Q_PROPERTY(lumen::ExportController *exporter READ exporter CONSTANT)
    Q_PROPERTY(lumen::RedactionController *redact READ redact CONSTANT)
    Q_PROPERTY(lumen::FormController *forms READ forms CONSTANT)

    Q_PROPERTY(bool modified READ isModified NOTIFY modifiedChanged)

    // Bumped whenever rendered page content changes. Page images append it to
    // their source URL, which is what makes QML re-fetch them -- an Image
    // whose source string is unchanged will never reload, however stale the
    // pixels behind it are.
    Q_PROPERTY(int renderGeneration READ renderGeneration NOTIFY renderGenerationChanged)

public:
    // Unscoped on purpose: QML reads these as DocumentStatus.Ready etc.
    enum Status {
        Empty,
        Loading,
        Ready,
        Error,
    };
    Q_ENUM(Status)

    explicit DocumentController(QObject *parent = nullptr);
    ~DocumentController() override;

    // Ownership stays with the QML engine; the controller only keeps a pointer
    // so it can hand the provider each newly opened document.
    void setImageProvider(PageImageProvider *provider);

    Status status() const { return m_status; }
    QString title() const { return m_title; }
    QString filePath() const { return m_filePath; }
    int pageCount() const;
    QString errorString() const { return m_errorString; }
    QAbstractItemModel *pageModel() const;
    OutlineModel *outlineModel() const { return m_outlineModel; }
    QAbstractItemModel *outlineModelAsItemModel() const;
    SearchController *search() const { return m_search; }
    SelectionController *selection() const { return m_selection; }
    AnnotationController *annotate() const { return m_annotate; }
    PageOperations *pages() const { return m_pageOps; }
    ExportController *exporter() const { return m_exporter; }
    RedactionController *redact() const { return m_redact; }
    FormController *forms() const { return m_forms; }

    bool isModified() const;
    int renderGeneration() const { return m_renderGeneration; }

    // Saves to `url`. Passing the currently open file is handled correctly:
    // the write goes to a temporary alongside it and is swapped in, because
    // PDFium still has the original mapped while it writes.
    Q_INVOKABLE bool saveAs(const QUrl &url);
    Q_INVOKABLE bool save();

    // Downsamples oversized images and writes a compacted copy.
    //
    // Always writes to a new file rather than in place: this is lossy, and
    // silently degrading the only copy of someone's document is not acceptable.
    Q_INVOKABLE bool compressTo(const QUrl &url, int targetDpi);

    Q_INVOKABLE void open(const QUrl &url);
    Q_INVOKABLE void close();

    // Page width in PDF points. Returns 0 for an out-of-range index.
    Q_INVOKABLE double pageWidthPoints(int index) const;

    // Length of a page's extracted text. Exists so tests can assert that
    // redaction destroyed content, which a rendered black box cannot prove.
    Q_INVOKABLE int pageTextLength(int index) const;

    // The editable text run under a point, as a map QML can read directly:
    // { valid, objectIndex, text, x, y, width, height, fontSize, longRun }.
    // Coordinates are PDF points with a top-left origin.
    Q_INVOKABLE QVariantMap textRunAt(int pageIndex, const QPointF &point) const;

signals:
    void statusChanged();
    void documentChanged();
    void modifiedChanged();
    void renderGenerationChanged();
    void saved(const QString &filePath);
    void saveFailed(const QString &reason);

    // originalBytes/newBytes let the UI report the saving honestly, including
    // when there was none.
    void compressed(const QString &filePath, qint64 originalBytes, qint64 newBytes,
                    int imagesDownsampled);

private:
    void setStatus(Status status, const QString &error = {});
    void adoptDocument(const QSharedPointer<PdfDocument> &document);

    Status m_status = Status::Empty;
    QString m_title;
    QString m_filePath;
    QString m_errorString;

    QSharedPointer<PdfDocument> m_document;
    PageImageProvider *m_provider = nullptr;
    PageListModel *m_pageModel = nullptr;
    OutlineModel *m_outlineModel = nullptr;
    SearchController *m_search = nullptr;
    SelectionController *m_selection = nullptr;
    AnnotationController *m_annotate = nullptr;
    PageOperations *m_pageOps = nullptr;
    ExportController *m_exporter = nullptr;
    RedactionController *m_redact = nullptr;
    FormController *m_forms = nullptr;

    int m_renderGeneration = 0;
};

} // namespace lumen
