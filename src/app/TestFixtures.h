#pragma once

#include <QString>

namespace lumen {

// Generates the PDF fixtures the test suite runs against.
//
// Written with QPdfWriter, which is already linked, rather than by hand: a CJK
// fixture needs a CID font subset embedded, and hand-assembling one would be
// a PDF-generator project of its own. The form fixture stays hand-written
// (scripts/make-form-fixture.ps1) because QPdfWriter cannot emit AcroForm
// widgets at all.
//
// Fixtures are generated rather than committed so the repository holds readable
// source instead of opaque binaries, and so what they contain is exactly what
// the assertions expect.
namespace fixtures {

// Multi-page Latin text with known, countable phrases.
bool writeLatinSample(const QString &filePath);

// Traditional and Simplified Chinese, plus Japanese, with known phrases.
// Returns false when no CJK-capable font is installed, since a fixture full of
// tofu would make the CJK tests meaningless rather than failing.
bool writeCjkSample(const QString &filePath);

// A deliberately long document, for measuring what happens at scale: page
// layout, scroll performance, and memory when a viewer cannot possibly hold
// every page. Text is minimal so the file stays small and the measurement is
// about page count rather than content.
bool writeLargeSample(const QString &filePath, int pageCount = 1000);

// Writes every generated fixture into `directory`. Returns the number written.
// The large sample is skipped unless `includeLarge`, since it takes seconds.
int writeAll(const QString &directory, bool includeLarge = false);

} // namespace fixtures
} // namespace lumen
