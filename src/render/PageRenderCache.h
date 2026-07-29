#pragma once

#include <QCache>
#include <QImage>
#include <QMutex>

namespace lumen {

// LRU cache of rasterised pages, budgeted in bytes rather than entry count so
// a handful of huge pages cannot blow past the memory target.
//
// Keyed on (page, rendered width): scrolling at a fixed zoom hits the cache,
// zooming misses it and re-renders, which is the behaviour we want.
class PageRenderCache {
public:
    explicit PageRenderCache(int budgetMegabytes = 192);

    QImage take(int pageIndex, int width) const;
    void insert(int pageIndex, int width, const QImage &image);
    void clear();

private:
    static quint64 keyFor(int pageIndex, int width);

    mutable QMutex m_mutex;
    mutable QCache<quint64, QImage> m_cache;
};

} // namespace lumen
