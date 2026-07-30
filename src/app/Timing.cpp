#include "app/Timing.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcTiming, "lumen.timing")

namespace lumen {

Timing &Timing::instance()
{
    static Timing timing;
    return timing;
}

void Timing::start()
{
    m_timer.start();
    m_milestones.clear();
}

void Timing::mark(const QString &milestone)
{
    if (!m_timer.isValid())
        return;

    const qint64 at = m_timer.elapsed();
    m_milestones.append({ milestone, at });

    // Logged as it happens rather than only in the summary: if startup hangs,
    // the last line printed is the phase that hung.
    qCInfo(lcTiming) << milestone << at << "ms";
}

qint64 Timing::elapsedMs() const
{
    return m_timer.isValid() ? m_timer.elapsed() : 0;
}

} // namespace lumen
