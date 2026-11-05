/* SPDX-License-Identifier: GPL-2.0-or-later */
/* AIN11+ VSFile streams belong to the assigned save profile. */
#ifndef XSYSTEM4_VSFILE_PATH_H
#define XSYSTEM4_VSFILE_PATH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "system4/ain.h"
#include "system4/string.h"
#include "vm.h"
#include "xsystem4.h"

#define VSFILE_MAX_FILENAME 4096u

static unsigned char vsfile_ascii_upper(unsigned char ch)
{
	return ch>='a' && ch<='z' ? ch-'a'+'A' : ch;
}

static bool vsfile_device_component(const char *start,size_t size)
{
	/* Windows accepts these as device names even with an extension. */
	size_t n=0; while(n<size && start[n]!='.') n++;
	while(n && start[n-1]==' ') n--;
	if(n==3) {
		unsigned char a=vsfile_ascii_upper(start[0]),b=vsfile_ascii_upper(start[1]),c=vsfile_ascii_upper(start[2]);
		return (a=='C' && b=='O' && c=='N') || (a=='P' && b=='R' && c=='N') ||
			(a=='A' && b=='U' && c=='X') || (a=='N' && b=='U' && c=='L');
	}
	if(n==4 && start[3]>='0' && start[3]<='9') {
		unsigned char a=vsfile_ascii_upper(start[0]),b=vsfile_ascii_upper(start[1]),c=vsfile_ascii_upper(start[2]);
		return (a=='C' && b=='O' && c=='M') || (a=='L' && b=='P' && c=='T');
	}
	return false;
}

static bool vsfile_relative_path(const char *utf)
{
	if(!utf || !*utf || *utf=='/' || *utf=='\\') return false;
	const char *segment=utf;
	for(const char *at=utf;;at++) {
		unsigned char ch=(unsigned char)*at;
		if(ch && (ch<0x20 || ch==0x7f || ch==':' || ch=='\\' || ch=='<' || ch=='>' ||
			ch=='"' || ch=='|' || ch=='?' || ch=='*')) return false;
		if(ch=='/' || !ch) {
			size_t n=(size_t)(at-segment);
			/* Dot/parent segments and Win32's trailing-dot/space aliases are
			 * rejected, so path joining cannot turn a filename into a parent. */
			if(!n || segment[n-1]=='.' || segment[n-1]==' ' || vsfile_device_component(segment,n)) return false;
			if(!ch) return true;
			segment=at+1;
		}
	}
}

static char *vsfile_resolve_path(const struct string *filename)
{
	if(!filename || filename->size<0) return NULL;
	if(!ain || ain->version<11 || !config.save_dir)
		return gamedir_path(filename->text);
	if(!*config.save_dir || !filename->size || (unsigned)filename->size>VSFILE_MAX_FILENAME ||
		memchr(filename->text,0,(size_t)filename->size)) return NULL;
	/* Decode before checking separators: 0x5c can be a SJIS trail byte. */
	char *utf=unix_path(filename->text);
	bool safe=vsfile_relative_path(utf);
	free(utf);
	return safe ? savedir_path(filename->text) : NULL;
}

#endif
