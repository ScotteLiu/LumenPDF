#pragma once

#include <QFont>
#include <QObject>

class QWindow;

namespace lumen {

// Native window chrome. Every platform-specific call in the app lives behind
// this class -- adding macOS (NSVisualEffectView) or Linux (KWin blur) later
// means implementing these two methods, not touching the UI.
class PlatformWindow : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString uiFontFamily READ uiFontFamily CONSTANT)

    // Test hook: which sidebar tab to open on launch (LUMEN_SIDEBAR_TAB).
    // Lets capture runs and smoke tests reach any panel without synthesising
    // input events. -1 when unset, which means "leave the default alone".
    Q_PROPERTY(int initialSidebarTab READ initialSidebarTab CONSTANT)

public:
    explicit PlatformWindow(QObject *parent = nullptr);

    // The best available UI font, chosen from a preference list at startup.
    //
    // Lumen is designed against Inter; where it is not installed the closest
    // system face is used instead. Resolving once here means no component has
    // to carry a fallback chain, and a missing font degrades gracefully
    // rather than dropping the whole app onto a serif default.
    static QFont preferredUiFont();
    QString uiFontFamily() const;
    int initialSidebarTab() const;

    // Applies the acrylic/mica backdrop the sidebar and toolbar are designed
    // against. No-op where the platform has no equivalent.
    Q_INVOKABLE void applyBackdrop(QWindow *window, bool dark);

    // Matches the system title bar to the app theme.
    Q_INVOKABLE void setDarkTitleBar(QWindow *window, bool dark);
};

} // namespace lumen
