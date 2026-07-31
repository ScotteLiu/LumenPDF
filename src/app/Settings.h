#pragma once

#include <QObject>
#include <QSettings>
#include <QSize>
#include <QVariantList>

namespace lumen {

// Everything the application remembers between runs.
//
// One object rather than scattered QSettings calls, for two reasons. Keys
// written from several places drift apart, and more importantly every setting
// here is a property with a change signal, so QML binds to it and writing is a
// side effect of the binding rather than something a caller has to remember.
//
// Writes are deliberately not batched. Losing the window size because the
// process was killed is a worse trade than a few hundred bytes of I/O.
class Settings : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString zoomMode READ zoomMode WRITE setZoomMode NOTIFY zoomModeChanged)
    Q_PROPERTY(bool sidebarVisible READ sidebarVisible WRITE setSidebarVisible NOTIFY sidebarVisibleChanged)
    Q_PROPERTY(int sidebarTab READ sidebarTab WRITE setSidebarTab NOTIFY sidebarTabChanged)
    Q_PROPERTY(bool restorePosition READ restorePosition WRITE setRestorePosition NOTIFY restorePositionChanged)
    Q_PROPERTY(bool checkForUpdates READ checkForUpdates WRITE setCheckForUpdates NOTIFY checkForUpdatesChanged)
    Q_PROPERTY(QSize windowSize READ windowSize WRITE setWindowSize NOTIFY windowSizeChanged)
    Q_PROPERTY(bool windowMaximized READ windowMaximized WRITE setWindowMaximized NOTIFY windowMaximizedChanged)

    // Newest first: [{ path, name, page, opened }]
    Q_PROPERTY(QVariantList recentFiles READ recentFiles NOTIFY recentFilesChanged)

public:
    explicit Settings(QObject *parent = nullptr);

    QString theme() const;
    void setTheme(const QString &value);

    // Empty means "follow the system locale", which is the default and the
    // only correct one for an application that claims to serve every language.
    QString language() const;
    void setLanguage(const QString &value);

    QString zoomMode() const;
    void setZoomMode(const QString &value);

    bool sidebarVisible() const;
    void setSidebarVisible(bool value);

    int sidebarTab() const;
    void setSidebarTab(int value);

    bool restorePosition() const;
    void setRestorePosition(bool value);

    bool checkForUpdates() const;
    void setCheckForUpdates(bool value);

    QSize windowSize() const;
    void setWindowSize(const QSize &value);

    bool windowMaximized() const;
    void setWindowMaximized(bool value);

    QVariantList recentFiles() const;

    // Records that a file was opened, moving it to the front. Files that no
    // longer exist are dropped when the list is read, not here -- a file on a
    // disconnected drive should not be forgotten for being briefly absent.
    Q_INVOKABLE void noteOpened(const QString &path);

    // Remembers where reading stopped. Called on close and on page changes.
    Q_INVOKABLE void notePosition(const QString &path, int pageIndex);

    // The page to reopen a file at, or 0.
    Q_INVOKABLE int positionFor(const QString &path) const;

    Q_INVOKABLE void removeRecent(const QString &path);
    Q_INVOKABLE void clearRecent();

    // Erases everything and restores defaults. Wired to a button, so it says
    // what it does rather than quietly leaving stale keys behind.
    Q_INVOKABLE void resetAll();

signals:
    void themeChanged();
    void languageChanged();
    void zoomModeChanged();
    void sidebarVisibleChanged();
    void sidebarTabChanged();
    void restorePositionChanged();
    void checkForUpdatesChanged();
    void windowSizeChanged();
    void windowMaximizedChanged();
    void recentFilesChanged();

private:
    QVariantList loadRecent() const;
    void storeRecent(const QVariantList &entries);

    mutable QSettings m_settings;
};

} // namespace lumen
