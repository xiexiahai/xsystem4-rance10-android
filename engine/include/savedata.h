/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#ifndef SYSTEM4_SAVEDATA_H
#define SYSTEM4_SAVEDATA_H

#include <stdio.h>
#include <stdint.h>
#include "cJSON.h"
#include "vm.h"

void json_load_page(struct page *page, cJSON *vars, bool call_dtors);

// A buffered write is successful only after closing the stream succeeds.
bool finish_save_file(FILE *file, bool write_success);

// Keep the previous file until the new stream has been written and closed.
struct save_file {
	FILE *file;
	char *path;
	char *temporary_path;
};
bool begin_save_file(struct save_file *save, const char *path);
bool end_save_file(struct save_file *save, bool write_success);

int save_json(const char *filename, cJSON *json);
cJSON *load_json(const char *filename);
int save_globals(const char *keyname, const char *filename, const char *group_name, int *n);
int load_globals(const char *keyname, const char *filename, const char *group_name, int *n);
bool write_group_save_comment(const char *key,const char *filename,const struct string *comment);
bool read_group_save_comment(const char *key,const char *filename,struct string **comment);

struct ain;
// Explicit VM lifetime; invalidate before changing any fingerprinted AIN data.
void savedata_program_id_begin(const struct ain *program);
void savedata_program_id_end(void);
void savedata_program_id_invalidate(const struct ain *program);
// False outside the currently registered VM program lifetime.
bool savedata_program_id_epoch(const struct ain *program, uint64_t *out);
int delete_save_file(const char *filename);

#endif /* SYSTEM4_SAVEDATA_H */
