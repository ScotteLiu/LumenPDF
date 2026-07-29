#pragma once

#include <QObject>

class QWindow;

namespace lumen {

// Native window chrome. Every platform-specific call in the app lives behind
// this class -- adding macOS (NSVisualEffectView) or Linux (KWin blur) later
// means implementing these two methods, not touching the UI.
class PlatformWindow : public QObject {
    Q_OBJECT

public:
    explicit PlatformWindow(QObject *parent = nullptr);

    // Applies the acrylic/mica backdrop the sidebar and toolbar are designed
    // against. No-op where the platform has no equivalent.
    Q_INVOKABLE void applyBackdrop(QWindow *window, bool dark);

    // Matches the system title bar to the app theme.
    Q_INVOKABLE void setDarkTitleBar(QWindow *window, bool dark);
};

} // namespace lumen
