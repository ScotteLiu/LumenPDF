#include "platform/PlatformWindow.h"

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
