#pragma once

#include <QString>

namespace lumen {

class DocumentController;
class PlatformWindow;
class Settings;

// Dumps the document's observable state as JSON.
//
// The test suite has to assert on numbers, not on screenshots. Everything the
// tests check -- page count, outline size, search hit count, selected text,
// per-page text length, form field values -- is read back through the same
// controllers the UI uses, so a passing test means the UI would have seen the
// same thing.
namespace report {

bool write(const QString &filePath,
           DocumentController *controller,
           PlatformWindow *platform = nullptr,
           Settings *settings = nullptr);

} // namespace report
} // namespace lumen
