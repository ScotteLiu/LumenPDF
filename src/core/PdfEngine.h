#pragma once

// Process-wide lifetime for the underlying PDF backend.
//
// PDFium requires FPDF_InitLibrary()/FPDF_DestroyLibrary() to bracket every
// other call, and neither is reentrant. Everything funnels through here so the
// backend can be swapped later without touching call sites.
namespace lumen {

class PdfEngine {
public:
    static void initialize();
    static void shutdown();

    // True when a real render backend is compiled in; false for the stub build.
    static bool isAvailable();

private:
    PdfEngine() = default;
};

} // namespace lumen
