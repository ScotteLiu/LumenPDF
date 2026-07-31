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

int writeWorldScriptsSample(const QString &filePath)
{
    // Each entry is a script, a phrase in it, and the fonts that can draw it.
    // The phrases are chosen for what they stress, not for what they mean:
    // Arabic and Hebrew for right-to-left order, Devanagari and Thai for
    // clusters that are several codepoints and one glyph, Vietnamese for
    // stacked combining marks, Greek and Cyrillic for the case rules that
    // case-insensitive search has to get right.
    struct Script {
        const char *name;
        QString text;
        QStringList fonts;
    };

    const QList<Script> scripts {
        { "Latin",
          QStringLiteral("The quick brown fox jumps over the lazy dog."),
          { QStringLiteral("Arial") } },

        { "Latin (diacritics)",
          QString::fromUtf8(u8"Příliš žluťoučký kůň úpěl ďábelské ódy — ÄÖÜ àéîõü."),
          { QStringLiteral("Arial") } },

        { "Vietnamese",
          QString::fromUtf8(u8"Tiếng Việt có nhiều dấu thanh chồng lên nhau."),
          { QStringLiteral("Arial"), QStringLiteral("Segoe UI") } },

        { "Greek",
          QString::fromUtf8(u8"Ελληνικό κείμενο με τόνους και τελικό σίγμα ς."),
          { QStringLiteral("Arial"), QStringLiteral("Segoe UI") } },

        { "Cyrillic",
          QString::fromUtf8(u8"Съешь же ещё этих мягких французских булок."),
          { QStringLiteral("Arial"), QStringLiteral("Segoe UI") } },

        { "Arabic",
          QString::fromUtf8(u8"النص العربي يُكتب من اليمين إلى اليسار."),
          { QStringLiteral("Segoe UI"), QStringLiteral("Arial"),
            QStringLiteral("Tahoma") } },

        { "Hebrew",
          QString::fromUtf8(u8"טקסט בעברית נכתב מימין לשמאל."),
          { QStringLiteral("Segoe UI"), QStringLiteral("Arial"),
            QStringLiteral("David") } },

        { "Devanagari",
          QString::fromUtf8(u8"हिन्दी में संयुक्त अक्षर होते हैं।"),
          { QStringLiteral("Nirmala UI"), QStringLiteral("Mangal") } },

        { "Thai",
          QString::fromUtf8(u8"ภาษาไทยเขียนติดกันโดยไม่มีช่องว่าง"),
          { QStringLiteral("Leelawadee UI"), QStringLiteral("Tahoma") } },

        { "Korean",
          QString::fromUtf8(u8"한국어 텍스트도 올바르게 처리되어야 합니다."),
          { QStringLiteral("Malgun Gothic"), QStringLiteral("Gulim") } },
    };

    const QStringList installed = QFontDatabase::families();

    QScopedPointer<QPdfWriter> writer(makeWriter(filePath));
    QPainter painter(writer.data());
    if (!painter.isActive())
        return 0;

    int written = 0;
    bool firstPage = true;

    for (const Script &script : scripts) {
        QString family;
        for (const QString &candidate : script.fonts) {
            if (installed.contains(candidate, Qt::CaseInsensitive)) {
                family = candidate;
                break;
            }
        }
        if (family.isEmpty()) {
            qCWarning(lcFixtures) << "no font for" << script.name << "-- skipped";
            continue;
        }

        if (!firstPage)
            writer->newPage();
        firstPage = false;

        painter.setFont(QFont(QStringLiteral("Arial"), 10, QFont::Bold));
        painter.drawText(QRect(72, 72, 460, 24), Qt::AlignLeft,
                         QString::fromLatin1(script.name));

        painter.setFont(QFont(family, 16));
        painter.drawText(QRect(72, 120, 460, 120), Qt::TextWordWrap, script.text);

        ++written;
    }

    painter.end();
    qCInfo(lcFixtures) << "wrote" << filePath << "with" << written << "scripts";
    return written;
}

