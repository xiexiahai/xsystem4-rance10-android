#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/string.h"
#include "vm.h"
#include "vm/page.h"
#include "hll.h"

#include "adv_log_model.inc"

#define R10LOG_TAG 0x41303152u
#define R10LOG_VERSION 1u
#define R10LOG_MAX_SAVE (32u * 1024u * 1024u)

static struct adv_log_model model;

static void R10Log_Clear(void)
{
	adv_log_clear(&model);
}

static void R10Log_AddText(struct string *text, struct string *window)
{
	if (!adv_log_add_text(&model, text, window))
		WARNING("AnteaterADVLogList.AddText failed");
}

static void R10Log_AddNewLine(void)
{
	if (!adv_log_new_line(&model))
		WARNING("AnteaterADVLogList.AddNewLine failed");
}

static void R10Log_AddNewPage(void)
{
	if (!adv_log_new_page(&model))
		WARNING("AnteaterADVLogList.AddNewPage failed");
}

static void R10Log_AddVoice(struct string *name, struct string *filter)
{
	if (!adv_log_add_voice(&model, name, filter))
		WARNING("AnteaterADVLogList.AddVoice failed");
}

static void R10Log_SetEnable(bool enabled)
{
	adv_log_set_enable(&model, enabled);
}

static bool R10Log_IsEnable(void)
{
	return adv_log_is_enable(&model);
}

static int R10Log_GetNumofADVLog(void)
{
	return (int)adv_log_page_count(&model);
}

static int R10Log_GetNumofADVLogText(int page)
{
	if (page < 0)
		return 0;
	return (int)adv_log_line_count(&model, (size_t)page);
}

static struct string *R10Log_GetADVLogText(int page, int line)
{
	if (page < 0 || line < 0)
		return adv_log_empty_string();
	return adv_log_get_text(&model, (size_t)page, (size_t)line);
}

static int R10Log_GetNumofADVLogVoice(int page)
{
	if (page < 0)
		return 0;
	return (int)adv_log_voice_count(&model, (size_t)page);
}

static struct string *R10Log_GetADVLogVoice(int page, int voice)
{
	if (page < 0 || voice < 0)
		return adv_log_empty_string();
	return adv_log_get_voice(&model, (size_t)page, (size_t)voice);
}

static struct string *R10Log_GetADVLogVoiceFilter(int page, int voice)
{
	if (page < 0 || voice < 0)
		return adv_log_empty_string();
	return adv_log_get_filter(&model, (size_t)page, (size_t)voice);
}

static bool R10Log_Save(struct page **buffer)
{
	if (!buffer)
		return false;

	uint8_t *bytes = NULL;
	size_t len = 0;
	if (!adv_log_pack(&model, &bytes, &len))
		return false;
	if (len < 16 || len > (size_t)R10LOG_MAX_SAVE) {
		free(bytes);
		return false;
	}

	size_t words = (len + 3) / 4;
	if (words > (size_t)INT32_MAX - 3) {
		free(bytes);
		return false;
	}
	int nr_vars = (int)(3 + words);
	struct page *page = alloc_page(ARRAY_PAGE, AIN_ARRAY, nr_vars);
	page->array.rank = 1;
	page->array.struct_type = -1;

	struct ain_type element;
	memset(&element, 0, sizeof(element));
	element.data = AIN_INT;
	element.rank = 0;
	element.array_type = NULL;
	page_set_element_type(page, &element);

	page_set_var(page, 0, (int32_t)R10LOG_TAG);
	page_set_var(page, 1, (int32_t)R10LOG_VERSION);
	page_set_var(page, 2, (int32_t)len);
	for (size_t i = 0; i < words; i++) {
		uint32_t word = 0;
		for (size_t b = 0; b < 4; b++) {
			size_t at = i * 4 + b;
			if (at < len)
				word |= (uint32_t)bytes[at] << (b * 8);
		}
		page_set_var(page, (int)(3 + i), (int32_t)word);
	}

	struct page *old = *buffer;
	*buffer = page;
	if (old) {
		delete_page_vars(old);
		free_page(old);
	}
	free(bytes);
	return true;
}

static bool R10Log_Load(struct page **buffer)
{
	if (!buffer || !*buffer)
		return false;
	struct page *page = *buffer;
	if (page->type != ARRAY_PAGE ||
	    (page->a_type != AIN_INT && (page->a_type != AIN_ARRAY || !page->element_type ||
	     page->element_type->data != AIN_INT || page->element_type->rank != 0 || page->element_type->array_type)))
		return false;
	if (page->array.rank != 1)
		return false;
	if (page->element_type) {
		if (page->element_type->data != AIN_INT ||
				page->element_type->rank != 0 ||
				page->element_type->array_type != NULL)
			return false;
	}
	if (page->nr_vars < 3)
		return false;
	if ((uint32_t)page->values[0].i != R10LOG_TAG)
		return false;
	if ((uint32_t)page->values[1].i != R10LOG_VERSION)
		return false;
	int32_t stored_len = page->values[2].i;
	if (stored_len < 16 || (uint32_t)stored_len > R10LOG_MAX_SAVE)
		return false;
	size_t len = (size_t)stored_len;
	size_t words = (len + 3) / 4;
	if ((size_t)page->nr_vars != 3 + words)
		return false;

	for (size_t i = len; i < words * 4; i++) {
		uint32_t word = (uint32_t)page->values[3 + (int)(i / 4)].i;
		if (((word >> ((i % 4) * 8)) & 0xffu) != 0)
			return false;
	}

	uint8_t *bytes = malloc(len);
	if (!bytes)
		return false;
	for (size_t i = 0; i < len; i++) {
		uint32_t word = (uint32_t)page->values[3 + (int)(i / 4)].i;
		bytes[i] = (uint8_t)(word >> ((i % 4) * 8));
	}
	bool ok = adv_log_unpack(&model, bytes, len);
	free(bytes);
	return ok;
}

static void R10Log_ModuleFini(void)
{
	adv_log_clear(&model);
	model = (struct adv_log_model){0};
}

HLL_LIBRARY(AnteaterADVLogList,
	HLL_EXPORT(Clear, R10Log_Clear),
	HLL_EXPORT(AddText, R10Log_AddText),
	HLL_EXPORT(AddNewLine, R10Log_AddNewLine),
	HLL_EXPORT(AddNewPage, R10Log_AddNewPage),
	HLL_EXPORT(AddVoice, R10Log_AddVoice),
	HLL_EXPORT(SetEnable, R10Log_SetEnable),
	HLL_EXPORT(IsEnable, R10Log_IsEnable),
	HLL_EXPORT(GetNumofADVLog, R10Log_GetNumofADVLog),
	HLL_EXPORT(GetNumofADVLogText, R10Log_GetNumofADVLogText),
	HLL_EXPORT(GetADVLogText, R10Log_GetADVLogText),
	HLL_EXPORT(GetNumofADVLogVoice, R10Log_GetNumofADVLogVoice),
	HLL_EXPORT(GetADVLogVoice, R10Log_GetADVLogVoice),
	HLL_EXPORT(GetADVLogVoiceFilter, R10Log_GetADVLogVoiceFilter),
	HLL_EXPORT(Save, R10Log_Save),
	HLL_EXPORT(Load, R10Log_Load),
	HLL_EXPORT(_ModuleFini, R10Log_ModuleFini)
);
