#include "app/Settings.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

namespace lumen {

namespace {

constexpr int kMaxRecent = 12;

// Recent files are stored as one JSON string rather than a QSettings array.
// An array of groups is written key by key, so a crash midway leaves a
// half-written list; a single value is replaced atomically or not at all.
constexpr auto kRecentKey = "recent/files";

} // namespace

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_settings(QStringLiteral("Lumen"), QStringLiteral("LumenPDF"))
{
}

QString Settings::theme() const
{
    return m_settings.value(QStringLiteral("appearance/theme"), QStringLiteral("dark")).toString();
}

void Settings::setTheme(const QString &value)
{
    if (value == theme())
        return;
    m_settings.setValue(QStringLiteral("appearance/theme"), value);
    emit themeChanged();
}

QString Settings::language() const
{
    return m_settings.value(QStringLiteral("appearance/language"), QString()).toString();
}

void Settings::setLanguage(const QString &value)
{
    if (value == language())
        return;
    m_settings.setValue(QStringLiteral("appearance/language"), value);
    emit languageChanged();
}

QString Settings::zoomMode() const
{
    return m_settings.value(QStringLiteral("view/zoomMode"), QStringLiteral("fitWidth")).toString();
}

void Settings::setZoomMode(const QString &value)
{
    if (value == zoomMode())
        return;
    m_settings.setValue(QStringLiteral("view/zoomMode"), value);
    emit zoomModeChanged();
}

bool Settings::sidebarVisible() const
{
    return m_settings.value(QStringLiteral("view/sidebarVisible"), true).toBool();
}

void Settings::setSidebarVisible(bool value)
{
    if (value == sidebarVisible())
        return;
    m_settings.setValue(QStringLiteral("view/sidebarVisible"), value);
    emit sidebarVisibleChanged();
}

int Settings::sidebarTab() const
{
    return m_settings.value(QStringLiteral("view/sidebarTab"), 0).toInt();
}

void Settings::setSidebarTab(int value)
{
    if (value == sidebarTab())
        return;
    m_settings.setValue(QStringLiteral("view/sidebarTab"), value);
    emit sidebarTabChanged();
}

bool Settings::restorePosition() const
{
    return m_settings.value(QStringLiteral("view/restorePosition"), true).toBool();
}

void Settings::setRestorePosition(bool value)
{
    if (value == restorePosition())
        return;
    m_settings.setValue(QStringLiteral("view/restorePosition"), value);
    emit restorePositionChanged();
}

bool Settings::checkForUpdates() const
{
    return m_settings.value(QStringLiteral("updates/check"), true).toBool();
}

void Settings::setCheckForUpdates(bool value)
{
    if (value == checkForUpdates())
        return;
    m_settings.setValue(QStringLiteral("updates/check"), value);
    emit checkForUpdatesChanged();
}

QSize Settings::windowSize() const
{
    return m_settings.value(QStringLiteral("window/size"), QSize(1280, 860)).toSize();
}

void Settings::setWindowSize(const QSize &value)
{
    // Guard against storing a size from a window that is mid-teardown or
    // minimised; reopening at 0x0 leaves an invisible application.
    if (value.width() < 480 || value.height() < 360 || value == windowSize())
        return;
    m_settings.setValue(QStringLiteral("window/size"), value);
    emit windowSizeChanged();
}

bool Settings::windowMaximized() const
{
    return m_settings.value(QStringLiteral("window/maximized"), false).toBool();
}

void Settings::setWindowMaximized(bool value)
{
    if (value == windowMaximized())
        return;
    m_settings.setValue(QStringLiteral("window/maximized"), value);
    emit windowMaximizedChanged();
}

QVariantList Settings::loadRecent() const
{
    const QByteArray raw = m_settings.value(QLatin1String(kRecentKey)).toString().toUtf8();
    if (raw.isEmpty())
        return {};

    const QJsonDocument json = QJsonDocument::fromJson(raw);
    if (!json.isArray())
        return {};

    QVariantList entries;
    const QJsonArray array = json.array();
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const QString path = object.value(QStringLiteral("path")).toString();
        if (path.isEmpty())
            continue;

        QVariantMap entry;
        entry[QStringLiteral("path")] = path;
        entry[QStringLiteral("name")] = QFileInfo(path).fileName();
        entry[QStringLiteral("page")] = object.value(QStringLiteral("page")).toInt();
        entry[QStringLiteral("opened")] = object.value(QStringLiteral("opened")).toString();
        entries.append(entry);
    }
    return entries;
}

