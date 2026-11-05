/* Stub implementation for the System 4 HLL library "FileDialog".
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

#include "system4.h"
#include "system4/string.h"

#include "vm/page.h"
#include "hll.h"

HLL_WARN_UNIMPLEMENTED(false, bool, FileDialog, OpenLoadDialog, struct string * * LoadFileName, struct string * TitleName, struct string * FolderName, struct page * FilterTitleList, struct page * FilterExtendList);
HLL_WARN_UNIMPLEMENTED(false, bool, FileDialog, OpenSaveDialog, struct string * * SaveFileName, struct string * TitleName, struct string * FolderName, struct page * FilterTitleList, struct page * FilterExtendList);

HLL_LIBRARY(FileDialog,
	    HLL_EXPORT(OpenLoadDialog, FileDialog_OpenLoadDialog),
	    HLL_EXPORT(OpenSaveDialog, FileDialog_OpenSaveDialog));
