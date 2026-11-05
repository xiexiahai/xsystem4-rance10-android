#ifndef AIN12_STRING_REPLACE_INC
#define AIN12_STRING_REPLACE_INC

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "system4/string.h"
#include "system4/utfsjis.h"

#define AIN12_STRING_REPLACE_MAX (16u * 1024u * 1024u)

/* Loose byte/character step used by the existing engine.  This is not a
 * validator: an isolated lead at the end is one byte, and a GBK lead range is
 * 0x81..0xfe.  UTF-8 is deliberately not assumed. */
static size_t ain12_string_replace_step(const struct string *s, size_t at)
{
	if (at >= (size_t)s->size)
		return 1;
	uint8_t b = (uint8_t)s->text[at];
	if (sys_text_is_gbk) {
		if (b >= 0x81 && b <= 0xfe && at + 1 < (size_t)s->size)
			return 2;
	} else if (SJIS_2BYTE(b) && at + 1 < (size_t)s->size) {
		return 2;
	}
	return 1;
}

static bool ain12_string_replace_boundary(const struct string *s,
		size_t start, size_t end)
{
	size_t at = start;
	while (at < end) {
		size_t step = ain12_string_replace_step(s, at);
		if (step > end - at)
			return false;
		at += step;
	}
	return at == end;
}

static bool ain12_string_replace_match(const struct string *s, size_t at,
		const struct string *key)
{
	if (key->size <= 0)
		return false;
	size_t length = (size_t)key->size;
	if (length > (size_t)s->size - at)
		return false;
	if (memcmp(s->text + at, key->text, length))
		return false;
	return ain12_string_replace_boundary(s, at, at + length);
}

static struct string *ain12_string_replace_copy(const struct string *s)
{
	return s->size ? string_dup(s) : string_ref(&EMPTY_STRING);
}

/* Return an owned replacement string.  Inputs are borrowed and never changed;
 * replacement bytes are never rescanned.  NULL/negative/oversized inputs and
 * an output over 16MiB return NULL before allocation. */
static struct string *ain12_string_replace_all(const struct string *subject,
		const struct string *key, const struct string *replacement)
{
	if (!subject || !key || !replacement)
		return NULL;
	if (subject->size < 0 || key->size < 0 || replacement->size < 0)
		return NULL;

	size_t s = (size_t)subject->size;
	size_t k = (size_t)key->size;
	size_t r = (size_t)replacement->size;
	if (s > AIN12_STRING_REPLACE_MAX || k > AIN12_STRING_REPLACE_MAX ||
			r > AIN12_STRING_REPLACE_MAX)
		return NULL;
	if (s == 0 || k == 0 || k > s)
		return ain12_string_replace_copy(subject);

	size_t result = 0, matches = 0, at = 0;
	while (at < s) {
		if (ain12_string_replace_match(subject, at, key)) {
			if (r > AIN12_STRING_REPLACE_MAX - result)
				return NULL;
			result += r;
			matches++;
			at += k;
		} else {
			size_t step = ain12_string_replace_step(subject, at);
			if (step > AIN12_STRING_REPLACE_MAX - result)
				return NULL;
			result += step;
			at += step;
		}
	}
	if (!matches)
		return ain12_string_replace_copy(subject);
	if (result == 0)
		return string_ref(&EMPTY_STRING);

	struct string *out = string_alloc((unsigned)result);
	size_t src = 0, dst = 0;
	while (src < s) {
		if (ain12_string_replace_match(subject, src, key)) {
			if (r)
				memcpy(out->text + dst, replacement->text, r);
			dst += r;
			src += k;
		} else {
			size_t step = ain12_string_replace_step(subject, src);
			memcpy(out->text + dst, subject->text + src, step);
			dst += step;
			src += step;
		}
	}
	if (dst != result) {
		free_string(out);
		return NULL;
	}
	out->text[dst] = '\0';
	return out;
}

#endif /* AIN12_STRING_REPLACE_INC */
