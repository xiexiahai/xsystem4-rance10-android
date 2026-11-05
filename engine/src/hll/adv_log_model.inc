#ifndef ADV_LOG_MODEL_INC
#define ADV_LOG_MODEL_INC

/*
 * Project-private message-window history model. This is not the old ADL
 * format and is not a claim about the native AnteaterADVEngine DLL.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "system4/string.h"

#ifndef ADV_LOG_MAX_PAGES
#define ADV_LOG_MAX_PAGES 1000u
#endif
#ifndef ADV_LOG_MAX_LINES
#define ADV_LOG_MAX_LINES 4096u
#endif
#ifndef ADV_LOG_MAX_VOICES
#define ADV_LOG_MAX_VOICES 4096u
#endif
#ifndef ADV_LOG_MAX_STRING
#define ADV_LOG_MAX_STRING (1024u * 1024u)
#endif
#ifndef ADV_LOG_MAX_BYTES
#define ADV_LOG_MAX_BYTES (16u * 1024u * 1024u)
#endif

/* These macros cover model mutation/pack/unpack only; engine strings returned
 * by getters still use make_string/free_string and are not redirected. */
#ifndef ADV_LOG_MALLOC
#define ADV_LOG_MALLOC(size) malloc(size)
#endif
#ifndef ADV_LOG_CALLOC
#define ADV_LOG_CALLOC(count, size) calloc((count), (size))
#endif
#ifndef ADV_LOG_REALLOC
#define ADV_LOG_REALLOC(ptr, size) realloc((ptr), (size))
#endif
#ifndef ADV_LOG_FREE
#define ADV_LOG_FREE(ptr) free(ptr)
#endif

struct adv_log_line {
	uint8_t *window;
	uint32_t window_len;
	uint8_t *text;
	uint32_t text_len;
};

struct adv_log_voice {
	uint8_t *name;
	uint32_t name_len;
	uint8_t *filter;
	uint32_t filter_len;
};

struct adv_log_page {
	struct adv_log_line *lines;
	uint32_t line_count;
	uint32_t line_cap;
	struct adv_log_voice *voices;
	uint32_t voice_count;
	uint32_t voice_cap;
};

struct adv_log_model {
	struct adv_log_page *pages;
	uint32_t page_count;
	uint32_t page_cap;
	bool enabled;
	bool enabled_known;
	size_t content_bytes;
};

static uint32_t adv_log_get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void adv_log_put_le32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}

static void adv_log_free_line(struct adv_log_line *line)
{
	ADV_LOG_FREE(line->window);
	ADV_LOG_FREE(line->text);
	memset(line, 0, sizeof(*line));
}

static void adv_log_free_voice(struct adv_log_voice *voice)
{
	ADV_LOG_FREE(voice->name);
	ADV_LOG_FREE(voice->filter);
	memset(voice, 0, sizeof(*voice));
}

static void adv_log_free_page(struct adv_log_page *page)
{
	for (uint32_t i = 0; i < page->line_count; i++)
		adv_log_free_line(&page->lines[i]);
	for (uint32_t i = 0; i < page->voice_count; i++)
		adv_log_free_voice(&page->voices[i]);
	ADV_LOG_FREE(page->lines);
	ADV_LOG_FREE(page->voices);
	memset(page, 0, sizeof(*page));
}

static size_t adv_log_page_bytes(const struct adv_log_page *page)
{
	size_t bytes = 0;
	for (uint32_t i = 0; i < page->line_count; i++) {
		bytes += page->lines[i].window_len;
		bytes += page->lines[i].text_len;
	}
	for (uint32_t i = 0; i < page->voice_count; i++) {
		bytes += page->voices[i].name_len;
		bytes += page->voices[i].filter_len;
	}
	return bytes;
}

static void adv_log_free_storage(struct adv_log_model *m)
{
	if (!m)
		return;
	for (uint32_t i = 0; i < m->page_count; i++)
		adv_log_free_page(&m->pages[i]);
	ADV_LOG_FREE(m->pages);
	m->pages = NULL;
	m->page_count = 0;
	m->page_cap = 0;
	m->content_bytes = 0;
}

