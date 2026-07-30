#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QPair>
#include <QString>

namespace lumen {

// Startup milestones, measured from the first line of main().
//
// "Fast" is the product's first claim, so it has to be a measured number rather
// than an impression. What matters to a user is time-to-first-page: the moment
// they can read something, not the moment the process exists. The milestones in
// between are what tell you which part to fix when that number is bad.
class Timing {
public:
    static Timing &instance();

    // Call once, as early in main() as possible.
    void start();

    void mark(const QString &milestone);

    qint64 elapsedMs() const;
    const QList<QPair<QString, qint64>> &milestones() const { return m_milestones; }

    bool isRunning() const { return m_timer.isValid(); }

private:
    QElapsedTimer m_timer;
    QList<QPair<QString, qint64>> m_milestones;
};

} // namespace lumen
