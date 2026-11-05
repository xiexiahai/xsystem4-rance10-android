/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PARTS_TEXTBOX_UTF8_H
#define PARTS_TEXTBOX_UTF8_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct textbox_utf8 {
    char *text;
    size_t len, cursor, anchor, limit;
    bool multiline;
};

static inline size_t tb_utf8_seq(const unsigned char *s,size_t n)
{
    if(!n)return 0;
    unsigned char c=s[0];
    if(c<0x80)return 1;
    if(c<0xC2)return 0;
    if(c<0xE0) {
        if(n<2 || (s[1]&0xC0)!=0x80)return 0;
        return 2;
    }
    if(c<0xF0) {
        if(n<3 || (s[1]&0xC0)!=0x80 || (s[2]&0xC0)!=0x80)return 0;
        if(c==0xE0 && s[1]<0xA0)return 0;
        if(c==0xED && s[1]>=0xA0)return 0;
        return 3;
    }
    if(c<0xF5) {
        if(n<4 || (s[1]&0xC0)!=0x80 || (s[2]&0xC0)!=0x80 ||
                (s[3]&0xC0)!=0x80)return 0;
        if(c==0xF0 && s[1]<0x90)return 0;
        if(c==0xF4 && s[1]>0x8F)return 0;
        return 4;
    }
    return 0;
}

static inline size_t tb_next(const char *s,size_t len,size_t offset)
{
    if(!s || offset>=len)return len;
    size_t n=tb_utf8_seq((const unsigned char*)s+offset,len-offset);
    return n?offset+n:len;
}
static inline size_t tb_prev(const char *s,size_t len,size_t offset)
{
    if(!s || offset==0)return 0;
    if(offset>len)offset=len;
    size_t i=offset-1,min=offset>4?offset-4:0;
    while(i>min && (((unsigned char)s[i])&0xC0)==0x80)i--;
    size_t n=tb_utf8_seq((const unsigned char*)s+i,offset-i);
    return n==offset-i?i:0;
}

