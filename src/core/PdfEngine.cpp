#include "core/PdfEngine.h"

#include <QLoggingCategory>

#ifdef LUMEN_HAS_PDFIUM
#include <fpdfview.h>
#endif

Q_LOGGING_CATEGORY(lcEngine, "lumen.engine")

namespace lumen {

namespace {
bool s_initialized = false;
}

void PdfEngine::initialize()
{
    if (s_initialized)
        return;

#ifdef LUMEN_HAS_PDFIUM
    FPDF_LIBRARY_CONFIG config {};
    config.version = 2;
    config.m_pUserFontPaths = nullptr;
    config.m_pIsolate = nullptr;
    config.m_v8EmbedderSlot = 0;
    FPDF_InitLibraryWithConfig(&config);
    qCInfo(lcEngine) << "PDFium initialized";
#else
    qCWarning(lcEngine) << "built without PDFium -- pages will render as placeholders";
#endif

    s_initialized = true;
}

void PdfEngine::shutdown()
{
    if (!s_initialized)
        return;

#ifdef LUMEN_HAS_PDFIUM
    FPDF_DestroyLibrary();
#endif

    s_initialized = false;
}

bool PdfEngine::isAvailable()
{
#ifdef LUMEN_HAS_PDFIUM
    return true;
#else
    return false;
#endif
}

} // namespace lumen
