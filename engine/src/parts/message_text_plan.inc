#ifndef MESSAGE_TEXT_PLAN_INC
#define MESSAGE_TEXT_PLAN_INC

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#ifndef MW_TEXT_MALLOC
#define MW_TEXT_MALLOC(size) malloc(size)
#endif
#ifndef MW_TEXT_REALLOC
#define MW_TEXT_REALLOC(ptr, size) realloc((ptr), (size))
#endif
#ifndef MW_TEXT_FREE
#define MW_TEXT_FREE(ptr) free(ptr)
#endif

struct mw_text_style {
	int face;
	float size, bold, edge;
	int r, g, b;
	int er, eg, eb;
	int tracking, leading;
	double delay_ms;
};

struct mw_text_token {
	char bytes[3];
	unsigned source_offset;
	unsigned byte_length;
	bool newline;
	struct mw_text_style style;
};

struct mw_text_plan {
	struct mw_text_token *tokens;
	size_t count;
	size_t glyph_count;
};

static bool mw_text_valid_style(const struct mw_text_style *style)
{
	if (!style)
		return false;
	if (style->face < 0 || style->face > 65535)
		return false;
	if (!isfinite(style->size) || style->size < 1.0f || style->size > 512.0f)
		return false;
	if (!isfinite(style->bold) || style->bold < 0.0f || style->bold > 16.0f)
		return false;
	if (!isfinite(style->edge) || style->edge < 0.0f || style->edge > 16.0f)
		return false;
	if (style->r < 0 || style->r > 255 || style->g < 0 || style->g > 255 ||
			style->b < 0 || style->b > 255)
		return false;
	if (style->er < 0 || style->er > 255 || style->eg < 0 || style->eg > 255 ||
			style->eb < 0 || style->eb > 255)
		return false;
	if (style->tracking < -512 || style->tracking > 512 ||
			style->leading < -512 || style->leading > 512)
		return false;
	if (!isfinite(style->delay_ms) || style->delay_ms < 0.0 ||
			style->delay_ms > 60000.0)
		return false;
	return true;
}

static bool mw_text_parse_int(const char *s, size_t n, int minimum, int maximum, int *out)
{
	if (!s || n == 0 || !out)
		return false;
	size_t i = 0;
	bool negative = false;
	if (s[i] == '+' || s[i] == '-') {
		negative = s[i] == '-';
		i++;
	}
	if (i == n)
		return false;
	int64_t value = 0;
	for (; i < n; i++) {
		if (s[i] < '0' || s[i] > '9')
			return false;
		value = value * 10 + (s[i] - '0');
		if (value > 1000000000)
			return false;
	}
	if (negative)
		value = -value;
	if (value < minimum || value > maximum)
		return false;
	*out = (int)value;
	return true;
}

static bool mw_text_parse_float(const char *s, size_t n, double minimum,
		double maximum, double *out)
{
	if (!s || n == 0 || !out)
		return false;
	size_t i = 0;
	bool negative = false;
	if (s[i] == '+' || s[i] == '-') {
		negative = s[i] == '-';
		i++;
	}
	if (i == n)
		return false;
	bool any_digit = false, dot = false;
	double value = 0.0, fraction = 0.1;
	for (; i < n; i++) {
		if (s[i] >= '0' && s[i] <= '9') {
			any_digit = true;
			if (!dot)
				value = value * 10.0 + (s[i] - '0');
			else {
				value += (s[i] - '0') * fraction;
				fraction *= 0.1;
			}
		} else if (s[i] == '.' && !dot) {
			dot = true;
		} else {
			return false;
		}
	}
	if (!any_digit || !isfinite(value))
		return false;
	if (negative)
		value = -value;
	if (!isfinite(value) || value < minimum || value > maximum)
		return false;
	*out = value;
	return true;
}

static bool mw_text_key_is(const char *key, size_t n, const char *name)
{
	return strlen(name) == n && !memcmp(key, name, n);
}