bool writeDemoDocument(const QString &filePath)
{
    QScopedPointer<QPdfWriter> writer(makeWriter(filePath));
    QPainter painter(writer.data());
    if (!painter.isActive())
        return false;

    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const QString serif = QStringLiteral("Georgia");
    const QString sans = QStringLiteral("Arial");

    const int left = 84;
    const int colWidth = 200;
    const int gutter = 24;
    const int rightCol = left + colWidth + gutter;

    QFont title(serif, 26, QFont::Bold);
    QFont heading(sans, 12, QFont::Bold);
    QFont body(serif, 9);
    QFont caption(sans, 7);

    // Sentences repeat "cycle" a known number of times so the recorded search
    // has something to find, and read as real prose rather than lorem ipsum.
    const QStringList paragraphs {
        QStringLiteral("Every document begins as something someone needs to read. The "
                       "cycle from writing to reading passes through a dozen tools, and "
                       "each one is a chance to lose fidelity, speed, or trust."),
        QStringLiteral("A viewer that takes eight seconds to show a page has already "
                       "failed, however complete its feature list. The cycle time of the "
                       "tool becomes the cycle time of the work."),
        QStringLiteral("Rendering happens on a worker pool, never on the thread that "
                       "draws the interface. Pages are rasterised as they approach the "
                       "viewport and discarded on a byte budget rather than a page count."),
        QStringLiteral("Redaction removes content instead of covering it. The page is "
                       "rasterised and its objects replaced, so nothing survives "
                       "underneath the mark for anyone to recover later."),
        QStringLiteral("Annotations are written as standards-conformant PDF markup. A "
                       "highlight made here opens as a highlight everywhere else, which "
                       "is the only definition of the feature that matters."),
        QStringLiteral("Text extraction normalises the lookalike codepoints that many "
                       "fonts map their glyphs through, so copied text is the characters "
                       "you saw rather than characters that merely resemble them."),
    };

    // -- Cover -------------------------------------------------------------
    painter.fillRect(QRect(0, 0, 595, 842), QColor(0xFA, 0xFA, 0xFC));

    painter.setPen(QColor(0x0A, 0x84, 0xFF));
    painter.fillRect(QRect(left, 150, 54, 4), QColor(0x0A, 0x84, 0xFF));

    painter.setPen(QColor(0x1D, 0x1D, 0x1F));
    painter.setFont(title);
    painter.drawText(QRect(left, 180, 400, 120), Qt::TextWordWrap,
                     QStringLiteral("Notes on building a\nfast document viewer"));

    painter.setFont(QFont(sans, 10));
    painter.setPen(QColor(0x6E, 0x6E, 0x73));
    painter.drawText(QRect(left, 320, 400, 60), Qt::TextWordWrap,
                     QStringLiteral("An engineering note on rendering, redaction and "
                                    "text handling.\nWritten for the LumenPDF demo."));

    painter.setFont(caption);
    painter.drawText(QRect(left, 740, 400, 20), Qt::AlignLeft,
                     QStringLiteral("LumenPDF · sample document · 2026"));

    // -- Content pages -----------------------------------------------------
    const QStringList sections {
        QStringLiteral("1  Why speed is the first feature"),
        QStringLiteral("2  Rendering off the main thread"),
        QStringLiteral("3  Redaction that removes"),
        QStringLiteral("4  Text, in every script"),
    };

    for (int page = 0; page < sections.size(); ++page) {
        writer->newPage();
        painter.fillRect(QRect(0, 0, 595, 842), Qt::white);

        painter.setFont(heading);
        painter.setPen(QColor(0x1D, 0x1D, 0x1F));
        painter.drawText(QRect(left, 84, 420, 30), Qt::AlignLeft, sections.at(page));

        painter.fillRect(QRect(left, 118, 424, 1), QColor(0, 0, 0, 40));

        painter.setFont(body);
        painter.setPen(QColor(0x2A, 0x2A, 0x2E));

        int y = 140;
        for (int i = 0; i < paragraphs.size(); ++i) {
            const QString text = paragraphs.at((i + page) % paragraphs.size());
            const int x = (i % 2 == 0) ? left : rightCol;
            if (i % 2 == 0 && i > 0)
                y += 96;

            painter.drawText(QRect(x, y, colWidth, 92), Qt::TextWordWrap, text);
        }

        // A figure block, so the page is not only text and the demo has
        // something with visual weight to scroll past.
        const int figureTop = 560;
        painter.fillRect(QRect(left, figureTop, 424, 150), QColor(0xF2, 0xF3, 0xF7));

        const int bars[] = { 96, 132, 58, 118, 84, 142, 70 };
        for (int i = 0; i < 7; ++i) {
            const int h = bars[i];
            painter.fillRect(QRect(left + 26 + i * 56, figureTop + 132 - h, 30, h),
                             i == 5 ? QColor(0x0A, 0x84, 0xFF) : QColor(0xC2, 0xC6, 0xD2));
        }

        painter.setFont(caption);
        painter.setPen(QColor(0x8A, 0x8A, 0x90));
        painter.drawText(QRect(left, figureTop + 156, 424, 16), Qt::AlignLeft,
                         QStringLiteral("Figure %1 — time to first rendered page, by build")
                             .arg(page + 1));

        painter.drawText(QRect(left, 780, 424, 16), Qt::AlignRight,
                         QStringLiteral("%1").arg(page + 2));
    }

    painter.end();
    qCInfo(lcFixtures) << "wrote demo document" << filePath;
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
    if (writeWorldScriptsSample(dir.filePath(QStringLiteral("world-scripts.pdf"))) > 0)
        ++written;
    if (writeDemoDocument(dir.filePath(QStringLiteral("demo-document.pdf"))))
        ++written;
    if (includeLarge && writeLargeSample(dir.filePath(QStringLiteral("large-sample.pdf"))))
        ++written;

    return written;
}

} // namespace lumen::fixtures