static bool adv_log_ensure_virtual(struct adv_log_model *m)
{
	if (m->page_count)
		return true;
	struct adv_log_page *pages = ADV_LOG_CALLOC(1, sizeof(*pages));
	struct adv_log_line *lines = ADV_LOG_CALLOC(1, sizeof(*lines));
	if (!pages || !lines) {
		ADV_LOG_FREE(pages);
		ADV_LOG_FREE(lines);
		return false;
	}
	pages[0].lines = lines;
	pages[0].line_cap = pages[0].line_count = 1;
	m->pages = pages;
	m->page_cap = 1;
	m->page_count = 1;
	m->content_bytes = 0;
	return true;
}

static bool adv_log_string_bytes(const struct string *s, const uint8_t **bytes,
		uint32_t *len)
{
	*bytes = NULL;
	*len = 0;
	if (!s)
		return true;
	if (s->size < 0 || (uint32_t)s->size > (uint32_t)ADV_LOG_MAX_STRING)
		return false;
	*bytes = (const uint8_t *)s->text;
	*len = (uint32_t)s->size;
	return true;
}

static uint8_t *adv_log_copy_bytes(const uint8_t *bytes, uint32_t len)
{
	if (!len)
		return NULL;
	uint8_t *copy = ADV_LOG_MALLOC(len);
	if (!copy)
		return NULL;
	memcpy(copy, bytes, len);
	return copy;
}

static bool adv_log_ensure_line_capacity(struct adv_log_page *page,
		uint32_t needed)
{
	if (page->line_cap >= needed)
		return true;
	uint32_t capacity = page->line_cap ? page->line_cap * 2 : 1;
	if (capacity < needed)
		capacity = needed;
	if (capacity > (uint32_t)ADV_LOG_MAX_LINES)
		capacity = (uint32_t)ADV_LOG_MAX_LINES;
	if (capacity < needed)
		return false;
	struct adv_log_line *lines = ADV_LOG_REALLOC(page->lines,
			(size_t)capacity * sizeof(*lines));
	if (!lines)
		return false;
	memset(lines + page->line_count, 0,
		(size_t)(capacity - page->line_count) * sizeof(*lines));
	page->lines = lines;
	page->line_cap = capacity;
	return true;
}

static bool adv_log_ensure_voice_capacity(struct adv_log_page *page,
		uint32_t needed)
{
	if (page->voice_cap >= needed)
		return true;
	uint32_t capacity = page->voice_cap ? page->voice_cap * 2 : 1;
	if (capacity < needed)
		capacity = needed;
	if (capacity > (uint32_t)ADV_LOG_MAX_VOICES)
		capacity = (uint32_t)ADV_LOG_MAX_VOICES;
	if (capacity < needed)
		return false;
	struct adv_log_voice *voices = ADV_LOG_REALLOC(page->voices,
			(size_t)capacity * sizeof(*voices));
	if (!voices)
		return false;
	memset(voices + page->voice_count, 0,
		(size_t)(capacity - page->voice_count) * sizeof(*voices));
	page->voices = voices;
	page->voice_cap = capacity;
	return true;
}

static bool adv_log_ensure_page_capacity(struct adv_log_model *m,
		uint32_t needed)
{
	if (m->page_cap >= needed)
		return true;
	uint32_t capacity = m->page_cap ? m->page_cap * 2 : 1;
	if (capacity < needed)
		capacity = needed;
	if (capacity > (uint32_t)ADV_LOG_MAX_PAGES)
		capacity = (uint32_t)ADV_LOG_MAX_PAGES;
	if (capacity < needed)
		return false;
	struct adv_log_page *pages = ADV_LOG_REALLOC(m->pages,
			(size_t)capacity * sizeof(*pages));
	if (!pages)
		return false;
	memset(pages + m->page_count, 0,
		(size_t)(capacity - m->page_count) * sizeof(*pages));
	m->pages = pages;
	m->page_cap = capacity;
	return true;
}

static struct string *adv_log_empty_string(void)
{
	return make_string("", 0);
}

static struct string *adv_log_make_string(const uint8_t *bytes, uint32_t len)
{
	if (!len)
		return adv_log_empty_string();
	return make_string((const char *)bytes, len);
}

static bool adv_log_is_enable(const struct adv_log_model *m)
{
	return m && (m->enabled_known ? m->enabled : true);
}

static void adv_log_set_enable(struct adv_log_model *m, bool enabled)
{
	if (!m)
		return;
	m->enabled = enabled;
	m->enabled_known = true;
}

static void adv_log_clear(struct adv_log_model *m)
{
	if (!m)
		return;
	bool enabled = adv_log_is_enable(m);
	adv_log_free_storage(m);
	m->enabled = enabled;
	m->enabled_known = true;
}

