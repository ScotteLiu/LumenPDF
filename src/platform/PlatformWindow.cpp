#include "platform/PlatformWindow.h"

#include "app/Timing.h"

#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQuickWindow>
#include <QWindow>

#ifdef Q_OS_WIN
// windows.h must come first: psapi.h uses its typedefs and does not include it.
#include <windows.h>

#include <psapi.h>
#endif

Q_LOGGING_CATEGORY(lcBench, "lumen.bench")

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>

// Present since Windows 11 22H2 but not in every SDK header yet.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2
#endif
#endif

namespace lumen {

PlatformWindow::PlatformWindow(QObject *parent)
    : QObject(parent)
{
}

QFont PlatformWindow::preferredUiFont()
{
    // In preference order. Inter is the design target; the rest are the
    // closest neutral grotesques each platform actually ships.
    static const QStringList preferences {
        QStringLiteral("Inter"),
        QStringLiteral("Inter Display"),
#ifdef Q_OS_WIN
        QStringLiteral("Segoe UI Variable Text"),
        QStringLiteral("Segoe UI"),
#elif defined(Q_OS_MACOS)
        QStringLiteral("SF Pro Text"),
        QStringLiteral("Helvetica Neue"),
#else
        QStringLiteral("Inter Variable"),
        QStringLiteral("Cantarell"),
        QStringLiteral("Noto Sans"),
        QStringLiteral("DejaVu Sans"),
#endif
    };

    const QStringList installed = QFontDatabase::families();
    for (const QString &candidate : preferences) {
        if (installed.contains(candidate, Qt::CaseInsensitive)) {
            QFont font(candidate);
            font.setStyleStrategy(QFont::PreferAntialias);
            return font;
        }
    }

    return QGuiApplication::font();
}

QString PlatformWindow::uiFontFamily() const
{
    return QGuiApplication::font().family();
}

QString PlatformWindow::benchmark() const
{
    return qEnvironmentVariable("LUMEN_BENCH");
}

QString PlatformWindow::recordDir() const
{
    return qEnvironmentVariable("LUMEN_RECORD");
}

bool PlatformWindow::captureFrame(QWindow *window, int index)
{
    const QString dir = recordDir();
    if (dir.isEmpty() || !window)
        return false;

    auto *quickWindow = qobject_cast<QQuickWindow *>(window);
    if (!quickWindow)
        return false;

    // grabWindow() is synchronous and returns what was actually rendered, so
    // frames cannot drift out of step with the animation driving them.
    const QImage frame = quickWindow->grabWindow();
    if (frame.isNull())
        return false;

    QDir().mkpath(dir);
    // Zero-padded so the encoder's glob picks them up in order.
    const QString path = QDir(dir).filePath(
        QStringLiteral("frame-%1.png").arg(index, 5, 10, QLatin1Char('0')));

    return frame.save(path, "PNG");
}

void PlatformWindow::markTiming(const QString &milestone)
{
    Timing::instance().mark(milestone);
}

void PlatformWindow::reportFrameRate(const QString &name, int frames, qreal seconds)
{
    if (seconds <= 0.0)
        return;

    const qreal fps = frames / seconds;
    qCInfo(lcBench).nospace()
        << "bench " << name << ": " << frames << " frames in "
        << seconds << " s = " << qRound(fps * 10) / 10.0 << " fps, "
        << qRound(memoryMegabytes()) << " MB";

    m_benchmarkName = name;
    m_benchmarkFps = fps;
    m_benchmarkFrames = frames;
}

qreal PlatformWindow::memoryMegabytes() const
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters {};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return qreal(counters.WorkingSetSize) / (1024.0 * 1024.0);
    return 0.0;
#else
    // Left unimplemented rather than guessed at; the benchmark reports 0 and
    // the harness can see that it is not a measurement.
    return 0.0;
#endif
}

int PlatformWindow::initialDark() const
{
    const QString theme = qEnvironmentVariable("LUMEN_THEME").toLower();
    if (theme == QLatin1String("light"))
        return 0;
    if (theme == QLatin1String("dark"))
        return 1;
    return -1;
}

int PlatformWindow::initialSidebarTab() const
{
    if (!qEnvironmentVariableIsSet("LUMEN_SIDEBAR_TAB"))
        return -1;

    bool ok = false;
    const int tab = qEnvironmentVariableIntValue("LUMEN_SIDEBAR_TAB", &ok);
    return ok ? tab : -1;
}

void PlatformWindow::applyBackdrop(QWindow *window, bool dark)
{
    if (!window)
        return;

#ifdef Q_OS_WIN
    const auto handle = reinterpret_cast<HWND>(window->winId());
    int backdrop = DWMSBT_MAINWINDOW; // Mica
    DwmSetWindowAttribute(handle, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    setDarkTitleBar(window, dark);
#else
    Q_UNUSED(dark)
#endif
}

void PlatformWindow::setDarkTitleBar(QWindow *window, bool dark)
{
    if (!window)
        return;

#ifdef Q_OS_WIN
    const auto handle = reinterpret_cast<HWND>(window->winId());
    BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(handle, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
#else
    Q_UNUSED(dark)
#endif
}

} // namespace lumen
