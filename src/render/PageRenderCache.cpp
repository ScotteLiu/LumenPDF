#include "render/PageRenderCache.h"

#include <QMutexLocker>

namespace lumen {

PageRenderCache::PageRenderCache(int budgetMegabytes)
{
    // QCache costs are ints, so track kilobytes to stay well clear of overflow.
    m_cache.setMaxCost(budgetMegabytes * 1024);
}

quint64 PageRenderCache::keyFor(int pageIndex, int width)
{
    return (quint64(quint32(pageIndex)) << 32) | quint32(width);
}

QImage PageRenderCache::take(int pageIndex, int width) const
{
    QMutexLocker locker(&m_mutex);
    if (const QImage *hit = m_cache.object(keyFor(pageIndex, width)))
        return *hit;
    return {};
}

void PageRenderCache::insert(int pageIndex, int width, const QImage &image)
{
    if (image.isNull())
        return;

    const int costKb = qMax(1, int(image.sizeInBytes() / 1024));

    QMutexLocker locker(&m_mutex);
    m_cache.insert(keyFor(pageIndex, width), new QImage(image), costKb);
}

void PageRenderCache::clear()
{
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
}

} // namespace lumen
