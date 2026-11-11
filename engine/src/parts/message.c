/* Copyright (C) 2026 kichikuou <KichikuouChrome@gmail.com>
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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"

#include "vm.h"
#include "input.h"
#include "parts.h"
#include "parts_internal.h"

#define PARTS_MSG_MAX_VARS 5

struct parts_message {
	STAILQ_ENTRY(parts_message) entry;
	int parts_no;
	int delegate_index;
	int type;
	int variables[PARTS_MSG_MAX_VARS];
	int types[PARTS_MSG_MAX_VARS];
	int nr_variables;
};

static STAILQ_HEAD(, parts_message) msg_queue =
		STAILQ_HEAD_INITIALIZER(msg_queue);

static int probe_refusal_number,probe_refusal_at;
void parts_probe_schedule_refusal(int number)
{
	if(!getenv("XSYSTEM4_TEST_REJECT_NETWORK")) return;
	probe_refusal_number=number;probe_refusal_at=vm_time()+500;
}
void parts_probe_update(void)
{
	if(!probe_refusal_number || vm_time()<probe_refusal_at) return;
	struct parts *p=parts_try_get(probe_refusal_number);probe_refusal_number=0;
	if(!p) VM_ERROR("Probe refusal button was released before delivery");
	parts_msg_push(p,PARTS_MSG_MOUSE_CLICK,"iii",0,0,VK_LBUTTON);
	fprintf(stderr,"[probe] delivered CancelButton refusal event: %d\n",p->no);
}

void parts_msg_bind_delegate(int parts_no,int delegate_index)
{
	// PACT initialization can queue a value change before the script attaches
	// its listener. Bind those pending events, preserving already-bound ones.
	struct parts_message *msg;
	STAILQ_FOREACH(msg,&msg_queue,entry)
		if (msg->parts_no==parts_no && msg->delegate_index<0)
			msg->delegate_index=delegate_index;
}

void parts_msg_release_parts(int parts_no)
{
	struct parts_message *msg=STAILQ_FIRST(&msg_queue),*next;
	while (msg) {
		next=STAILQ_NEXT(msg,entry);
		if (msg->parts_no==parts_no) {
			STAILQ_REMOVE(&msg_queue,msg,parts_message,entry);
			free(msg);
		}
		msg=next;
	}
}

void parts_msg_push(struct parts* parts, int type, const char *fmt, ...)
{
	// Message system is introduced in Rance 9
	if (!parts_multi_controller)
		return;

	struct parts_message *msg = xmalloc(sizeof(*msg));
	msg->parts_no = parts ? parts->no : 0;
	msg->delegate_index = parts ? parts->delegate_index : -1;
	msg->type = type;
	msg->nr_variables = 0;

	va_list ap;
	va_start(ap, fmt);
	for (const char *p = fmt; *p; p++) {
		if (msg->nr_variables >= PARTS_MSG_MAX_VARS) {
			VM_ERROR("parts_msg_push: too many variables");
		}
		switch (*p) {
		case 'i':
			msg->types[msg->nr_variables] = 1;
			msg->variables[msg->nr_variables++] = va_arg(ap, int);
			break;
		case 'b':
			msg->types[msg->nr_variables] = 3;
			msg->variables[msg->nr_variables++] = !!va_arg(ap, int);
			break;
		default:
			VM_ERROR("parts_msg_push: unsupported format '%c'", *p);
		}
	}
	va_end(ap);

	STAILQ_INSERT_TAIL(&msg_queue, msg, entry);
}

void PE_ReleaseMessage(void)
{
	while (!STAILQ_EMPTY(&msg_queue)) {
		struct parts_message *msg = STAILQ_FIRST(&msg_queue);
		STAILQ_REMOVE_HEAD(&msg_queue, entry);
		free(msg);
	}
}

void PE_PopMessage(void)
{
	if (STAILQ_EMPTY(&msg_queue))
		return;
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	STAILQ_REMOVE_HEAD(&msg_queue, entry);
	free(msg);
}

int PE_GetMessageType(void)
{
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	if(!msg) return ain && ain->version>=11?-1:0;
	if(ain && ain->version>=11) {
		static const int types[]={
			[PARTS_MSG_MOUSE_ENTER]=0,[PARTS_MSG_MOUSE_MOVE]=1,[PARTS_MSG_MOUSE_LEAVE]=2,
			[PARTS_MSG_MOUSE_WHEEL]=3,[PARTS_MSG_MOUSE_CLICK]=4,[PARTS_MSG_MOUSE_ON]=6,
			[PARTS_MSG_DRAG_BEGIN]=7,[PARTS_MSG_DRAGGING]=8,[PARTS_MSG_DRAG_END]=10,
			[PARTS_MSG_DROP_ENTER]=11,[PARTS_MSG_DROP_ON]=12,[PARTS_MSG_DROPPED]=13,[PARTS_MSG_DROP_LEAVE]=14,
			[PARTS_MSG_KEY_TRIGGER]=15,[PARTS_MSG_KEY_DOWN]=16,[PARTS_MSG_KEY_PRESS]=17,[PARTS_MSG_KEY_UP]=18,
			[PARTS_MSG_FOCUS]=19,[PARTS_MSG_LOST_FOCUS]=20,[PARTS_MSG_SCROLL]=21,[PARTS_MSG_CHANGED_TEXT]=22,[PARTS_MSG_CHANGED_FLAG]=23,
			[PARTS_MSG_SWIPE_BEGIN]=27,[PARTS_MSG_SWIPING]=28,[PARTS_MSG_SWIPE_END]=29,
			[PARTS_MSG_LIST_SELECTED]=25,[PARTS_MSG_FORM_CLOSING]=30
		};
		if(msg->type<=0 || msg->type>=(int)(sizeof(types)/sizeof(*types)))
			VM_ERROR("Unmapped AIN12 part message %d",msg->type);
		return types[msg->type];
	}
	return msg->type;
}

int PE_GetMessagePartsNumber(void)
{
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	return msg ? msg->parts_no : 0;
}

int PE_GetMessageDelegateIndex(void)
{
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	return msg ? msg->delegate_index : -1;
}

int PE_GetMessageVariableCount(void)
{
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	return msg ? msg->nr_variables : 0;
}

int PE_GetMessageVariableType(int index)
{
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	return msg && index>=0 && index<msg->nr_variables ? msg->types[index] : 0;
}

int PE_GetMessageVariableInt(int index)
{
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	if (!msg)
		return 0;
	if (index < 0 || index >= msg->nr_variables)
		return 0;
	return msg->variables[index];
}

float PE_GetMessageVariableFloat(possibly_unused int index)
{
	return 0.0f;
}

bool PE_GetMessageVariableBool(int index)
{
	return PE_GetMessageVariableType(index)==3 && PE_GetMessageVariableInt(index);
}

void PE_GetMessageVariableString(possibly_unused int index, struct string **out)
{
	if (*out)
		free_string(*out);
	*out = string_ref(&EMPTY_STRING);
}
