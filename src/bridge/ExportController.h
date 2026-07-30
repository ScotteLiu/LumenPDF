#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QSharedPointer>
#include <QUrl>

namespace lumen {

class PdfDocument;

// Exporting to formats other than PDF: page images, and plain text.
//
// Runs off the GUI thread and reports progress, because this is the one
// operation in the app that is legitimately slow -- 300 dpi across 245 pages
// is real work, and a frozen window during it would be indefensible.
class ExportController : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int dpi READ dpi WRITE setDpi NOTIFY dpiChanged)

public:
    explicit ExportController(QObject *parent = nullptr);
    ~ExportController() override;

    void setDocument(const QSharedPointer<PdfDocument> &document);

    bool isBusy() const { return m_watcher != nullptr; }
    qreal progress() const { return m_progress; }

    int dpi() const { return m_dpi; }
    void setDpi(int dpi);

    // Writes "<name>-page-001.png" and so on into `directory`.
    // `lastPage` is inclusive; pass -1 for both bounds to mean every page.
    Q_INVOKABLE void exportImages(const QUrl &directory,
                                  const QString &format,
                                  int firstPage,
                                  int lastPage);

    // Extracted text of every page, separated by blank lines.
    Q_INVOKABLE bool exportText(const QUrl &file);

    Q_INVOKABLE void cancel();

signals:
    void busyChanged();
    void progressChanged();
    void dpiChanged();

    void finished(const QString &directory, int fileCount);
    void failed(const QString &reason);

private:
    void teardownWatcher();

    QSharedPointer<PdfDocument> m_document;

    // 200 dpi: sharp enough to read and to print acceptably, without producing
    // 20 MB per page the way 600 dpi does.
    int m_dpi = 200;

    qreal m_progress = 0.0;
    int m_pageTotal = 0;

    QFutureWatcher<bool> *m_watcher = nullptr;
    QString m_outputDirectory;
};

} // namespace lumen
