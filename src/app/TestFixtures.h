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

// Two A1 pages. Redaction rasterises at 300 dpi and the render path refuses
// anything over 40 megapixels, so these cannot be flattened -- the fixture
// exists to prove that refusal is reported rather than silently counted as
// success.
bool writeOversizeSample(const QString &filePath);

// Multi-page Latin text with known, countable phrases.
bool writeLatinSample(const QString &filePath);

// Traditional and Simplified Chinese, plus Japanese, with known phrases.
// Returns false when no CJK-capable font is installed, since a fixture full of
// tofu would make the tests meaningless rather than failing.
bool writeCjkSample(const QString &filePath);

// One page per writing system, covering the ways text extraction can go wrong:
// right-to-left order, combining marks, Indic clusters, and scripts with no
// spaces. Each page carries a phrase the tests assert on.
//
// Returns the number of scripts actually written -- a script whose font is not
// installed is skipped rather than rendered as boxes, because a fixture full of
// tofu tests nothing.
int writeWorldScriptsSample(const QString &filePath);

// A dense, designed document used for the product-page recording.
//
// Separate from the test fixtures on purpose: those are deliberately sparse and
// their contents are pinned by assertions, which makes them look empty on
// screen. This one exists to be looked at, and its text is written by us so the
// public demo does not republish somebody else's paper.
bool writeDemoDocument(const QString &filePath);

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