static bool mw_text_apply_font_attr(const char *key, size_t key_len,
		const char *value, size_t value_len, struct mw_text_style *style)
{
	if (mw_text_key_is(key, key_len, "type")) {
		int v;
		if (!mw_text_parse_int(value, value_len, 0, 65535, &v))
			return false;
		style->face = v;
	} else if (mw_text_key_is(key, key_len, "size")) {
		int v;
		if (!mw_text_parse_int(value, value_len, 1, 512, &v))
			return false;
		style->size = (float)v;
	} else if (mw_text_key_is(key, key_len, "r") ||
			mw_text_key_is(key, key_len, "g") ||
			mw_text_key_is(key, key_len, "b") ||
			mw_text_key_is(key, key_len, "er") ||
			mw_text_key_is(key, key_len, "eg") ||
			mw_text_key_is(key, key_len, "eb")) {
		int v;
		if (!mw_text_parse_int(value, value_len, 0, 255, &v))
			return false;
		if (key[0] == 'r' && key_len == 1) style->r = v;
		else if (key[0] == 'g' && key_len == 1) style->g = v;
		else if (key[0] == 'b' && key_len == 1) style->b = v;
		else if (key[0] == 'e' && key_len == 2 && key[1] == 'r') style->er = v;
		else if (key[0] == 'e' && key_len == 2 && key[1] == 'g') style->eg = v;
		else if (key[0] == 'e' && key_len == 2 && key[1] == 'b') style->eb = v;
		else return false;
	} else if (mw_text_key_is(key, key_len, "bold") ||
			mw_text_key_is(key, key_len, "edge")) {
		double v;
		if (!mw_text_parse_float(value, value_len, 0.0, 16.0, &v))
			return false;
		if (key[0] == 'b') style->bold = (float)v;
		else style->edge = (float)v;
	} else if (mw_text_key_is(key, key_len, "tracking") ||
			mw_text_key_is(key, key_len, "leading")) {
		int v;
		if (!mw_text_parse_int(value, value_len, -512, 512, &v))
			return false;
		if (key[0] == 't') style->tracking = v;
		else style->leading = v;
	} else {
		return false;
	}
	return true;
}

static bool mw_text_parse_font_attrs(const char *s, size_t n,
		struct mw_text_style *style)
{
	size_t i = 0;
	while (i < n) {
		while (i < n && s[i] == ' ')
			i++;
		if (i == n)
			break;
		size_t key_start = i;
		while (i < n && s[i] != '=' && s[i] != ' ')
			i++;
		if (i == n || s[i] != '=' || i == key_start)
			return false;
		size_t key_end = i++;
		size_t value_start = i;
		while (i < n && s[i] != ' ')
			i++;
		if (i == value_start)
			return false;
		if (!mw_text_apply_font_attr(s + key_start, key_end - key_start,
				s + value_start, i - value_start, style))
			return false;
	}
	return true;
}

static bool mw_text_parse_time_value(const char *s, size_t n, int *delay)
{
	size_t start = 0, end = n;
	while (start < end && s[start] == ' ')
		start++;
	while (end > start && s[end - 1] == ' ')
		end--;
	return mw_text_parse_int(s + start, end - start, 0, 60000, delay);
}

static bool mw_text_token_reserve(struct mw_text_plan *plan, size_t *capacity)
{
	if (plan->count >= 8192)
		return false;
	if (plan->count < *capacity)
		return true;
	size_t next = *capacity ? *capacity * 2 : 64;
	if (next > 8192)
		next = 8192;
	if (next <= *capacity)
		return false;
	struct mw_text_token *tokens;
	if (!*capacity)
		tokens = MW_TEXT_MALLOC(next * sizeof(*tokens));
	else
		tokens = MW_TEXT_REALLOC(plan->tokens, next * sizeof(*tokens));
	if (!tokens)
		return false;
	plan->tokens = tokens;
	*capacity = next;
	return true;
}

static bool mw_text_add_glyph(struct mw_text_plan *plan, size_t *capacity,
		const unsigned char *bytes, size_t length, size_t source_offset,
		const struct mw_text_style *style)
{
	if (!mw_text_token_reserve(plan, capacity))
		return false;
	struct mw_text_token *token = &plan->tokens[plan->count++];
	memset(token, 0, sizeof(*token));
	memcpy(token->bytes, bytes, length);
	token->source_offset = (unsigned)source_offset;
	token->byte_length = (unsigned)length;
	token->newline = false;
	token->style = *style;
	plan->glyph_count++;
	return true;
}

static bool mw_text_add_newline(struct mw_text_plan *plan, size_t *capacity,
		size_t source_offset, const struct mw_text_style *style)
{
	if (!mw_text_token_reserve(plan, capacity))
		return false;
	struct mw_text_token *token = &plan->tokens[plan->count++];
	memset(token, 0, sizeof(*token));
	token->bytes[0] = '\n';
	token->source_offset = (unsigned)source_offset;
	token->byte_length = 1;
	token->newline = true;
	token->style = *style;
	token->style.delay_ms = 0.0;
	return true;
}

static bool mw_text_is_gbk_lead(unsigned char b)
{
	return b >= 0x81 && b <= 0xfe;
}

static bool mw_text_is_gbk_trail(unsigned char b)
{
	return b >= 0x40 && b <= 0xfe && b != 0x7f;
}

static bool mw_text_is_sjis_lead(unsigned char b)
{
	return (b >= 0x81 && b <= 0x9f) || (b >= 0xe0 && b <= 0xfc);
}

