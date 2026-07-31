#include "ocr/OcrEngine.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcOcr, "lumen.ocr")

#ifdef LUMEN_HAS_WINDOWS_OCR

// C++/WinRT and Qt disagree about a few Windows macros, so the WinRT headers
// come first and in their own translation unit. Nothing Qt-specific beyond
// QImage and QString is used below for the same reason.
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>

namespace {

// Names are qualified rather than imported wholesale: WinRT also has a type
// called OcrResult, and letting the two meet in one namespace would make every
// use ambiguous in a way the compiler explains badly.
namespace WinOcr = winrt::Windows::Media::Ocr;
namespace WinImaging = winrt::Windows::Graphics::Imaging;
namespace WinGlobalization = winrt::Windows::Globalization;
namespace WinStreams = winrt::Windows::Storage::Streams;
namespace WinCrypto = winrt::Windows::Security::Cryptography;

using winrt::hresult_error;
using winrt::hstring;

// WinRT needs an initialised apartment on every thread that touches it, and OCR
// runs on the worker pool -- so this is per-thread, not once per process.
// Multi-threaded apartment: nothing here has UI affinity.
struct ApartmentGuard {
    bool initialised = false;

    ApartmentGuard()
    {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            initialised = true;
        } catch (const hresult_error &) {
            // Already initialised on this thread, which is fine and common.
            initialised = false;
        }
    }

    ~ApartmentGuard()
    {
        if (initialised)
            winrt::uninit_apartment();
    }
};

QString toQString(const hstring &value)
{
    return QString::fromWCharArray(value.c_str(), int(value.size()));
}

// Wraps a QImage's pixels as a SoftwareBitmap without asking the caller to
// think about pixel formats. Windows OCR wants BGRA8.
WinImaging::SoftwareBitmap toSoftwareBitmap(const QImage &source)
{
    const QImage image = source.format() == QImage::Format_ARGB32
        ? source
        : source.convertToFormat(QImage::Format_ARGB32);

    // ARGB32 is BGRA in memory on little-endian, which is what Bgra8 means.
    const auto *data = image.constBits();
    const auto bytes = static_cast<size_t>(image.sizeInBytes());

    const WinStreams::IBuffer buffer =
        WinCrypto::CryptographicBuffer::CreateFromByteArray(
            winrt::array_view<const uint8_t>(data, data + bytes));

    return WinImaging::SoftwareBitmap::CreateCopyFromBuffer(
        buffer, WinImaging::BitmapPixelFormat::Bgra8,
        image.width(), image.height());
}

} // namespace

namespace lumen {

QStringList Ocr::availableLanguages()
{
    QStringList tags;
    try {
        ApartmentGuard guard;
        for (const auto &language : WinOcr::OcrEngine::AvailableRecognizerLanguages())
            tags.append(toQString(language.LanguageTag()));
    } catch (const hresult_error &error) {
        qCWarning(lcOcr) << "could not enumerate recognisers:" << toQString(error.message());
    }
    return tags;
}

bool Ocr::isAvailable()
{
    return !availableLanguages().isEmpty();
}

OcrResult Ocr::recognise(const QImage &image, const QString &languageTag)
{
    OcrResult result;

    if (image.isNull()) {
        result.error = QStringLiteral("Nothing to recognise.");
        return result;
    }

    // Windows OCR rejects images below 40px and above 10000px on either side.
    if (image.width() < 40 || image.height() < 40
        || image.width() > 10000 || image.height() > 10000) {
        result.error = QStringLiteral("Image is outside the size the recogniser accepts.");
        return result;
    }

    try {
        ApartmentGuard guard;

        WinOcr::OcrEngine engine {nullptr};

        if (!languageTag.isEmpty()) {
            const WinGlobalization::Language language(
                hstring(reinterpret_cast<const wchar_t *>(languageTag.utf16())));
            engine = WinOcr::OcrEngine::TryCreateFromLanguage(language);
        }

        // Falling back to the user's own language preferences rather than to
        // English: someone reading Thai documents on a Thai system should not
        // have to choose a language before OCR does the obvious thing.
        if (!engine)
            engine = WinOcr::OcrEngine::TryCreateFromUserProfileLanguages();

        if (!engine) {
            result.error = QStringLiteral(
                "No OCR language is installed. Add one in Windows Settings › "
                "Time & language › Language & region.");
            return result;
        }

        result.language = toQString(engine.RecognizerLanguage().LanguageTag());

        const auto bitmap = toSoftwareBitmap(image);
        const WinOcr::OcrResult ocr = engine.RecognizeAsync(bitmap).get();

        for (const auto &line : ocr.Lines()) {
            OcrLine outLine;
            outLine.text = toQString(line.Text());

            for (const auto &word : line.Words()) {
                const auto r = word.BoundingRect();
                outLine.words.append(OcrWord {
                    toQString(word.Text()),
                    QRectF(r.X, r.Y, r.Width, r.Height)
                });
            }

            result.lines.append(outLine);
        }

        result.ok = true;
        qCInfo(lcOcr) << "recognised" << result.wordCount() << "words in"
                      << result.language;
    } catch (const hresult_error &error) {
        result.error = toQString(error.message());
        qCWarning(lcOcr) << "recognition failed:" << result.error;
    }

    return result;
}

} // namespace lumen

#else   // LUMEN_HAS_WINDOWS_OCR

namespace lumen {

QStringList Ocr::availableLanguages() { return {}; }
bool Ocr::isAvailable() { return false; }

OcrResult Ocr::recognise(const QImage &, const QString &)
{
    OcrResult result;
    result.error = QStringLiteral("This build has no OCR backend.");
    return result;
}

} // namespace lumen

#endif
