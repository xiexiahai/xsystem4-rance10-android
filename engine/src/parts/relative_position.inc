#ifndef PARTS_RELATIVE_POSITION_INC
#define PARTS_RELATIVE_POSITION_INC

#include <stdbool.h>
#include <limits.h>
#include <math.h>

/* Project-selected pixel/ratio coordinate contract.  The struct stores only
 * the raw ratio; resolution never mutates it or accumulates across calls. */
struct parts_relative_position {
	bool enabled;
	double x, y;
};

static bool parts_relative_trunc_to_int(double value, int *out)
{
	if (!isfinite(value))
		return false;
	double truncated = trunc(value);
	if (truncated < (double)INT_MIN || truncated > (double)INT_MAX)
		return false;
	*out = (int)truncated;
	return true;
}

static bool parts_relative_raw_in_int_range(double value)
{
	return isfinite(value) && value >= (double)INT_MIN && value <= (double)INT_MAX;
}

static bool parts_relative_resolve(const struct parts_relative_position *position,
		int basis_w, int basis_h, int *out_x, int *out_y)
{
	if (!position || !out_x || !out_y)
		return false;
	if (basis_w < 0 || basis_w > 32768 || basis_h < 0 || basis_h > 32768)
		return false;
	if (!isfinite(position->x) || !isfinite(position->y))
		return false;

	double scaled_x = position->x;
	double scaled_y = position->y;
	if (position->enabled) {
		scaled_x *= (double)basis_w;
		scaled_y *= (double)basis_h;
		if (!isfinite(scaled_x) || !isfinite(scaled_y))
			return false;
	}

	int x, y;
	if (!parts_relative_trunc_to_int(scaled_x, &x) ||
			!parts_relative_trunc_to_int(scaled_y, &y))
		return false;

	*out_x = x;
	*out_y = y;
	return true;
}

static bool parts_relative_set(struct parts_relative_position *position,
		bool enabled, double x, double y)
{
	if (!position || !isfinite(x) || !isfinite(y))
		return false;
	/* Disabled/pixel mode stores a directly usable pixel; enabled mode may
	 * store any finite ratio, with the scaled result checked by resolve. */
	if (!enabled &&
			(!parts_relative_raw_in_int_range(x) ||
			 !parts_relative_raw_in_int_range(y)))
		return false;

	position->enabled = enabled;
	position->x = x;
	position->y = y;
	return true;
}

#endif /* PARTS_RELATIVE_POSITION_INC */
