/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PARTS_TEXTBOX_INTERNAL_H
#define PARTS_TEXTBOX_INTERNAL_H
#include "parts_internal.h"
#include "textbox_utf8.h"
struct parts_textbox {
    int owner_no;
    struct textbox_utf8 buffer;
    bool readonly,show_h,show_v,keep_caret_visible;
    int padding[4]; /* top, bottom, left, right */
    int scroll_x,scroll_y;
    SDL_Color bg,readonly_bg,frame,selection;
    char editing[SDL_TEXTEDITINGEVENT_TEXT_SIZE];
    int editing_start,editing_length,drag_axis,drag_offset;
    unsigned blink_ms;
    /* Background plus two axes (thumb/previous/next x four states + track).
     * Derived from widget.cg_name; the existing name is the save contract. */
    Texture skin[27];
};
#endif
