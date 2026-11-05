#ifndef AIN12_STRING_REGEX_INC
#define AIN12_STRING_REGEX_INC

#include "ain12_string_replace.inc"

/* Deliberately bounded character-aware subset, not a general regex engine.
 * Atoms: literal characters, escaped ASCII punctuation, dot, lazy .+?.
 * Replacement formats: ordinary literals, $$ and $&. Captures, prefix/suffix
 * expansions, flags, classes, anchors and other quantifiers fail explicitly.
 * Inputs are borrowed; the returned string owns its storage/reference. */
#define AIN12_REGEX_TOKENS 128
#define AIN12_REGEX_STEPS 2000000u
enum ain12_regex_error {
	AIN12_RX_OK, AIN12_RX_INVALID, AIN12_RX_UNSUPPORTED, AIN12_RX_LIMIT
};
enum ain12_regex_atom { AIN12_RX_LITERAL, AIN12_RX_DOT, AIN12_RX_LAZY_PLUS };
struct ain12_regex_token { enum ain12_regex_atom kind; unsigned length; char bytes[2]; };
struct ain12_regex {
	struct ain12_regex_token tokens[AIN12_REGEX_TOKENS];
	unsigned count, steps;
	const struct string *subject;
	enum ain12_regex_error error;
};

static bool ain12_regex_compile(struct ain12_regex *rx, const struct string *pattern)
{
	if (!pattern->size) { rx->error = AIN12_RX_UNSUPPORTED; return false; }
	for (size_t at = 0; at < (size_t)pattern->size;) {
		if (rx->count == AIN12_REGEX_TOKENS) { rx->error = AIN12_RX_LIMIT; return false; }
		struct ain12_regex_token t = { .kind = AIN12_RX_LITERAL };
		size_t step = ain12_string_replace_step(pattern, at);
		unsigned char c = pattern->text[at];
		if (step == 1 && c == '\\') {
			if (++at == (size_t)pattern->size ||
				ain12_string_replace_step(pattern, at) != 1 || !pattern->text[at] ||
				!strchr("\\.^$|?*+()[]{}", pattern->text[at])) {
				rx->error = AIN12_RX_UNSUPPORTED; return false;
			}
			t.length = 1; t.bytes[0] = pattern->text[at++];
		} else if (step == 1 && c == '.') {
			t.kind = AIN12_RX_DOT; at++;
			if (at + 1 < (size_t)pattern->size && pattern->text[at] == '+' && pattern->text[at+1] == '?') {
				t.kind = AIN12_RX_LAZY_PLUS; at += 2;
			}
		} else {
			if (step == 1 && c && strchr("[](){}^$|*+?", c)) {
				rx->error = AIN12_RX_UNSUPPORTED; return false;
			}
			t.length = step; memcpy(t.bytes, pattern->text + at, step); at += step;
		}
		rx->tokens[rx->count++] = t;
	}
	return true;
}

static bool ain12_regex_tick(struct ain12_regex *rx)
{
	if (!rx->steps) { rx->error = AIN12_RX_LIMIT; return false; }
	rx->steps--; return true;
}

static bool ain12_regex_dot(const struct string *s, size_t at)
{
	return at < (size_t)s->size && s->text[at] != '\r' && s->text[at] != '\n';
}

static bool ain12_regex_match(struct ain12_regex *rx, unsigned token, size_t at, size_t *end)
{
	if (!ain12_regex_tick(rx)) return false;
	if (token == rx->count) { *end = at; return true; }
	struct ain12_regex_token *t = &rx->tokens[token];
	const struct string *s = rx->subject;
	if (t->kind == AIN12_RX_LITERAL) {
		if (at >= (size_t)s->size || t->length > (size_t)s->size - at ||
			ain12_string_replace_step(s, at) != t->length || memcmp(s->text + at, t->bytes, t->length))
			return false;
		return ain12_regex_match(rx, token+1, at+t->length, end);
	}
	if (!ain12_regex_dot(s, at)) return false;
	size_t next = at + ain12_string_replace_step(s, at);
	if (t->kind == AIN12_RX_DOT) return ain12_regex_match(rx, token+1, next, end);
	for (;;) {
		if (ain12_regex_match(rx, token+1, next, end)) return true;
		if (rx->error || !ain12_regex_dot(s, next)) return false;
		if (!ain12_regex_tick(rx)) return false;
		next += ain12_string_replace_step(s, next);
	}
}

