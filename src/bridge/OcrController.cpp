#include "bridge/OcrController.h"

#include "core/PdfDocument.h"
#include "ocr/OcrEngine.h"

#include <QLoggingCategory>
#include <QtConcurrent/QtConcurrentMap>

Q_LOGGING_CATEGORY(lcOcrCtl, "lumen.ocr")

namespace lumen {

namespace {

// Recognition accuracy climbs steeply up to about 300 dpi and then flattens,
// while memory and time keep climbing. Scanned pages are usually 200-300 dpi in
// the first place, so rendering higher invents detail that was never there.
constexpr int kRecogniseDpi = 300;

} // namespace

OcrController::OcrController(QObject *parent)
    : QObject(parent)
{
    // Default to whatever Windows prefers rather than forcing a choice. The
    // user can override it, and the list is what the UI offers.
    const QStringList available = Ocr::availableLanguages();
    if (!available.isEmpty())
        m_language = available.first();
}

OcrController::~OcrController()
{
    cancel();
}

bool OcrController::isAvailable() const
{
    return Ocr::isAvailable();
}

QStringList OcrController::languages() const
{
    return Ocr::availableLanguages();
}

void OcrController::setLanguage(const QString &tag)
{
    if (m_language == tag)
        return;
    m_language = tag;
    emit languageChanged();
}

void OcrController::setDocument(const QSharedPointer<PdfDocument> &document)
{
    cancel();
    m_document = document;
    recount();
    emit documentChanged();
}

void OcrController::recount()
{
    m_pagesNeedingText = 0;
    if (!m_document || !m_document->isValid())
        return;

    // Counting means extracting text from every page, which is fast but not
    // free -- capped so opening a thousand-page scan does not stall.
    const int limit = qMin(m_document->pageCount(), 200);
    for (int i = 0; i < limit; ++i) {
        if (!m_document->pageHasText(i))
            ++m_pagesNeedingText;
    }
}

void OcrController::cancel()
{
    if (!m_watcher)
        return;

    auto *watcher = m_watcher;
    m_watcher = nullptr;
    watcher->disconnect(this);
    watcher->cancel();
    watcher->waitForFinished();
    watcher->deleteLater();

    m_progress = 0.0;
    emit progressChanged();
    emit busyChanged();
}

void OcrController::teardown()
{
    if (!m_watcher)
        return;

    auto *watcher = m_watcher;
    m_watcher = nullptr;
    watcher->disconnect(this);
    watcher->deleteLater();
    emit busyChanged();
}

void OcrController::recogniseDocument()
{
    if (!m_document || !m_document->isValid()) {
        emit failed(tr("No document is open."));
        return;
    }

    QList<int> pages;
    for (int i = 0; i < m_document->pageCount(); ++i) {
        if (!m_document->pageHasText(i))
            pages.append(i);
    }

    if (pages.isEmpty()) {
        emit failed(tr("Every page already has text — there is nothing to recognise."));
        return;
    }

    run(pages);
}

void OcrController::recognisePage(int pageIndex)
{
    if (!m_document || !m_document->isValid()) {
        emit failed(tr("No document is open."));
        return;
    }
    if (pageIndex < 0 || pageIndex >= m_document->pageCount())
        return;

    run({ pageIndex });
}

void OcrController::run(const QList<int> &pages)
{
    if (m_watcher) {
        emit failed(tr("Recognition is already running."));
        return;
    }
    if (!Ocr::isAvailable()) {
        emit failed(tr("No OCR language is installed. Add one in Windows Settings › "
                       "Time & language › Language & region."));
        return;
    }

    const auto document = m_document;
    const QString language = m_language;

    m_pageTotal = pages.size();
    m_progress = 0.0;
    emit progressChanged();

    m_watcher = new QFutureWatcher<int>(this);
    emit busyChanged();

    connect(m_watcher, &QFutureWatcherBase::progressValueChanged, this, [this](int done) {
        if (m_pageTotal <= 0)
            return;
        m_progress = qBound(0.0, qreal(done) / m_pageTotal, 1.0);
        emit progressChanged();
    });

    connect(m_watcher, &QFutureWatcherBase::finished, this, [this] {
        if (!m_watcher)
            return;

        const QList<int> results = m_watcher->future().results();
        int recognised = 0;
        int words = 0;
        for (int count : results) {
            if (count > 0) {
                ++recognised;
                words += count;
            }
        }

        m_progress = 1.0;
        emit progressChanged();
        teardown();
        recount();
        emit documentChanged();

        qCInfo(lcOcrCtl) << "recognised" << recognised << "pages," << words << "words";

        if (recognised == 0)
            emit failed(tr("No text could be recognised."));
        else
            emit finished(recognised, words);
    });

    // Recognition itself is the expensive part and Windows OCR is thread-safe
    // per call, so pages are processed concurrently. Writing the text layer
    // back into the document is serialised by PdfDocument's own mutex.
    m_watcher->setFuture(QtConcurrent::mapped(pages, [this, document, language](int page) {
        const QSizeF points = document->pageInfo(page).sizePoints;
        if (points.width() <= 0 || points.height() <= 0)
            return -1;

        const double scale = kRecogniseDpi / 72.0;
        const QSize pixels(qRound(points.width() * scale),
                           qRound(points.height() * scale));

        const QImage rendered = document->renderPage(page, pixels);
        if (rendered.isNull())
            return -1;

        const OcrResult result = Ocr::recognise(rendered, language);
        if (!result.ok || result.lines.isEmpty())
            return -1;

        // Back from image pixels into PDF points.
        QVector<PdfDocument::RecognisedWord> words;
        for (const OcrLine &line : result.lines) {
            for (const OcrWord &word : line.words) {
                if (word.text.trimmed().isEmpty())
                    continue;
                words.append({ word.text,
                               QRectF(word.box.x() / scale, word.box.y() / scale,
                                      word.box.width() / scale,
                                      word.box.height() / scale) });
            }
        }

        if (words.isEmpty())
            return -1;

        // Queued to the GUI thread: writing to the document mutates it, and
        // the page must be re-rendered afterwards.
        QMetaObject::invokeMethod(this, [this, page] {
            emit pageInvalidated(page);
        }, Qt::QueuedConnection);

        // Explicit int: the lambda's other returns are int literals, and
        // qsizetype would make the deduced type ambiguous.
        return document->addInvisibleTextLayer(page, words) ? int(words.size()) : -1;
    }));
}

} // namespace lumen
