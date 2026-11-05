#ifndef PARTS_GAUGE_LAYOUT_INC
#define PARTS_GAUGE_LAYOUT_INC

#include <math.h>
#include <stdbool.h>

/*
 * Horizontal gauge window for the selected project contract:
 *   reverse=false: keep the left  floor(rate*width) source pixels
 *   reverse=true : keep the right floor(rate*width) source pixels
 * The kept region is copied source-x -> destination-x without flipping.
 * rate 0 keeps nothing (all transparent); rate 1 keeps the whole image.
 */
static bool parts_hgauge_window(int width, float rate, bool reverse,
		int *x, int *pixels)
{
	if (width < 1 || width > 4096)
		return false;
	if (!isfinite(rate) || rate < 0.0f || rate > 1.0f)
		return false;

	int count = (int)(rate * (float)width);
	if (count < 0)
		count = 0;
	else if (count > width)
		count = width;
	int offset = reverse ? width - count : 0;

	if (x)
		*x = offset;
	if (pixels)
		*pixels = count;
	return true;
}

#endif /* PARTS_GAUGE_LAYOUT_INC */