static bool ain12_regex_replacement(const struct string *r, bool *formatted)
{
	*formatted = false;
	for (size_t at = 0; at < (size_t)r->size;) {
		size_t step = ain12_string_replace_step(r, at);
		if (step == 1 && r->text[at] == '$' && at+1 < (size_t)r->size) {
			unsigned char c = r->text[at+1];
			if ((c >= '0' && c <= '9') || c == '{' || c == '`' || c == '\'') return false;
			if (c == '$' || c == '&') { *formatted = true; at += 2; continue; }
		}
		at += step;
	}
	return true;
}

static bool ain12_regex_emit(struct ain12_regex *rx, struct string *out, size_t *used,
		const char *bytes, size_t length)
{
	if (length > AIN12_STRING_REPLACE_MAX - *used) { rx->error = AIN12_RX_LIMIT; return false; }
	if (out && length) memcpy(out->text + *used, bytes, length);
	*used += length; return true;
}

static bool ain12_regex_emit_replacement(struct ain12_regex *rx, struct string *out,
		size_t *used, const struct string *r, bool formatted, size_t start, size_t end)
{
	if (!formatted) return ain12_regex_emit(rx, out, used, r->text, r->size);
	for (size_t at = 0; at < (size_t)r->size;) {
		size_t step = ain12_string_replace_step(r, at);
		if (step == 1 && r->text[at] == '$' && at+1 < (size_t)r->size) {
			if (r->text[at+1] == '$') {
				if (!ain12_regex_emit(rx, out, used, "$", 1)) return false;
				at += 2; continue;
			}
			if (r->text[at+1] == '&') {
				if (!ain12_regex_emit(rx, out, used, rx->subject->text+start, end-start)) return false;
				at += 2; continue;
			}
		}
		if (!ain12_regex_emit(rx, out, used, r->text+at, step)) return false;
		at += step;
	}
	return true;
}

static bool ain12_regex_replace_pass(struct ain12_regex *rx, const struct string *r,
		bool formatted, struct string *out, size_t *used, unsigned *matches)
{
	rx->steps = AIN12_REGEX_STEPS; *used = 0; *matches = 0;
	for (size_t at = 0; at < (size_t)rx->subject->size;) {
		size_t end;
		if (ain12_regex_match(rx, 0, at, &end)) {
			if (end <= at) { rx->error = AIN12_RX_INVALID; return false; }
			if (!ain12_regex_emit_replacement(rx, out, used, r, formatted, at, end)) return false;
			(*matches)++; at = end;
		} else {
			if (rx->error) return false;
			size_t step = ain12_string_replace_step(rx->subject, at);
			if (!ain12_regex_emit(rx, out, used, rx->subject->text+at, step)) return false;
			at += step;
		}
	}
	return true;
}

static struct string *ain12_string_regex_replace(const struct string *s,
		const struct string *pattern, const struct string *r, enum ain12_regex_error *error)
{
	struct ain12_regex rx = { .subject = s };
	struct string *out = NULL;
	if (!s || !pattern || !r || s->size < 0 || pattern->size < 0 || r->size < 0) {
		rx.error = AIN12_RX_INVALID; goto done;
	}
	if ((size_t)s->size > AIN12_STRING_REPLACE_MAX ||
		(size_t)pattern->size > AIN12_STRING_REPLACE_MAX || (size_t)r->size > AIN12_STRING_REPLACE_MAX) {
		rx.error = AIN12_RX_LIMIT; goto done;
	}
	if (!ain12_regex_compile(&rx, pattern)) goto done;
	bool formatted;
	if (!ain12_regex_replacement(r, &formatted)) { rx.error = AIN12_RX_UNSUPPORTED; goto done; }
	size_t length; unsigned matches;
	if (!ain12_regex_replace_pass(&rx, r, formatted, NULL, &length, &matches)) goto done;
	if (!matches) { out = ain12_string_replace_copy(s); goto done; }
	if (!length) { out = string_ref(&EMPTY_STRING); goto done; }
	out = string_alloc((unsigned)length);
	size_t written; unsigned repeated;
	if (!ain12_regex_replace_pass(&rx, r, formatted, out, &written, &repeated) || written != length || repeated != matches) {
		free_string(out); out = NULL;
		if (!rx.error) rx.error = AIN12_RX_INVALID;
		goto done;
	}
	out->text[written] = 0;
done:
	if (error) *error = rx.error;
	return out;
}
#endif