static size_t adv_log_page_count(const struct adv_log_model *m)
{
	if (!m)
		return 0;
	return m->page_count ? m->page_count : 1;
}

static size_t adv_log_line_count(const struct adv_log_model *m, size_t page)
{
	if (!m)
		return 0;
	if (!m->page_count)
		return page == 0 ? 1 : 0;
	if (page >= m->page_count)
		return 0;
	return m->pages[page].line_count;
}

static size_t adv_log_voice_count(const struct adv_log_model *m, size_t page)
{
	if (!m)
		return 0;
	if (!m->page_count || page >= m->page_count)
		return 0;
	return m->pages[page].voice_count;
}

static struct string *adv_log_get_text(const struct adv_log_model *m,
		size_t page, size_t line)
{
	if (!m)
		return adv_log_empty_string();
	if (!m->page_count)
		return adv_log_empty_string();
	if (page >= m->page_count || line >= m->pages[page].line_count)
		return adv_log_empty_string();
	struct adv_log_line *entry = &m->pages[page].lines[line];
	return adv_log_make_string(entry->text, entry->text_len);
}

static struct string *adv_log_get_voice(const struct adv_log_model *m,
		size_t page, size_t voice)
{
	if (!m || !m->page_count || page >= m->page_count ||
			voice >= m->pages[page].voice_count)
		return adv_log_empty_string();
	struct adv_log_voice *entry = &m->pages[page].voices[voice];
	return adv_log_make_string(entry->name, entry->name_len);
}

static struct string *adv_log_get_filter(const struct adv_log_model *m,
		size_t page, size_t voice)
{
	if (!m || !m->page_count || page >= m->page_count ||
			voice >= m->pages[page].voice_count)
		return adv_log_empty_string();
	struct adv_log_voice *entry = &m->pages[page].voices[voice];
	return adv_log_make_string(entry->filter, entry->filter_len);
}

static bool adv_log_add_text_ready(struct adv_log_model *m,
		const uint8_t *text_bytes, uint32_t text_len,
		const uint8_t *window_bytes, uint32_t window_len)
{
	struct adv_log_page *page = &m->pages[m->page_count - 1];
	struct adv_log_line *line = &page->lines[page->line_count - 1];
	bool line_empty = line->text_len == 0;
	bool window_equal = line->window_len == window_len &&
		(!window_len || !memcmp(line->window, window_bytes, window_len));
	bool need_new_line = !line_empty && window_len > 0 && !window_equal;
	bool assign_window = line_empty && window_len > 0 && !window_equal;
	if (need_new_line && page->line_count >= (uint32_t)ADV_LOG_MAX_LINES)
		return false;
	if (!need_new_line &&
			(size_t)line->text_len + text_len > (size_t)ADV_LOG_MAX_STRING)
		return false;

	size_t new_content = m->content_bytes;
	if (assign_window)
		new_content -= line->window_len;
	if (assign_window || need_new_line)
		new_content += window_len;
	new_content += text_len;
	if (new_content > (size_t)ADV_LOG_MAX_BYTES)
		return false;

	uint8_t *new_text = NULL, *new_window = NULL, *joined = NULL;
	if (need_new_line && text_len) {
		new_text = ADV_LOG_MALLOC(text_len);
		if (!new_text)
			return false;
		memcpy(new_text, text_bytes, text_len);
	}
	if (assign_window || need_new_line) {
		new_window = adv_log_copy_bytes(window_bytes, window_len);
		if (window_len && !new_window) {
			ADV_LOG_FREE(new_text);
			return false;
		}
	}
	if (!need_new_line && text_len) {
		size_t joined_len = (size_t)line->text_len + text_len;
		joined = ADV_LOG_MALLOC(joined_len);
		if (!joined) {
			ADV_LOG_FREE(new_window);
			return false;
		}
		if (line->text_len)
			memcpy(joined, line->text, line->text_len);
		memcpy(joined + line->text_len, text_bytes, text_len);
	}

	if (need_new_line) {
		if (!adv_log_ensure_line_capacity(page, page->line_count + 1)) {
			ADV_LOG_FREE(new_text);
			ADV_LOG_FREE(new_window);
			return false;
		}
		struct adv_log_line *next = &page->lines[page->line_count];
		memset(next, 0, sizeof(*next));
		next->window = new_window;
		next->window_len = window_len;
		next->text = new_text;
		next->text_len = text_len;
		page->line_count++;
		m->content_bytes = new_content;
		return true;
	}

	if (assign_window) {
		ADV_LOG_FREE(line->window);
		line->window = new_window;
		line->window_len = window_len;
	}
	if (text_len) {
		ADV_LOG_FREE(line->text);
		line->text = joined;
		line->text_len += text_len;
	}
	m->content_bytes = new_content;
	return true;
}

