#include "platform/PlatformWindow.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QWindow>

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
