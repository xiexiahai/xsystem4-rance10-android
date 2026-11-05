/* Stub implementation for the System 4 HLL library "ADVEngine".
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

HLL_WARN_UNIMPLEMENTED(false, bool, ADVEngine, Load, struct string * FunctionName);
HLL_WARN_UNIMPLEMENTED(, void, ADVEngine, Release, void);
HLL_WARN_UNIMPLEMENTED(0, int, ADVEngine, NumofFunctionName, void);
HLL_WARN_UNIMPLEMENTED(, void, ADVEngine, GetFunctionName, struct string * * FunctionName, int Index);
HLL_WARN_UNIMPLEMENTED(false, bool, ADVEngine, AnalyseIf, struct string * FunctionName);
HLL_WARN_UNIMPLEMENTED(0, int, ADVEngine, NumofIf, struct string * FunctionName);
HLL_WARN_UNIMPLEMENTED(, void, ADVEngine, GetIf, struct string * * If, struct string * FunctionName, int Index);
HLL_WARN_UNIMPLEMENTED(false, bool, ADVEngine, AnalyseFunction, struct string * FunctionName, struct string * IfData);
HLL_WARN_UNIMPLEMENTED(0, int, ADVEngine, NumofStep, struct string * FunctionName);
HLL_WARN_UNIMPLEMENTED(, void, ADVEngine, GetStepText, struct string * * Text, struct string * FunctionName, int Step);
HLL_WARN_UNIMPLEMENTED(, void, ADVEngine, OpenFunctionFile, struct string * FunctionName, int Step);
HLL_WARN_UNIMPLEMENTED(0, int, ADVEngine, GetCodeType, struct string * FunctionName, int Step);
HLL_WARN_UNIMPLEMENTED(, void, ADVEngine, GetMessage, struct string * * Message, struct string * FunctionName, int Step);
HLL_WARN_UNIMPLEMENTED(false, bool, ADVEngine, BindFunction, struct string * FunctionName, int Step);
HLL_WARN_UNIMPLEMENTED(, void, ADVEngine, GetFuncName, struct string * * FuncName, struct string * FunctionName, int Step);
HLL_WARN_UNIMPLEMENTED(0, int, ADVEngine, CreateDelegateObjectID, struct string * FunctionName, int Step);
HLL_WARN_UNIMPLEMENTED(false, bool, ADVEngine, GetArgumentValue_int, int * Value, struct string * FunctionName, int Step, int Index);
HLL_WARN_UNIMPLEMENTED(false, bool, ADVEngine, GetArgumentValue_float, float * Value, struct string * FunctionName, int Step, int Index);
HLL_WARN_UNIMPLEMENTED(false, bool, ADVEngine, GetArgumentValue_bool, int * Value, struct string * FunctionName, int Step, int Index);
HLL_WARN_UNIMPLEMENTED(false, bool, ADVEngine, GetArgumentValue_string, struct string * * Value, struct string * FunctionName, int Step, int Index);

HLL_LIBRARY(ADVEngine,
	    HLL_EXPORT(Load, ADVEngine_Load),
	    HLL_EXPORT(Release, ADVEngine_Release),
	    HLL_EXPORT(NumofFunctionName, ADVEngine_NumofFunctionName),
	    HLL_EXPORT(GetFunctionName, ADVEngine_GetFunctionName),
	    HLL_EXPORT(AnalyseIf, ADVEngine_AnalyseIf),
	    HLL_EXPORT(NumofIf, ADVEngine_NumofIf),
	    HLL_EXPORT(GetIf, ADVEngine_GetIf),
	    HLL_EXPORT(AnalyseFunction, ADVEngine_AnalyseFunction),
	    HLL_EXPORT(NumofStep, ADVEngine_NumofStep),
	    HLL_EXPORT(GetStepText, ADVEngine_GetStepText),
	    HLL_EXPORT(OpenFunctionFile, ADVEngine_OpenFunctionFile),
	    HLL_EXPORT(GetCodeType, ADVEngine_GetCodeType),
	    HLL_EXPORT(GetMessage, ADVEngine_GetMessage),
	    HLL_EXPORT(BindFunction, ADVEngine_BindFunction),
	    HLL_EXPORT(GetFuncName, ADVEngine_GetFuncName),
	    HLL_EXPORT(CreateDelegateObjectID, ADVEngine_CreateDelegateObjectID),
	    HLL_EXPORT(GetArgumentValue_int, ADVEngine_GetArgumentValue_int),
	    HLL_EXPORT(GetArgumentValue_float, ADVEngine_GetArgumentValue_float),
	    HLL_EXPORT(GetArgumentValue_bool, ADVEngine_GetArgumentValue_bool),
	    HLL_EXPORT(GetArgumentValue_string, ADVEngine_GetArgumentValue_string));
