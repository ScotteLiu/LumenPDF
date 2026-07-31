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

    // Which theme to start in: 1 dark, 0 light, -1 leave the default alone.
    // Driven by LUMEN_THEME=dark|light so both themes can be screenshotted.
    Q_PROPERTY(int initialDark READ initialDark CONSTANT)

    // Benchmark to run instead of waiting for the user, from LUMEN_BENCH.
    // Empty for a normal launch.
    Q_PROPERTY(QString benchmark READ benchmark CONSTANT)

    // Directory to record frames into, from LUMEN_RECORD. Empty for a normal
    // launch. Recording exists so the demo on the product page is the real
    // application rather than a mock-up.
    Q_PROPERTY(QString recordDir READ recordDir CONSTANT)

public:
    explicit PlatformWindow(QObject *parent = nullptr);

    // The best available UI font, chosen from a preference list at startup.
    //
    // Lumen is designed against Inter; where it is not installed the closest
    // system face is used instead. Resolving once here means no component has
    // to carry a fallback chain, and a missing font degrades gracefully
    // rather than dropping the whole app onto a serif default.
    // Last completed benchmark, for the state report. Empty name means none ran.
    QString benchmarkName() const { return m_benchmarkName; }
    qreal benchmarkFps() const { return m_benchmarkFps; }
    int benchmarkFrames() const { return m_benchmarkFrames; }

    static QFont preferredUiFont();
    QString uiFontFamily() const;
    int initialSidebarTab() const;
    int initialDark() const;
    QString benchmark() const;
    QString recordDir() const;

    // Writes one frame of the recording. `window` is the QQuickWindow to grab
    // and `index` orders the files for the encoder.
    Q_INVOKABLE bool captureFrame(QWindow *window, int index);

    // Records a milestone on the startup timeline from QML. The moment the
    // first page raster is actually on screen can only be known up there.
    Q_INVOKABLE void markTiming(const QString &milestone);

    // Records a completed benchmark run so it lands in the state report.
    Q_INVOKABLE void reportFrameRate(const QString &name, int frames, qreal seconds);

    // Current process working set, in megabytes. Measured in-process because
    // an external sample races with the thing it is measuring.
    Q_INVOKABLE qreal memoryMegabytes() const;

    // Applies the acrylic/mica backdrop the sidebar and toolbar are designed
    // against. No-op where the platform has no equivalent.
    Q_INVOKABLE void applyBackdrop(QWindow *window, bool dark);

    // Matches the system title bar to the app theme.
    Q_INVOKABLE void setDarkTitleBar(QWindow *window, bool dark);

private:
    QString m_benchmarkName;
    qreal m_benchmarkFps = 0.0;
    int m_benchmarkFrames = 0;
};

} // namespace lumen