static bool adv_log_add_text(struct adv_log_model *m, const struct string *text,
		const struct string *window_name)
{
	if (!m)
		return false;
	if (!adv_log_is_enable(m))
		return true;
	const uint8_t *text_bytes = NULL, *window_bytes = NULL;
	uint32_t text_len = 0, window_len = 0;
	if (!adv_log_string_bytes(text, &text_bytes, &text_len) ||
			!adv_log_string_bytes(window_name, &window_bytes, &window_len))
		return false;

	struct adv_log_model temporary;
	bool use_temporary = m->page_count == 0;
	if (use_temporary) {
		memset(&temporary, 0, sizeof(temporary));
		temporary.enabled = m->enabled;
		temporary.enabled_known = m->enabled_known;
		if (!adv_log_ensure_virtual(&temporary))
			return false;
	}
	struct adv_log_model *target = use_temporary ? &temporary : m;
	if (!adv_log_add_text_ready(target, text_bytes, text_len,
			window_bytes, window_len)) {
		if (use_temporary)
			adv_log_free_storage(&temporary);
		return false;
	}
	if (use_temporary) {
		adv_log_free_storage(m);
		*m = temporary;
	}
	return true;
}

static bool adv_log_new_line_ready(struct adv_log_model *m)
{
	struct adv_log_page *page = &m->pages[m->page_count - 1];
	if (page->line_count >= (uint32_t)ADV_LOG_MAX_LINES)
		return false;
	struct adv_log_line *cur = &page->lines[page->line_count - 1];
	uint32_t inherited_len = cur->window_len;
	if (inherited_len > (uint32_t)ADV_LOG_MAX_STRING ||
			(size_t)inherited_len > (size_t)ADV_LOG_MAX_BYTES - m->content_bytes)
		return false;
	uint8_t *window = adv_log_copy_bytes(cur->window, inherited_len);
	if (inherited_len && !window)
		return false;
	if (!adv_log_ensure_line_capacity(page, page->line_count + 1)) {
		ADV_LOG_FREE(window);
		return false;
	}
	struct adv_log_line *next = &page->lines[page->line_count];
	memset(next, 0, sizeof(*next));
	next->window = window;
	next->window_len = inherited_len;
	page->line_count++;
	m->content_bytes += inherited_len;
	return true;
}

static bool adv_log_new_line(struct adv_log_model *m)
{
	if (!m)
		return false;
	if (!adv_log_is_enable(m))
		return true;
	struct adv_log_model temporary;
	bool use_temporary = m->page_count == 0;
	if (use_temporary) {
		memset(&temporary, 0, sizeof(temporary));
		temporary.enabled = m->enabled;
		temporary.enabled_known = m->enabled_known;
		if (!adv_log_ensure_virtual(&temporary))
			return false;
	}
	struct adv_log_model *target = use_temporary ? &temporary : m;
	if (!adv_log_new_line_ready(target)) {
		if (use_temporary)
			adv_log_free_storage(&temporary);
		return false;
	}
	if (use_temporary) {
		adv_log_free_storage(m);
		*m = temporary;
	}
	return true;
}

