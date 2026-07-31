#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QSharedPointer>
#include <QStringList>

namespace lumen {

class PdfDocument;

// Making a scanned document searchable.
//
// Pages are rendered, recognised, and given an invisible text layer. The pixels
// are never altered: what the user sees afterwards is the same scan, but with
// text they can select, search and copy.
//
// Runs off the GUI thread and reports progress, because recognising a hundred
// pages is minutes of work, not milliseconds.
class OcrController : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool available READ isAvailable CONSTANT)
    Q_PROPERTY(QStringList languages READ languages CONSTANT)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)

    // Pages with no extractable text -- what OCR would actually add something
    // to. Shown so the user knows whether it is worth running at all.
    Q_PROPERTY(int pagesNeedingText READ pagesNeedingText NOTIFY documentChanged)

public:
    explicit OcrController(QObject *parent = nullptr);
    ~OcrController() override;

    void setDocument(const QSharedPointer<PdfDocument> &document);

    bool isAvailable() const;
    QStringList languages() const;

    QString language() const { return m_language; }
    void setLanguage(const QString &tag);

    bool isBusy() const { return m_watcher != nullptr; }
    qreal progress() const { return m_progress; }
    int pagesNeedingText() const { return m_pagesNeedingText; }

    // Recognises every page that has no text layer already. Pages that already
    // carry text are skipped: re-recognising them would layer OCR guesses on
    // top of text that is already correct.
    Q_INVOKABLE void recogniseDocument();

    // Recognises one page regardless of whether it already has text.
    Q_INVOKABLE void recognisePage(int pageIndex);

    Q_INVOKABLE void cancel();

signals:
    void languageChanged();
    void busyChanged();
    void progressChanged();
    void documentChanged();

    void pageInvalidated(int pageIndex);
    void finished(int pagesRecognised, int wordsAdded);
    void failed(const QString &reason);

private:
    void run(const QList<int> &pages);
    void teardown();
    void recount();

    QSharedPointer<PdfDocument> m_document;

    QString m_language;
    qreal m_progress = 0.0;
    int m_pageTotal = 0;
    int m_pagesNeedingText = 0;

    // Each entry is the word count added to one page, or -1 for a failure.
    QFutureWatcher<int> *m_watcher = nullptr;
};

} // namespace lumen
