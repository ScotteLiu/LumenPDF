#include "app/TestFixtures.h"

#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QLoggingCategory>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

Q_LOGGING_CATEGORY(lcFixtures, "lumen.fixtures")

namespace lumen::fixtures {

namespace {

// 72 dpi makes one device unit equal one PDF point, so the coordinates written
// here are the same numbers the tests use when they click and select.
constexpr int kResolution = 72;

QPdfWriter *makeWriter(const QString &filePath)
{
    auto *writer = new QPdfWriter(filePath);
    writer->setPageSize(QPageSize(QPageSize::A4));
    writer->setResolution(kResolution);
    writer->setPageMargins(QMarginsF(0, 0, 0, 0));
    writer->setCreator(QStringLiteral("LumenPDF test fixture generator"));
    return writer;
}

// The first installed font that can actually draw Han characters. Picking one
// explicitly matters: Qt's default UI font on Windows has no CJK coverage, so
// the text would silently come out as boxes and the fixture would be useless.
QString findCjkFont()
{
    static const QStringList candidates {
        QStringLiteral("Microsoft JhengHei"),   // Traditional Chinese
        QStringLiteral("Microsoft YaHei"),      // Simplified Chinese
        QStringLiteral("PMingLiU"),
        QStringLiteral("SimSun"),
        QStringLiteral("MS Gothic"),
        QStringLiteral("Yu Gothic"),
        QStringLiteral("Noto Sans CJK TC"),
        QStringLiteral("Noto Sans CJK SC"),
    };

    const QStringList installed = QFontDatabase::families();
    for (const QString &candidate : candidates) {
        if (installed.contains(candidate, Qt::CaseInsensitive))
            return candidate;
    }
    return {};
}

} // namespace

bool writeLatinSample(const QString &filePath)
{
    QScopedPointer<QPdfWriter> writer(makeWriter(filePath));
    QPainter painter(writer.data());
    if (!painter.isActive())
        return false;

    QFont body(QStringLiteral("Arial"), 11);
    QFont heading(QStringLiteral("Arial"), 20, QFont::Bold);

    // "cycle" appears a known number of times so the search test can assert a
    // count rather than merely "more than zero".
    const QStringList paragraphs {
        QStringLiteral("The cycle time of a graph is the ratio that matters here."),
        QStringLiteral("A minimum mean cycle is found by iterating over every cycle."),
        QStringLiteral("Redaction must destroy content, not merely cover it."),
        QStringLiteral("Selection follows the reading order of the text, not the page."),
    };

    for (int page = 0; page < 3; ++page) {
        if (page > 0)
            writer->newPage();

        painter.setFont(heading);
        painter.drawText(QRect(72, 72, 450, 40),
                         Qt::AlignLeft,
                         QStringLiteral("Latin fixture page %1").arg(page + 1));

        painter.setFont(body);
        int y = 140;
        for (const QString &paragraph : paragraphs) {
            painter.drawText(QRect(72, y, 450, 40), Qt::TextWordWrap, paragraph);
            y += 44;
        }

        painter.setFont(body);
        painter.drawText(QRect(72, 640, 450, 20), Qt::AlignLeft,
                         QStringLiteral("Page %1 of 3").arg(page + 1));
    }

    painter.end();
    qCInfo(lcFixtures) << "wrote" << filePath;
    return true;
}

bool writeCjkSample(const QString &filePath)
{
    const QString family = findCjkFont();
    if (family.isEmpty()) {
        qCWarning(lcFixtures) << "no CJK-capable font installed; skipping CJK fixture";
        return false;
    }

    QScopedPointer<QPdfWriter> writer(makeWriter(filePath));
    QPainter painter(writer.data());
    if (!painter.isActive())
        return false;

    QFont body(family, 12);
    QFont heading(family, 20, QFont::Bold);

    // Deliberately mixed: Traditional and Simplified Han, Japanese kana,
    // full-width punctuation, and a Latin word inside a Chinese sentence --
    // every one of which is a place where word boundaries, search, or snippet
    // cleaning can go wrong.
    const QStringList lines {
        QString::fromUtf8(u8"這是一份用來測試中日韓文字處理的樣本文件。"),
        QString::fromUtf8(u8"搜尋、選取、複製都必須正確處理沒有空白的文字。"),
        QString::fromUtf8(u8"简体中文也要能正常搜寻与选取，不能只支援繁體。"),
        QString::fromUtf8(u8"日本語のテキストも含まれています。カタカナとひらがな。"),
        QString::fromUtf8(u8"混合 PDF 與 English 的句子：全形括號（像這樣）也要處理。"),
        QString::fromUtf8(u8"重複出現的詞：文件、文件、文件。共三次。"),
    };

    for (int page = 0; page < 2; ++page) {
        if (page > 0)
            writer->newPage();

        painter.setFont(heading);
        painter.drawText(QRect(72, 72, 460, 40), Qt::AlignLeft,
                         QString::fromUtf8(u8"中日韓測試 第 %1 頁").arg(page + 1));

        painter.setFont(body);
        int y = 140;
        for (const QString &line : lines) {
            painter.drawText(QRect(72, y, 460, 60), Qt::TextWordWrap, line);
            y += 50;
        }
    }

    painter.end();
    qCInfo(lcFixtures) << "wrote" << filePath << "using font" << family;
    return true;
}

bool writeLargeSample(const QString &filePath, int pageCount)
{
    QScopedPointer<QPdfWriter> writer(makeWriter(filePath));
    QPainter painter(writer.data());
    if (!painter.isActive())
        return false;

    QFont body(QStringLiteral("Arial"), 11);
    QFont heading(QStringLiteral("Arial"), 24, QFont::Bold);

    const int pages = qBound(1, pageCount, 5000);

    for (int page = 0; page < pages; ++page) {
        if (page > 0)
            writer->newPage();

        painter.setFont(heading);
        painter.drawText(QRect(72, 72, 450, 50), Qt::AlignLeft,
                         QStringLiteral("Page %1").arg(page + 1));

        painter.setFont(body);
        for (int line = 0; line < 12; ++line) {
            painter.drawText(QRect(72, 150 + line * 22, 450, 20), Qt::AlignLeft,
                             QStringLiteral("Line %1 of page %2 -- filler text for layout.")
                                 .arg(line + 1).arg(page + 1));
        }
    }

    painter.end();
    qCInfo(lcFixtures) << "wrote" << filePath << "with" << pages << "pages";
    return true;
}

int writeAll(const QString &directory, bool includeLarge)
{
    QDir dir(directory);
    if (!dir.exists() && !QDir().mkpath(directory)) {
        qCWarning(lcFixtures) << "could not create" << directory;
        return 0;
    }

    int written = 0;
    if (writeLatinSample(dir.filePath(QStringLiteral("latin-sample.pdf"))))
        ++written;
    if (writeCjkSample(dir.filePath(QStringLiteral("cjk-sample.pdf"))))
        ++written;
    if (includeLarge && writeLargeSample(dir.filePath(QStringLiteral("large-sample.pdf"))))
        ++written;

    return written;
}

} // namespace lumen::fixtures
