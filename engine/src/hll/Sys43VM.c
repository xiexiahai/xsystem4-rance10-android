/* Stub implementation for the System 4 HLL library "Sys43VM".
 *
 * Generated for the Rance 03 Android port.
 * All functions are HLL_WARN_UNIMPLEMENTED stubs: they log a warning at
 * runtime and return a zero value, so that the game can keep running and we
 * get an accurate runtime report of what still needs a real implementation.
 *
 * Based on the interface definitions in the game's own .ain file.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdbool.h>
#include <string.h>

#include "system4.h"
#include "system4/string.h"

#include "vm/page.h"
#include "vm/heap.h"
#include "savedata.h"
#include "hll.h"

static bool Sys43VM_GetFunctionNameList(struct page **list)
{
	if (!list) return false;
	if (*list && (*list)->a_type==AIN_ARRAY) {
		if ((*list)->array.rank!=1 || (*list)->element_type->data!=AIN_STRING)
			VM_ERROR("GetFunctionNameList requires a string array");
		*list=generic_array_resize(*list,ain->nr_functions,false);
	} else {
		if (*list) {delete_page_vars(*list);free_page(*list);}
		union vm_value length=vm_int(ain->nr_functions);
		*list=alloc_array(1,&length,AIN_ARRAY_STRING,-1,false);
	}
	for(int i=0;i<ain->nr_functions;i++)
		variable_set(*list,i,AIN_STRING,vm_int(heap_alloc_string(cstr_to_string(ain->functions[i].name))));
	return true;
}
HLL_WARN_UNIMPLEMENTED(false, bool, Sys43VM, BeginProfiler, void);
HLL_WARN_UNIMPLEMENTED(false, bool, Sys43VM, EndProfiler, void);
HLL_WARN_UNIMPLEMENTED(0, int, Sys43VM, GetActualNumofPage, void);
HLL_WARN_UNIMPLEMENTED(0, int, Sys43VM, GetActualMemorySize, void);
static bool Sys43VM_WriteGroupSaveComment(struct string *key,struct string *filename,struct string *log)
{return key && filename && write_group_save_comment(key->text,filename->text,log);}
static bool Sys43VM_ReadGroupSaveComment(struct string *key,struct string *filename,struct string **log)
{return key && filename && read_group_save_comment(key->text,filename->text,log);}

HLL_LIBRARY(Sys43VM,
	    HLL_EXPORT(WriteGroupSaveComment, Sys43VM_WriteGroupSaveComment),
	    HLL_EXPORT(ReadGroupSaveComment, Sys43VM_ReadGroupSaveComment),
	    HLL_EXPORT(GetFunctionNameList, Sys43VM_GetFunctionNameList),
	    HLL_EXPORT(BeginProfiler, Sys43VM_BeginProfiler),
	    HLL_EXPORT(EndProfiler, Sys43VM_EndProfiler),
	    HLL_EXPORT(GetActualNumofPage, Sys43VM_GetActualNumofPage),
	    HLL_EXPORT(GetActualMemorySize, Sys43VM_GetActualMemorySize));
