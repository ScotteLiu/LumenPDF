#pragma once

#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace lumen {

// One recognised word, with where it sits in the image that was recognised.
struct OcrWord {
    QString text;
    QRectF box;      // pixels, top-left origin, in the recognised image
};

struct OcrLine {
    QString text;
    QVector<OcrWord> words;
};

struct OcrResult {
    bool ok = false;
    QString language;
    QVector<OcrLine> lines;
    QString error;

    int wordCount() const
    {
        int n = 0;
        for (const OcrLine &line : lines)
            n += line.words.size();
        return n;
    }
};

// Optical character recognition.
//
// Backed by Windows.Media.Ocr, which ships with the operating system. That is a
// deliberate trade: bundling Tesseract would add roughly a hundred megabytes of
// language data to a forty-megabyte installer, and the OS engine is already
// there, already offline, and already carries whichever languages the user
// installed for their own system.
//
// The cost is that the available languages are Windows' to decide, and that
// this is the one part of the app with no cross-platform story yet. Both are
// stated rather than hidden -- see availableLanguages(), which is what the UI
// offers instead of pretending every language works.
class Ocr {
public:
    // Language tags Windows can currently recognise, most-preferred first.
    // Empty when OCR is unavailable on this machine.
    static QStringList availableLanguages();

    // True when at least one recogniser is installed.
    static bool isAvailable();

    // Recognises an image. `languageTag` may be empty, in which case the user's
    // own language preferences decide. Blocking: call it off the GUI thread.
    static OcrResult recognise(const QImage &image, const QString &languageTag = {});
};

} // namespace lumen
