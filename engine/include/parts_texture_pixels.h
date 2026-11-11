/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PARTS_TEXTURE_PIXELS_H
#define PARTS_TEXTURE_PIXELS_H
#include <stdint.h>
struct texture;
struct parts;
struct parts_texture_pixels {
 int w,h,min_filter,mag_filter,wrap_s,wrap_t;
 uint8_t *rgba;
};
/* -1 capture error, 0 no current image, 1 caller-owned final RGBA.
 * Does not resume a parked owner or change its ownership/accounting. */
int parts_copy_texture_pixels(const struct parts *,const struct texture *,struct parts_texture_pixels *);
#endif
