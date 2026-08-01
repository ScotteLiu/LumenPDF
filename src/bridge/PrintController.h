#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QSharedPointer>
#include <QStringList>

namespace lumen {

class PdfDocument;

// Printing.
//
// The print sheet is built in QML like every other surface in the app rather
// than using QPrintDialog, which would drag in QtWidgets and put a native
// dialog in the middle of an interface that is otherwise entirely ours.
//
// Pages are rasterised at the printer's own resolution and drawn one per sheet.
// Rendering through PDFium rather than handing the printer a PDF means what
// comes out matches what was on screen -- including annotations, form values
// and OCR layers -- instead of depending on the printer's own PDF interpreter.
class PrintController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QStringList printers READ printers NOTIFY printersChanged)
    Q_PROPERTY(QString printer READ printer WRITE setPrinter NOTIFY printerChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY documentChanged)

public:
    explicit PrintController(QObject *parent = nullptr);
    ~PrintController() override;

    void setDocument(const QSharedPointer<PdfDocument> &document);

    QStringList printers() const;

    // Not inline: reading this resolves the system default on first call, and
    // that first call is what loads the printing stack.
    QString printer() const;
    void setPrinter(const QString &name);

    bool isBusy() const { return m_watcher != nullptr; }
    qreal progress() const { return m_progress; }
    int pageCount() const;

    // `firstPage`/`lastPage` are zero-based and inclusive; -1 means every page.
    Q_INVOKABLE void print(int firstPage, int lastPage, int copies,
                           bool greyscale, bool fitToPage);

    // Writes the same output to a PDF instead of a printer, which is how
    // "Microsoft Print to PDF" behaves without needing that driver installed.
    Q_INVOKABLE void printToFile(const QUrl &url, int firstPage, int lastPage,
                                 bool greyscale, bool fitToPage);

    Q_INVOKABLE void refreshPrinters();

signals:
    void printersChanged();
    void printerChanged();
    void busyChanged();
    void progressChanged();
    void documentChanged();

    void finished(int pagesPrinted);
    void failed(const QString &reason);

private:
    void run(const QString &printerName, const QString &filePath,
             int firstPage, int lastPage, int copies,
             bool greyscale, bool fitToPage);

    // Mutable so the getter can stay const while filling this in lazily.
    void resolveDefaultPrinter() const;

    QSharedPointer<PdfDocument> m_document;
    mutable QString m_printer;
    mutable bool m_printerResolved = false;
    qreal m_progress = 0.0;

    QFutureWatcher<int> *m_watcher = nullptr;
};

} // namespace lumen