static bool adv_log_new_page(struct adv_log_model *m)
{
	if (!m)
		return false;
	if (!adv_log_is_enable(m))
		return true;
	if (m->page_count == 0) {
		if (ADV_LOG_MAX_PAGES == 0)
			return false;
		uint32_t count = ADV_LOG_MAX_PAGES >= 2 ? 2 : 1;
		struct adv_log_page *pages = ADV_LOG_CALLOC(count, sizeof(*pages));
		if (!pages)
			return false;
		for (uint32_t i = 0; i < count; i++) {
			pages[i].lines = ADV_LOG_CALLOC(1, sizeof(*pages[i].lines));
			if (!pages[i].lines) {
				for (uint32_t j = 0; j < i; j++)
					ADV_LOG_FREE(pages[j].lines);
				ADV_LOG_FREE(pages);
				return false;
			}
			pages[i].line_cap = pages[i].line_count = 1;
		}
		m->pages = pages;
		m->page_cap = count;
		m->page_count = count;
		m->content_bytes = 0;
		return true;
	}

	struct adv_log_page *new_page = ADV_LOG_CALLOC(1, sizeof(*new_page));
	struct adv_log_line *new_line = ADV_LOG_CALLOC(1, sizeof(*new_line));
	if (!new_page || !new_line) {
		ADV_LOG_FREE(new_page);
		ADV_LOG_FREE(new_line);
		return false;
	}
	new_page->lines = new_line;
	new_page->line_cap = new_page->line_count = 1;

	if (m->page_count >= (uint32_t)ADV_LOG_MAX_PAGES) {
		size_t removed = adv_log_page_bytes(&m->pages[0]);
		adv_log_free_page(&m->pages[0]);
		memmove(&m->pages[0], &m->pages[1],
			(size_t)(m->page_count - 1) * sizeof(*m->pages));
		m->pages[m->page_count - 1] = *new_page;
		ADV_LOG_FREE(new_page);
		m->content_bytes -= removed;
		return true;
	}
	if (!adv_log_ensure_page_capacity(m, m->page_count + 1)) {
		adv_log_free_page(new_page);
		ADV_LOG_FREE(new_page);
		return false;
	}
	m->pages[m->page_count++] = *new_page;
	ADV_LOG_FREE(new_page);
	return true;
}

static bool adv_log_add_voice_ready(struct adv_log_model *m,
		const uint8_t *name_bytes, uint32_t name_len,
		const uint8_t *filter_bytes, uint32_t filter_len)
{
	struct adv_log_page *page = &m->pages[m->page_count - 1];
	if (page->voice_count >= (uint32_t)ADV_LOG_MAX_VOICES)
		return false;
	if ((size_t)name_len > (size_t)ADV_LOG_MAX_BYTES - m->content_bytes ||
			(size_t)filter_len > (size_t)ADV_LOG_MAX_BYTES - m->content_bytes - name_len)
		return false;
	uint8_t *name_copy = adv_log_copy_bytes(name_bytes, name_len);
	uint8_t *filter_copy = adv_log_copy_bytes(filter_bytes, filter_len);
	if ((name_len && !name_copy) || (filter_len && !filter_copy)) {
		ADV_LOG_FREE(name_copy);
		ADV_LOG_FREE(filter_copy);
		return false;
	}
	if (!adv_log_ensure_voice_capacity(page, page->voice_count + 1)) {
		ADV_LOG_FREE(name_copy);
		ADV_LOG_FREE(filter_copy);
		return false;
	}
	struct adv_log_voice *voice = &page->voices[page->voice_count++];
	voice->name = name_copy;
	voice->name_len = name_len;
	voice->filter = filter_copy;
	voice->filter_len = filter_len;
	m->content_bytes += name_len + filter_len;
	return true;
}

static bool adv_log_add_voice(struct adv_log_model *m, const struct string *name,
		const struct string *filter)
{
	if (!m)
		return false;
	if (!adv_log_is_enable(m))
		return true;
	const uint8_t *name_bytes = NULL, *filter_bytes = NULL;
	uint32_t name_len = 0, filter_len = 0;
	if (!adv_log_string_bytes(name, &name_bytes, &name_len) ||
			!adv_log_string_bytes(filter, &filter_bytes, &filter_len))
		return false;

	struct adv_log_model temporary;
	bool use_temporary = m->page_count == 0;
	if (use_temporary) {
		memset(&temporary, 0, sizeof(temporary));
		temporary.enabled = m->enabled;
		temporary.enabled_known = m->enabled_known;
		if (!adv_log_ensure_virtual(&temporary))
			return false;
	}
	struct adv_log_model *target = use_temporary ? &temporary : m;
	if (!adv_log_add_voice_ready(target, name_bytes, name_len,
			filter_bytes, filter_len)) {
		if (use_temporary)
			adv_log_free_storage(&temporary);
		return false;
	}
	if (use_temporary) {
		adv_log_free_storage(m);
		*m = temporary;
	}
	return true;
}

