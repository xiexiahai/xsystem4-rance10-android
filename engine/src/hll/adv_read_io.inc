#ifndef ADV_READ_IO_INC
#define ADV_READ_IO_INC

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#pragma push_macro("ERROR")
#undef ERROR
#include <windows.h>
#undef ERROR
#pragma pop_macro("ERROR")
#include <io.h>
wchar_t *utf8_to_wchar(const char *utf8);
#else
#include <unistd.h>
#endif
#include "system4/file.h"

#ifndef ADV_READ_IO_MAX_BYTES
#define ADV_READ_IO_MAX_BYTES (10u * 1024u * 1024u)
#endif

static bool adv_read_file_load(struct adv_read_table *table, const char *path)
{
	if (!table || !path)
		return false;

	FILE *file = file_open_utf8(path, "rb");
	if (!file) {
		int error = errno;
		return error == ENOENT;
	}

	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return false;
	}
	long length = ftell(file);
	if (length < 12 || (unsigned long)length > (unsigned long)ADV_READ_IO_MAX_BYTES) {
		fclose(file);
		return false;
	}
	if (fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return false;
	}

	uint8_t *bytes = malloc((size_t)length);
	if (!bytes) {
		fclose(file);
		return false;
	}
	bool read_ok = fread(bytes, 1, (size_t)length, file) == (size_t)length;
	if (fclose(file) != 0)
		read_ok = false;
	if (!read_ok) {
		free(bytes);
		return false;
	}

	bool unpacked = adv_read_unpack(table, bytes, (size_t)length);
	free(bytes);
	return unpacked;
}

static bool adv_read_file_save(struct adv_read_table *table, const char *path)
{
	if (!table || !path)
		return false;

	uint8_t *bytes = NULL;
	size_t length = 0;
	if (!adv_read_pack(table, &bytes, &length)) {
		free(bytes);
		return false;
	}
	if (length < 12 || length > (size_t)ADV_READ_IO_MAX_BYTES) {
		free(bytes);
		return false;
	}

	size_t path_len = strlen(path);
	if (path_len > SIZE_MAX - 5) {
		free(bytes);
		return false;
	}
	char *temporary_path = malloc(path_len + 5);
	if (!temporary_path) {
		free(bytes);
		return false;
	}
	memcpy(temporary_path, path, path_len);
	memcpy(temporary_path + path_len, ".tmp", 5);

	FILE *file = file_open_utf8(temporary_path, "wb");
	if (!file) {
		free(bytes);
		free(temporary_path);
		return false;
	}

	bool write_ok = fwrite(bytes, 1, length, file) == length;
	if (fflush(file) != 0)
		write_ok = false;
	if (ferror(file))
		write_ok = false;
	if (write_ok) {
#ifdef _WIN32
		if (_commit(_fileno(file)) != 0)
			write_ok = false;
#else
		if (fsync(fileno(file)) != 0)
			write_ok = false;
#endif
	}
	if (fclose(file) != 0)
		write_ok = false;

	bool replaced = false;
#ifdef _WIN32
	wchar_t *source = utf8_to_wchar(temporary_path);
	wchar_t *target = utf8_to_wchar(path);
	if (write_ok && source && target) {
		replaced = MoveFileExW(source, target,
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
	}
	if (!replaced && source)
		DeleteFileW(source);
	free(source);
	free(target);
#else
	if (write_ok) {
		if (rename(temporary_path, path) == 0)
			replaced = true;
		else
			remove(temporary_path);
	} else {
		remove(temporary_path);
	}
#endif

	free(bytes);
	free(temporary_path);
	return replaced;
}

#endif /* ADV_READ_IO_INC */