static bool mw_text_is_sjis_trail(unsigned char b)
{
	return b >= 0x40 && b <= 0xfc && b != 0x7f;
}

static bool mw_text_is_sjis_single(unsigned char b)
{
	return b <= 0x7f || (b >= 0xa1 && b <= 0xdf);
}

static bool mw_text_control(const char *content, size_t n,
		struct mw_text_style *style, struct mw_text_style font_stack[32],
		int *font_depth, double time_stack[32], int *time_depth)
{
	if (n == 5 && !memcmp(content, "/font", 5)) {
		if (*font_depth == 0)
			return false;
		double current_delay=style->delay_ms;
		*style = font_stack[--*font_depth];
		style->delay_ms=current_delay;
		return true;
	}
	if (n == 5 && !memcmp(content, "/time", 5)) {
		if (*time_depth == 0)
			return false;
		style->delay_ms = time_stack[--*time_depth];
		return true;
	}
	if (n >= 4 && !memcmp(content, "font", 4) && (n == 4 || content[4] == ' ')) {
		if (*font_depth >= 32)
			return false;
		font_stack[(*font_depth)++] = *style;
		return mw_text_parse_font_attrs(content + 4, n - 4, style);
	}
	if (n >= 4 && !memcmp(content, "time", 4) && n > 4 && content[4] == ' ') {
		if (*time_depth >= 32)
			return false;
		int delay;
		if (!mw_text_parse_time_value(content + 4, n - 4, &delay))
			return false;
		time_stack[(*time_depth)++] = style->delay_ms;
		style->delay_ms = (double)delay;
		return true;
	}
	return false;
}

static void mw_text_plan_free(struct mw_text_plan *plan)
{
	if (!plan)
		return;
	if (plan->tokens)
		MW_TEXT_FREE(plan->tokens);
	plan->tokens = NULL;
	plan->count = 0;
	plan->glyph_count = 0;
}

static bool mw_text_parse(const char *bytes, size_t length, bool gbk,
		const struct mw_text_style *base, struct mw_text_plan *inout)
{
	if (!bytes || !base || !inout)
		return false;
	if (length > 1024 * 1024)
		return false;
	if (!mw_text_valid_style(base))
		return false;
	if (memchr(bytes, 0, length))
		return false;

	struct mw_text_plan candidate = {0};
	size_t capacity = 0;
	struct mw_text_style current = *base;
	struct mw_text_style font_stack[32];
	double time_stack[32];
	int font_depth = 0, time_depth = 0;
	size_t i = 0;

	while (i < length) {
		unsigned char b = (unsigned char)bytes[i];
		if (b == '$' && i + 1 < length && bytes[i + 1] == '{') {
			size_t close = i + 2;
			while (close < length && bytes[close] != '}')
				close++;
			if (close == length)
				goto fail;
			const char *content = bytes + i + 2;
			size_t content_len = close - i - 2;
			if (!mw_text_control(content, content_len, &current, font_stack,
					&font_depth, time_stack, &time_depth))
				goto fail;
			i = close + 1;
			continue;
		}

		if (b == '\r') {
			size_t consumed = (i + 1 < length && bytes[i + 1] == '\n') ? 2 : 1;
			if (!mw_text_add_newline(&candidate, &capacity, i, &current))
				goto fail;
			i += consumed;
			continue;
		}
		if (b == '\n') {
			if (!mw_text_add_newline(&candidate, &capacity, i, &current))
				goto fail;
			i++;
			continue;
		}

		size_t glyph_len;
		if (gbk) {
			if (b < 0x80) {
				glyph_len = 1;
			} else if (mw_text_is_gbk_lead(b) && i + 1 < length &&
					mw_text_is_gbk_trail((unsigned char)bytes[i + 1])) {
				glyph_len = 2;
			} else {
				goto fail;
			}
		} else {
			if (mw_text_is_sjis_single(b)) {
				glyph_len = 1;
			} else if (mw_text_is_sjis_lead(b) && i + 1 < length &&
					mw_text_is_sjis_trail((unsigned char)bytes[i + 1])) {
				glyph_len = 2;
			} else {
				goto fail;
			}
		}
		if (!mw_text_add_glyph(&candidate, &capacity,
				(const unsigned char *)bytes + i, glyph_len, i, &current))
			goto fail;
		i += glyph_len;
	}

	if (font_depth != 0 || time_depth != 0)
		goto fail;

	mw_text_plan_free(inout);
	*inout = candidate;
	return true;

fail:
	mw_text_plan_free(&candidate);
	return false;
}

#endif /* MESSAGE_TEXT_PLAN_INC */