static bool adv_log_pack(const struct adv_log_model *m, uint8_t **out, size_t *len)
{
	if (out)
		*out = NULL;
	if (len)
		*len = 0;
	if (!m || !out || !len)
		return false;
	uint32_t pages = m->page_count ? m->page_count : 1;
	if (pages == 0 || pages > (uint32_t)ADV_LOG_MAX_PAGES)
		return false;
	size_t total = 16;
	struct adv_log_line empty_line;
	memset(&empty_line, 0, sizeof(empty_line));
	for (uint32_t p = 0; p < pages; p++) {
		struct adv_log_page page;
		if (m->page_count) {
			page = m->pages[p];
		} else {
			memset(&page, 0, sizeof(page));
			page.line_count = 1;
			page.lines = &empty_line;
		}
		if (page.line_count == 0 ||
				page.line_count > (uint32_t)ADV_LOG_MAX_LINES ||
				page.voice_count > (uint32_t)ADV_LOG_MAX_VOICES)
			return false;
		if (total > SIZE_MAX - 4)
			return false;
		total += 4;
		for (uint32_t l = 0; l < page.line_count; l++) {
			struct adv_log_line *line = &page.lines[l];
			if (line->window_len > (uint32_t)ADV_LOG_MAX_STRING ||
					line->text_len > (uint32_t)ADV_LOG_MAX_STRING ||
					(line->window_len && !line->window) ||
					(line->text_len && !line->text))
				return false;
			size_t add = 8 + (size_t)line->window_len + (size_t)line->text_len;
			if (total > SIZE_MAX - add)
				return false;
			total += add;
		}
		if (total > SIZE_MAX - 4)
			return false;
		total += 4;
		for (uint32_t v = 0; v < page.voice_count; v++) {
			struct adv_log_voice *voice = &page.voices[v];
			if (voice->name_len > (uint32_t)ADV_LOG_MAX_STRING ||
					voice->filter_len > (uint32_t)ADV_LOG_MAX_STRING ||
					(voice->name_len && !voice->name) ||
					(voice->filter_len && !voice->filter))
				return false;
			size_t add = 8 + (size_t)voice->name_len + (size_t)voice->filter_len;
			if (total > SIZE_MAX - add)
				return false;
			total += add;
		}
	}

	uint8_t *buffer = ADV_LOG_MALLOC(total);
	if (!buffer)
		return false;
	uint8_t *at = buffer;
	memcpy(at, "R10LOG01", 8);
	at += 8;
	adv_log_put_le32(at, adv_log_is_enable(m) ? 1 : 0);
	at += 4;
	adv_log_put_le32(at, pages);
	at += 4;
	for (uint32_t p = 0; p < pages; p++) {
		struct adv_log_page page;
		if (m->page_count) {
			page = m->pages[p];
		} else {
			memset(&page, 0, sizeof(page));
			page.line_count = 1;
			page.lines = &empty_line;
		}
		adv_log_put_le32(at, page.line_count);
		at += 4;
		for (uint32_t l = 0; l < page.line_count; l++) {
			struct adv_log_line *line = &page.lines[l];
			adv_log_put_le32(at, line->window_len);
			at += 4;
			if (line->window_len) {
				memcpy(at, line->window, line->window_len);
				at += line->window_len;
			}
			adv_log_put_le32(at, line->text_len);
			at += 4;
			if (line->text_len) {
				memcpy(at, line->text, line->text_len);
				at += line->text_len;
			}
		}
		adv_log_put_le32(at, page.voice_count);
		at += 4;
		for (uint32_t v = 0; v < page.voice_count; v++) {
			struct adv_log_voice *voice = &page.voices[v];
			adv_log_put_le32(at, voice->name_len);
			at += 4;
			if (voice->name_len) {
				memcpy(at, voice->name, voice->name_len);
				at += voice->name_len;
			}
			adv_log_put_le32(at, voice->filter_len);
			at += 4;
			if (voice->filter_len) {
				memcpy(at, voice->filter, voice->filter_len);
				at += voice->filter_len;
			}
		}
	}
	*out = buffer;
	*len = total;
	return true;
}