static inline bool tb_normalize(const char *s,bool multiline,char **out,
        size_t *out_len,size_t *scalars)
{
    if(!s || !out || !out_len || !scalars)return false;
    size_t n=strlen(s);
    if(n>262144)return false;
    char *dst=malloc(n+1);
    if(!dst)return false;
    size_t o=0,count=0;
    for(size_t i=0;i<n;) {
        unsigned char c=(unsigned char)s[i];
        if(c<0x80) {
            if(c=='\r' || c=='\n' || c=='\t') {
                if(c=='\r' && i+1<n && s[i+1]=='\n')i++;
                dst[o++]=c=='\t'?' ':(multiline?'\n':' ');
                i++;count++;
            } else if(c<0x20 || c==0x7F) {
                free(dst);return false;
            } else {
                dst[o++]=s[i++];count++;
            }
        } else {
            size_t k=tb_utf8_seq((const unsigned char*)s+i,n-i);
            if(!k) {free(dst);return false;}
            memcpy(dst+o,s+i,k);o+=k;i+=k;count++;
        }
    }
    dst[o]=0;*out=dst;*out_len=o;*scalars=count;
    return true;
}
static inline size_t tb_count(const char *s,size_t len)
{
    size_t n=0;
    for(size_t i=0;i<len;) {
        size_t k=tb_utf8_seq((const unsigned char*)s+i,len-i);
        if(!k)return (size_t)-1;
        i+=k;n++;
    }
    return n;
}
static inline size_t tb_clamp_limit(size_t limit)
{
    return limit>65536?65536:limit;
}
static inline bool tb_init(struct textbox_utf8 *b,size_t limit,bool multiline)
{
    if(!b)return false;
    char *text=malloc(1);
    if(!text)return false;
    text[0]=0;
    free(b->text);
    b->text=text;b->len=b->cursor=b->anchor=0;
    b->limit=tb_clamp_limit(limit);b->multiline=multiline;
    return true;
}
static inline void tb_free(struct textbox_utf8 *b)
{
    if(!b)return;
    free(b->text);memset(b,0,sizeof(*b));
}
static inline bool tb_set(struct textbox_utf8 *b,const char *s)
{
    if(!b)return false;
    char *text=NULL;size_t len=0,count=0;
    if(!tb_normalize(s,b->multiline,&text,&len,&count))return false;
    if(count>b->limit){free(text);return false;}
    free(b->text);b->text=text;b->len=len;b->cursor=b->anchor=len;
    return true;
}
static inline bool tb_insert(struct textbox_utf8 *b,const char *s)
{
    if(!b)return false;
    char *ins=NULL;size_t ins_len=0,ins_count=0;
    if(!tb_normalize(s,b->multiline,&ins,&ins_len,&ins_count))return false;
    size_t start=b->cursor<b->anchor?b->cursor:b->anchor;
    size_t end=b->cursor<b->anchor?b->anchor:b->cursor;
    size_t total=tb_count(b->text,b->len);
    size_t removed=tb_count(b->text+start,end-start);
    if(total==(size_t)-1 || removed==(size_t)-1){free(ins);return false;}
    size_t outside=total-removed;
    size_t remain=b->limit>outside?b->limit-outside:0;
    size_t take=0,take_bytes=0;
    while(take<remain) {
        size_t k=tb_utf8_seq((const unsigned char*)ins+take_bytes,ins_len-take_bytes);
        if(!k)break;
        take++;take_bytes+=k;
    }
    size_t len=start+take_bytes+(b->len-end);
    char *text=malloc(len+1);
    if(!text){free(ins);return false;}
    memcpy(text,b->text,start);
    memcpy(text+start,ins,take_bytes);
    memcpy(text+start+take_bytes,b->text+end,b->len-end+1);
    free(b->text);free(ins);
    b->text=text;b->len=len;b->cursor=b->anchor=start+take_bytes;
    return true;
}
static inline bool tb_erase(struct textbox_utf8 *b,bool backwards)
{
    if(!b)return false;
    size_t start=b->cursor<b->anchor?b->cursor:b->anchor;
    size_t end=b->cursor<b->anchor?b->anchor:b->cursor;
    if(start==end) {
        if(backwards) {
            if(b->cursor==0)return true;
            start=tb_prev(b->text,b->len,b->cursor);end=b->cursor;
        } else {
            if(b->cursor>=b->len)return true;
            start=b->cursor;end=tb_next(b->text,b->len,b->cursor);
        }
    }
    memmove(b->text+start,b->text+end,b->len-end+1);
    b->len-=end-start;b->cursor=b->anchor=start;
    return true;
}
static inline void tb_move(struct textbox_utf8 *b,int dir,bool select)
{
    if(!b || (dir!=1 && dir!=-1))return;
    size_t pos=dir<0?tb_prev(b->text,b->len,b->cursor):
        tb_next(b->text,b->len,b->cursor);
    b->cursor=pos;
    if(!select)b->anchor=pos;
}
static inline void tb_home(struct textbox_utf8 *b,bool end,bool select)
{
    if(!b)return;
    size_t pos=b->cursor;
    if(end) {
        while(pos<b->len && b->text[pos]!='\n')pos++;
    } else {
        while(pos>0 && b->text[pos-1]!='\n')pos--;
    }
    b->cursor=pos;
    if(!select)b->anchor=pos;
}
static inline void tb_select_all(struct textbox_utf8 *b)
{
    if(!b)return;
    b->anchor=0;b->cursor=b->len;
}
static inline bool tb_set_limit(struct textbox_utf8 *b,size_t limit)
{
    if(!b)return false;
    limit=tb_clamp_limit(limit);
    size_t count=tb_count(b->text,b->len);
    if(count==(size_t)-1)return false;
    if(limit>=count){b->limit=limit;return true;}
    size_t bytes=0,n=0;
    while(n<limit) {bytes=tb_next(b->text,b->len,bytes);n++;}
    char *text=malloc(bytes+1);
    if(!text)return false;
    memcpy(text,b->text,bytes);text[bytes]=0;
    free(b->text);b->text=text;b->len=bytes;b->limit=limit;
    if(b->cursor>bytes)b->cursor=bytes;
    if(b->anchor>bytes)b->anchor=bytes;
    return true;
}

#endif /* PARTS_TEXTBOX_UTF8_H */
