#ifndef PARTS_NUMERAL_LAYOUT_INC
#define PARTS_NUMERAL_LAYOUT_INC

#include <stdbool.h>
#include <stdint.h>

/*
 * Left-to-right numeral glyph slots:
 *   0..9 digits, 10 minus, 11 comma.
 *
 * `min_digits` is the total digit field width (sign excluded). Values <= 1
 * are treated as 1; values > 24 are rejected without touching the outputs.
 * Padding zeros are placed after the sign and before the actual digits, and
 * commas only group the actual digits, matching the old padding/comma layout.
 */
static bool parts_numeral_build_chars(int number, int min_digits, bool comma,
		uint8_t chars[32], int *count)
{
	if (min_digits > 24)
		return false;
	if (min_digits < 1)
		min_digits = 1;

	int64_t n = number;
	bool negative = n < 0;
	if (negative)
		n = -n;

	uint8_t digits[24];
	int nr_digits = 0;
	do {
		digits[nr_digits++] = (uint8_t)(n % 10);
		n /= 10;
	} while (n && nr_digits < 24);

	int nr_chars = 0;
	if (negative)
		chars[nr_chars++] = 10;
	for (int i = nr_digits; i < min_digits; i++)
		chars[nr_chars++] = 0;
	for (int i = 0; i < nr_digits; i++) {
		int k = i; /* left-to-right digit position */
		if (comma && k > 0 && ((nr_digits - k) % 3) == 0)
			chars[nr_chars++] = 11;
		chars[nr_chars++] = digits[nr_digits - 1 - i];
	}
	if (count)
		*count = nr_chars;
	return true;
}

/*
 * Combined-font cache equality. All twelve declared widths must match; a
 * single differing slot (including the optional minus/comma tail) is enough
 * to reject cache reuse.
 */
static bool parts_numeral_widths_equal(const int a[12], const int b[12])
{
	for (int i = 0; i < 12; i++)
		if (a[i] != b[i])
			return false;
	return true;
}

#endif /* PARTS_NUMERAL_LAYOUT_INC */
