#ifndef PARTS_WIDGET_SCROLL_GEOMETRY_H
#define PARTS_WIDGET_SCROLL_GEOMETRY_H

#include <stdbool.h>
#include <stdint.h>

struct widget_scroll_geometry {
    int previous, next, track, thumb, length, start, travel;
};

static inline struct widget_scroll_geometry widget_scroll_layout(
        int length, int previous, int next, int total, int view, int pos,
        int intrinsic, bool stretch)
{
    struct widget_scroll_geometry g;

    if (length < 0) length = 0;
    if (length > 4096) length = 4096;

    if (previous < 0) previous = 0;
    if (previous > length / 2) previous = length / 2;

    if (next < 0) next = 0;
    if (next > length - previous) next = length - previous;

    g.length = length;
    g.previous = previous;
    g.next = next;
    g.track = length - previous - next;

    if (intrinsic <= 0) intrinsic = 12;

    if (total <= 0) {
        view = 0;
        g.thumb = g.track;
        g.travel = 0;
        g.start = previous;
        return g;
    }

    if (view < 0) view = 0;
    if (view > total) view = total;

    if (view >= total) {
        g.thumb = g.track;
        g.travel = 0;
        g.start = previous;
        return g;
    }

    int thumb = intrinsic;
    if (stretch) {
        int64_t scaled = (int64_t)g.track * view / total;
        if (scaled > thumb) thumb = (int)scaled;
    }
    if (thumb < 0) thumb = 0;
    if (thumb > g.track) thumb = g.track;
    g.thumb = thumb;
    g.travel = g.track - thumb;

    int limit = total - view;
    if (limit < 0) limit = 0;
    if (pos < 0) pos = 0;
    if (pos > limit) pos = limit;

    if (limit == 0 || g.travel == 0) {
        g.start = previous;
    } else {
        int64_t numerator = (int64_t)g.travel * pos;
        int offset = (int)((numerator + limit / 2) / limit);
        g.start = previous + offset;
    }
    return g;
}

#endif /* PARTS_WIDGET_SCROLL_GEOMETRY_H */