static bool adv_log_unpack(struct adv_log_model *m, const uint8_t *data, size_t len)
{
	if (!m || !data || len < 16 || memcmp(data, "R10LOG01", 8))
		return false;
	size_t pos = 8;
	uint32_t enabled = adv_log_get_le32(data + pos);
	pos += 4;
	uint32_t pages = adv_log_get_le32(data + pos);
	pos += 4;
	if (enabled > 1 || pages == 0 || pages > (uint32_t)ADV_LOG_MAX_PAGES)
		return false;

	struct adv_log_model temporary;
	memset(&temporary, 0, sizeof(temporary));
	temporary.pages = ADV_LOG_CALLOC(pages, sizeof(*temporary.pages));
	if (!temporary.pages)
		return false;
	temporary.page_count = pages;
	temporary.page_cap = pages;
	temporary.enabled = enabled != 0;
	temporary.enabled_known = true;

	for (uint32_t p = 0; p < pages; p++) {
		if (len - pos < 4)
			goto fail;
		uint32_t lines = adv_log_get_le32(data + pos);
		pos += 4;
		if (lines == 0 || lines > (uint32_t)ADV_LOG_MAX_LINES)
			goto fail;
		struct adv_log_page *page = &temporary.pages[p];
		page->lines = ADV_LOG_CALLOC(lines, sizeof(*page->lines));
		if (!page->lines)
			goto fail;
		page->line_count = page->line_cap = lines;
		for (uint32_t l = 0; l < lines; l++) {
			struct adv_log_line *line = &page->lines[l];
			if (len - pos < 4)
				goto fail;
			uint32_t window_len = adv_log_get_le32(data + pos);
			pos += 4;
			if (window_len > (uint32_t)ADV_LOG_MAX_STRING ||
					(size_t)window_len > len - pos)
				goto fail;
			line->window = adv_log_copy_bytes(data + pos, window_len);
			if (window_len && !line->window)
				goto fail;
			line->window_len = window_len;
			pos += window_len;
			if (len - pos < 4)
				goto fail;
			uint32_t text_len = adv_log_get_le32(data + pos);
			pos += 4;
			if (text_len > (uint32_t)ADV_LOG_MAX_STRING ||
					(size_t)text_len > len - pos)
				goto fail;
			line->text = adv_log_copy_bytes(data + pos, text_len);
			if (text_len && !line->text)
				goto fail;
			line->text_len = text_len;
			pos += text_len;
			if ((size_t)window_len > (size_t)ADV_LOG_MAX_BYTES - temporary.content_bytes)
				goto fail;
			temporary.content_bytes += window_len;
			if ((size_t)text_len > (size_t)ADV_LOG_MAX_BYTES - temporary.content_bytes)
				goto fail;
			temporary.content_bytes += text_len;
		}
		if (len - pos < 4)
			goto fail;
		uint32_t voices = adv_log_get_le32(data + pos);
		pos += 4;
		if (voices > (uint32_t)ADV_LOG_MAX_VOICES)
			goto fail;
		page->voices = voices ? ADV_LOG_CALLOC(voices, sizeof(*page->voices)) : NULL;
		if (voices && !page->voices)
			goto fail;
		page->voice_count = page->voice_cap = voices;
		for (uint32_t v = 0; v < voices; v++) {
			struct adv_log_voice *voice = &page->voices[v];
			if (len - pos < 4)
				goto fail;
			uint32_t name_len = adv_log_get_le32(data + pos);
			pos += 4;
			if (name_len > (uint32_t)ADV_LOG_MAX_STRING ||
					(size_t)name_len > len - pos)
				goto fail;
			voice->name = adv_log_copy_bytes(data + pos, name_len);
			if (name_len && !voice->name)
				goto fail;
			voice->name_len = name_len;
			pos += name_len;
			if (len - pos < 4)
				goto fail;
			uint32_t filter_len = adv_log_get_le32(data + pos);
			pos += 4;
			if (filter_len > (uint32_t)ADV_LOG_MAX_STRING ||
					(size_t)filter_len > len - pos)
				goto fail;
			voice->filter = adv_log_copy_bytes(data + pos, filter_len);
			if (filter_len && !voice->filter)
				goto fail;
			voice->filter_len = filter_len;
			pos += filter_len;
			if ((size_t)name_len > (size_t)ADV_LOG_MAX_BYTES - temporary.content_bytes)
				goto fail;
			temporary.content_bytes += name_len;
			if ((size_t)filter_len > (size_t)ADV_LOG_MAX_BYTES - temporary.content_bytes)
				goto fail;
			temporary.content_bytes += filter_len;
		}
	}
	if (pos != len)
		goto fail;
	adv_log_free_storage(m);
	*m = temporary;
	return true;

fail:
	adv_log_free_storage(&temporary);
	return false;
}

#endif /* ADV_LOG_MODEL_INC */
