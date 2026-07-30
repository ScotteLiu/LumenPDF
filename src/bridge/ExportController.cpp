#include "bridge/ExportController.h"

#include "core/PdfDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QTextStream>
#include <QtConcurrent/QtConcurrentMap>

Q_LOGGING_CATEGORY(lcExport, "lumen.export")

namespace lumen {

ExportController::ExportController(QObject *parent)
    : QObject(parent)
{
}

ExportController::~ExportController()
{
    cancel();
}

void ExportController::setDocument(const QSharedPointer<PdfDocument> &document)
{
    cancel();
    m_document = document;
}

void ExportController::setDpi(int dpi)
{
    const int clamped = qBound(72, dpi, 600);
    if (m_dpi == clamped)
        return;
    m_dpi = clamped;
    emit dpiChanged();
}

void ExportController::teardownWatcher()
{
    if (!m_watcher)
        return;

    auto *watcher = m_watcher;
    m_watcher = nullptr;
    watcher->disconnect(this);
    watcher->deleteLater();
    emit busyChanged();
}

void ExportController::cancel()
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

void ExportController::exportImages(const QUrl &directory,
                                    const QString &format,
                                    int firstPage,
                                    int lastPage)
{
    if (m_watcher) {
        emit failed(tr("An export is already running."));
        return;
    }
    if (!m_document || !m_document->isValid()) {
        emit failed(tr("No document is open."));
        return;
    }

    const QString dirPath = directory.isLocalFile() ? directory.toLocalFile()
                                                    : directory.toString();
    QDir dir(dirPath);
    if (dirPath.isEmpty() || !dir.exists()) {
        emit failed(tr("That folder does not exist."));
        return;
    }

    const int total = m_document->pageCount();
    const int from = (firstPage < 0) ? 0 : qBound(0, firstPage, total - 1);
    const int to = (lastPage < 0) ? total - 1 : qBound(from, lastPage, total - 1);

    const QString suffix = format.isEmpty() ? QStringLiteral("png") : format.toLower();
    const int quality = (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg"))
        ? 92 : -1;

    // Zero-padded to the width of the largest page number, so the files sort
    // correctly in every file manager.
    const int digits = QString::number(total).size();
    const QString stem = QFileInfo(m_document->filePath()).completeBaseName();

    QList<int> pages;
    pages.reserve(to - from + 1);
    for (int i = from; i <= to; ++i)
        pages.append(i);

    const auto document = m_document;
    const int dpi = m_dpi;

    m_pageTotal = pages.size();
    m_progress = 0.0;
    m_outputDirectory = dirPath;
    emit progressChanged();

    m_watcher = new QFutureWatcher<bool>(this);
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

        const QList<bool> results = m_watcher->future().results();
        const int written = int(std::count(results.cbegin(), results.cend(), true));
        const int failedCount = results.size() - written;

        m_progress = 1.0;
        emit progressChanged();

        const QString dir = m_outputDirectory;
        teardownWatcher();

        qCInfo(lcExport) << "wrote" << written << "images to" << dir
                         << "(" << failedCount << "failed )";

        if (written == 0)
            emit failed(tr("No pages could be exported."));
        else
            emit finished(dir, written);
    });

    m_watcher->setFuture(QtConcurrent::mapped(
        pages,
        [document, dirPath, stem, suffix, digits, dpi, quality](int page) {
            const QString name = QStringLiteral("%1-page-%2.%3")
                .arg(stem)
                .arg(page + 1, digits, 10, QLatin1Char('0'))
                .arg(suffix);
            return document->exportPageImage(page,
                                             QDir(dirPath).filePath(name),
                                             dpi,
                                             quality);
        }));
}

bool ExportController::exportText(const QUrl &file)
{
    if (!m_document || !m_document->isValid()) {
        emit failed(tr("No document is open."));
        return false;
    }

    QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (path.isEmpty()) {
        emit failed(tr("No destination given."));
        return false;
    }
    if (!path.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive))
        path += QStringLiteral(".txt");

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        emit failed(tr("Could not write %1").arg(QFileInfo(path).fileName()));
        return false;
    }

    // Text extraction is fast enough to stay on this thread even for a few
    // hundred pages, and streaming keeps peak memory flat regardless of size.
    QTextStream stream(&out);
    stream.setEncoding(QStringConverter::Utf8);

    const int pages = m_document->pageCount();
    for (int i = 0; i < pages; ++i) {
        if (i > 0)
            stream << "\n\n";
        stream << m_document->pageText(i);
    }

    stream.flush();
    out.close();

    qCInfo(lcExport) << "wrote text of" << pages << "pages to" << path;
    emit finished(QFileInfo(path).absolutePath(), 1);
    return true;
}

} // namespace lumen