void Settings::storeRecent(const QVariantList &entries)
{
    QJsonArray array;
    for (const QVariant &value : entries) {
        const QVariantMap entry = value.toMap();
        QJsonObject object;
        object[QStringLiteral("path")] = entry.value(QStringLiteral("path")).toString();
        object[QStringLiteral("page")] = entry.value(QStringLiteral("page")).toInt();
        object[QStringLiteral("opened")] = entry.value(QStringLiteral("opened")).toString();
        array.append(object);
    }
    m_settings.setValue(QLatin1String(kRecentKey),
                        QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    emit recentFilesChanged();
}

QVariantList Settings::recentFiles() const
{
    return loadRecent();
}

void Settings::noteOpened(const QString &path)
{
    if (path.isEmpty())
        return;

    const QString canonical = QFileInfo(path).absoluteFilePath();

    QVariantList entries = loadRecent();
    int existingPage = 0;
    for (int i = entries.size() - 1; i >= 0; --i) {
        if (entries.at(i).toMap().value(QStringLiteral("path")).toString() == canonical) {
            // Reopening must not lose the reading position that was already
            // recorded for this file.
            existingPage = entries.at(i).toMap().value(QStringLiteral("page")).toInt();
            entries.removeAt(i);
        }
    }

    QVariantMap entry;
    entry[QStringLiteral("path")] = canonical;
    entry[QStringLiteral("page")] = existingPage;
    entry[QStringLiteral("opened")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    entries.prepend(entry);

    while (entries.size() > kMaxRecent)
        entries.removeLast();

    storeRecent(entries);
}

void Settings::notePosition(const QString &path, int pageIndex)
{
    if (path.isEmpty() || pageIndex < 0)
        return;

    const QString canonical = QFileInfo(path).absoluteFilePath();

    QVariantList entries = loadRecent();
    for (int i = 0; i < entries.size(); ++i) {
        QVariantMap entry = entries.at(i).toMap();
        if (entry.value(QStringLiteral("path")).toString() != canonical)
            continue;
        if (entry.value(QStringLiteral("page")).toInt() == pageIndex)
            return; // Nothing changed; do not rewrite the file on every scroll.
        entry[QStringLiteral("page")] = pageIndex;
        entries[i] = entry;
        storeRecent(entries);
        return;
    }
}

int Settings::positionFor(const QString &path) const
{
    if (!restorePosition() || path.isEmpty())
        return 0;

    const QString canonical = QFileInfo(path).absoluteFilePath();
    const QVariantList entries = loadRecent();
    for (const QVariant &value : entries) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("path")).toString() == canonical)
            return entry.value(QStringLiteral("page")).toInt();
    }
    return 0;
}

void Settings::removeRecent(const QString &path)
{
    const QString canonical = QFileInfo(path).absoluteFilePath();
    QVariantList entries = loadRecent();
    const qsizetype before = entries.size();
    for (int i = entries.size() - 1; i >= 0; --i) {
        if (entries.at(i).toMap().value(QStringLiteral("path")).toString() == canonical)
            entries.removeAt(i);
    }
    if (entries.size() != before)
        storeRecent(entries);
}

void Settings::clearRecent()
{
    m_settings.remove(QLatin1String(kRecentKey));
    emit recentFilesChanged();
}

void Settings::resetAll()
{
    m_settings.clear();
    m_settings.sync();

    emit themeChanged();
    emit languageChanged();
    emit zoomModeChanged();
    emit sidebarVisibleChanged();
    emit sidebarTabChanged();
    emit restorePositionChanged();
    emit checkForUpdatesChanged();
    emit windowSizeChanged();
    emit windowMaximizedChanged();
    emit recentFilesChanged();
}

} // namespace lumen
