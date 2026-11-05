/* Stub implementation for the System 4 HLL library "TextFile".
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

HLL_WARN_UNIMPLEMENTED(false, bool, TextFile, Create, struct string * FileName);
HLL_WARN_UNIMPLEMENTED(false, bool, TextFile, Close, void);
HLL_WARN_UNIMPLEMENTED(false, bool, TextFile, Write, struct string * Text);
HLL_WARN_UNIMPLEMENTED(false, bool, TextFile, ReadAll, struct string * FileName, struct string * * Text);

HLL_LIBRARY(TextFile,
	    HLL_EXPORT(Create, TextFile_Create),
	    HLL_EXPORT(Close, TextFile_Close),
	    HLL_EXPORT(Write, TextFile_Write),
	    HLL_EXPORT(ReadAll, TextFile_ReadAll));
