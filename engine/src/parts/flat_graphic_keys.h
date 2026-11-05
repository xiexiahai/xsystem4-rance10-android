/* Copyright (C) 2026 xsystem4 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PARTS_FLAT_GRAPHIC_KEYS_H
#define PARTS_FLAT_GRAPHIC_KEYS_H
#include "system4/flat.h"

/* Return a borrowed span for one local frame. v15+ stores zero or more
 * instances per frame; the legacy count describes one key per frame. */
static inline size_t parts_flat_graphic_keys(const struct flat_timeline *tl,
        int version, int local, const struct flat_key_data_graphic **keys)
{
    if (!keys) return 0;
    *keys = NULL;
    if (!tl || tl->type != FLAT_TIMELINE_GRAPHIC || local < 0 || local >= tl->frame_count)
        return 0;
    if (version >= 15) {
        if (!tl->graphic.frames) return 0;
        const struct flat_key_frame_graphic *frame = &tl->graphic.frames[local];
        if (!frame->count || !frame->keys) return 0;
        *keys = frame->keys;
        return frame->count;
    }
    if (!tl->graphic.keys || (size_t)local >= tl->graphic.count) return 0;
    *keys = &tl->graphic.keys[local];
    return 1;
}
#endif
