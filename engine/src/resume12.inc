/* Copyright (C) 2026 xsystem4 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Project R10VM1 images retain AIN11+ runtime metadata. Not native RSM. */
#include <math.h>
#include <limits.h>
#include <zlib.h>
#include "system4/instructions.h"
#include "system4/little_endian.h"
#include "resume12_types.inc"
#include "r12_save_perf.h"

#define R12_FILE_LIMIT (256u*1024u*1024u)
#define R12_HEAP_LIMIT (1024u*1024u)
#define R12_WORD_LIMIT (16u*1024u*1024u)
#define R12_STRING_LIMIT (16u*1024u*1024u)
static const char *r12_stage;
static int r12_problem_slot,r12_problem_type,r12_problem_value,r12_problem_word;
static void r12_reset_problem(const char *stage)
{r12_stage=stage;r12_problem_slot=-1;r12_problem_type=0;r12_problem_value=0;r12_problem_word=-1;}
struct r12_image {
    struct vm_pointer *heap;
    int32_t *free_slots;
    size_t cap,used;
    uint32_t seq;
    union vm_value *stack;
    int sp,fp;
    struct function_call frames[4096];
    size_t ip;
};
static bool r12_number(const cJSON *j,double lo,double hi,double *out)
{
    if(!cJSON_IsNumber(j) || !isfinite(j->valuedouble) || floor(j->valuedouble)!=j->valuedouble || j->valuedouble<lo || j->valuedouble>hi) return false;
    *out=j->valuedouble;return true;
}
static bool r12_int(const cJSON *j,int *out)
{double v;if(!r12_number(j,INT32_MIN,INT32_MAX,&v))return false;*out=(int)v;return true;}
static bool r12_field(const cJSON *j,const char *key,int *out)
{return r12_int(cJSON_GetObjectItemCaseSensitive(j,key),out);}
static bool r12_bool(const cJSON *j,const char *key,bool *out)
{const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);if(!cJSON_IsBool(v))return false;*out=cJSON_IsTrue(v);return true;}
static cJSON *r12_words(const union vm_value *v,int n)
{return cJSON_CreateIntArray_cb(n,get_number,(void*)v);}
static void r12_append(cJSON *array,cJSON **tail,cJSON *item)
{
    if(!item)return;
    if(*tail){(*tail)->next=item;item->prev=*tail;}else array->child=item;
    *tail=item;
}
static bool r12_put(cJSON *object,const char *key,cJSON *item)
{
    if(!item)return false;
    cJSON_AddItemToObject(object,key,item);
    if(cJSON_GetObjectItemCaseSensitive(object,key)==item)return true;
    cJSON_Delete(item);return false;
}
static void r12_free_image(struct r12_image *c)
{
    for(size_t i=0;c->heap && i<c->cap;i++)if(c->heap[i].ref) {
        if(c->heap[i].type==VM_STRING){if(c->heap[i].s)free_string(c->heap[i].s);}
        else if(c->heap[i].page) {
            /* Candidate ownership is flat. Never dereference the live heap. */
            free(c->heap[i].page->delegate_environments);
            free_page(c->heap[i].page);
        }
    }
    free(c->heap);free(c->free_slots);free(c->stack);memset(c,0,sizeof(*c));
}
static uint32_t r12_crc_type(uint32_t crc,const struct ain_type *t,int depth)
{
    if(!t || depth>32)return crc32(crc,(const Bytef*)"null",4);
    int32_t fields[]={t->data,t->struc,t->rank};crc=crc32(crc,(const Bytef*)fields,sizeof(fields));
    return t->array_type?r12_crc_type(crc,t->array_type,depth+1):crc;
}
static void r12_program_id_uncached(char out[64])
{
    uint32_t crc=crc32(0,ain->code,ain->code_size);
    int32_t header[]={ain->version,ain->minor_version,ain->nr_globals,ain->nr_functions,ain->nr_structures};
    crc=crc32(crc,(const Bytef*)header,sizeof(header));
    for(int i=0;i<ain->nr_globals;i++) {
        crc=crc32(crc,(const Bytef*)ain->globals[i].name,strlen(ain->globals[i].name)+1);
        crc=r12_crc_type(crc,&ain->globals[i].type,0);
    }
    for(int i=0;i<ain->nr_functions;i++) {
        struct ain_function *f=&ain->functions[i];
        crc=crc32(crc,(const Bytef*)f->name,strlen(f->name)+1);
        crc=crc32(crc,(const Bytef*)&f->address,sizeof(f->address));
        crc=r12_crc_type(crc,&f->return_type,0);
        for(int k=0;k<f->nr_vars;k++)crc=r12_crc_type(crc,&f->vars[k].type,0);
    }
    for(int i=0;i<ain->nr_structures;i++) {
        struct ain_struct *s=&ain->structures[i];crc=crc32(crc,(const Bytef*)s->name,strlen(s->name)+1);
        for(int k=0;k<s->nr_members;k++) {
            crc=crc32(crc,(const Bytef*)s->members[k].name,strlen(s->members[k].name)+1);
            crc=r12_crc_type(crc,&s->members[k].type,0);
        }
    }
    snprintf(out,64,"AIN%d.%d-%zu-%08x",ain->version,ain->minor_version,ain->code_size,crc);
}
#include "r12_program_cache.h"
static void r12_program_id_compute_timed(char out[64])
{
    Uint64 started = r12_save_perf_active ? SDL_GetPerformanceCounter() : 0;
    r12_program_id_uncached(out);
    if (r12_save_perf_active) {
        r12_save_perf_active->program_ticks += SDL_GetPerformanceCounter() - started;
        r12_save_perf_active->program_calculations++;
    }
}
static void r12_program_id(char out[64])
{
    if (r12_save_perf_active) r12_save_perf_active->program_calls++;
    r12_cached_program_id(ain, out, r12_program_id_compute_timed);
}
static char *r12_hex(const struct string *s)
{
    if(s->size<0 || (size_t)s->size>R12_STRING_LIMIT)return NULL;
    char *hex=malloc((size_t)s->size*2+1);if(!hex)return NULL;
    static const char digits[]="0123456789abcdef";
    for(size_t i=0;i<(size_t)s->size;i++){unsigned c=(unsigned char)s->text[i];hex[i*2]=digits[c>>4];hex[i*2+1]=digits[c&15];}
    hex[(size_t)s->size*2]=0;return hex;
}
static int r12_unhex_char(char ch)
{return ch>='0'&&ch<='9'?ch-'0':ch>='a'&&ch<='f'?ch-'a'+10:-1;}
static struct string *r12_unhex(const cJSON *j)
{
    if(!cJSON_IsString(j))return NULL;
    size_t n=strlen(j->valuestring);
    if(n%2 || n>R12_STRING_LIMIT*2)return NULL;
    char *bytes=malloc(n/2+1);if(!bytes)return NULL;
    for(size_t i=0;i<n;i+=2){int a=r12_unhex_char(j->valuestring[i]),b=r12_unhex_char(j->valuestring[i+1]);if(a<0||b<0){free(bytes);return NULL;}bytes[i/2]=(a<<4)|b;}
    struct string *s=make_string(bytes,n/2);free(bytes);return s;
}
static bool r12_write_json_timed(const char *path,cJSON *j,struct r12_save_perf *perf)
{
    r12_save_perf_phase(perf,R12_PERF_PRINT);
    char *data=cJSON_PrintUnformatted(j);if(!data){r12_save_perf_failure(perf);return false;}size_t len=strlen(data);
    if(len>R12_FILE_LIMIT){r12_save_perf_failure(perf);cJSON_free(data);return false;}
    r12_save_perf_phase(perf,R12_PERF_PERSIST);
    char *full=savedir_path(path);struct save_file file;
    bool ok=begin_save_file(&file,full);free(full);
    if(ok){bool written=fwrite(data,1,len,file.file)==len;ok=end_save_file(&file,written);}
    if(!ok)r12_save_perf_failure(perf);
    r12_save_perf_phase(perf,R12_PERF_CLEANUP);
    cJSON_free(data);return ok;
}
static bool r12_write_json(const char *path,cJSON *j)
{return r12_write_json_timed(path,j,NULL);}
static cJSON *r12_read_json(const char *path)
{
    r12_reset_problem("read-open");
    char *full=savedir_path(path);FILE *f=file_open_utf8(full,"rb");free(full);if(!f)return NULL;
    r12_stage="read-size";
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long len=ftell(f);
    if(len<2 || (unsigned long)len>R12_FILE_LIMIT || fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    r12_stage="read-buffer";char *data=malloc((size_t)len+1);if(!data){fclose(f);return NULL;}
    r12_stage="read-content";
    bool ok=fread(data,1,len,f)==(size_t)len;fclose(f);data[len]=0;
    if(!ok || memchr(data,0,len)){free(data);return NULL;}
    r12_stage="read-json";const char *end=NULL;cJSON *j=cJSON_ParseWithOpts(data,&end,false);
    while(end && end<data+len && (*end==' '||*end=='\t'||*end=='\r'||*end=='\n'))end++;
    if(!j || end!=data+len){cJSON_Delete(j);j=NULL;}free(data);return j;
}
static cJSON *r12_build_json(const char *key)
{
    if(heap_size>R12_HEAP_LIMIT || stack_ptr<2 || stack_ptr>4096 || call_stack_ptr<1 || call_stack_ptr>4096)return NULL;
    int key_slot=stack[stack_ptr-2].i,path_slot=stack[stack_ptr-1].i;
    if(!string_index_valid(key_slot)||!string_index_valid(path_slot))return NULL;
    cJSON *j=cJSON_CreateObject(),*items=cJSON_CreateArray();
    if(!j||!items){cJSON_Delete(j);cJSON_Delete(items);return NULL;}
    char id[64];r12_program_id(id);
    cJSON_AddStringToObject(j,"format","R10VM1");cJSON_AddStringToObject(j,"program",id);cJSON_AddStringToObject(j,"key",key);
    cJSON_AddNumberToObject(j,"heap-capacity",heap_size);cJSON_AddNumberToObject(j,"next-seq",heap_next_seq);
    cJSON_AddNumberToObject(j,"ip",instr_ptr);
    if(!r12_put(j,"heap",items)){cJSON_Delete(j);return NULL;}
    size_t words=0;cJSON *last_entry=NULL;
    for(size_t i=0;i<heap_size;i++)if(heap[i].ref) {
        int ref=heap[i].ref-(i==(size_t)key_slot)-(i==(size_t)path_slot);if(ref<=0)continue;
        cJSON *entry=cJSON_CreateArray();if(!entry)goto fail;r12_append(items,&last_entry,entry);
        cJSON_AddItemToArray(entry,cJSON_CreateNumber(i));cJSON_AddItemToArray(entry,cJSON_CreateNumber(ref));cJSON_AddItemToArray(entry,cJSON_CreateNumber(heap[i].seq));cJSON_AddItemToArray(entry,cJSON_CreateNumber(heap[i].type));
        if(heap[i].type==VM_STRING) {
            char *hex=r12_hex(heap[i].s);if(!hex)goto fail;cJSON_AddItemToArray(entry,cJSON_CreateString(hex));free(hex);continue;
        }
        struct page *p=heap[i].page;if(!p){cJSON_AddItemToArray(entry,cJSON_CreateNull());continue;}
        if(p->nr_vars<0 || (words+=(unsigned)p->nr_vars)>R12_WORD_LIMIT)goto fail;
        cJSON *page=cJSON_CreateObject();cJSON_AddItemToArray(entry,page);
        cJSON_AddNumberToObject(page,"type",p->type);cJSON_AddNumberToObject(page,"index",p->index);
        r12_put(page,"values",r12_words(p->values,p->nr_vars));
        if(p->type==ARRAY_PAGE) {
            cJSON_AddNumberToObject(page,"rank",p->array.rank);cJSON_AddNumberToObject(page,"struct-type",p->array.struct_type);
            cJSON *t=p->element_type?r12_type_encode(p->element_type):cJSON_CreateNull();if(!t)goto fail;r12_put(page,"element",t);
        } else if(p->type==LOCAL_PAGE) {
            cJSON_AddNumberToObject(page,"parent",p->local.parent);cJSON_AddBoolToObject(page,"parent-owned",p->local_parent_owned);
            cJSON_AddNumberToObject(page,"struct",p->local.struct_ptr);
        } else if(p->type==DELEGATE_PAGE) {
            cJSON *env=cJSON_CreateArray();if(!env||!r12_put(page,"environments",env))goto fail;
            for(int k=0;k<p->nr_vars/3;k++)cJSON_AddItemToArray(env,cJSON_CreateNumber(delegate_get_environment(p,k)));
        }
    }
    r12_put(j,"stack",r12_words(stack,stack_ptr-2));cJSON *frames=cJSON_CreateArray();
    if(!frames||!r12_put(j,"frames",frames))goto fail;
    for(int i=0;i<call_stack_ptr;i++) {
        const struct function_call *f=&call_stack[i];cJSON *row=cJSON_CreateArray();if(!row)goto fail;cJSON_AddItemToArray(frames,row);
        double vals[]={f->fno,f->call_address,f->return_address,f->page_slot,f->struct_page};
        for(int k=0;k<5;k++)cJSON_AddItemToArray(row,cJSON_CreateNumber(vals[k]));
    }
    if(!cJSON_GetObjectItemCaseSensitive(j,"frames") || cJSON_GetArraySize(frames)!=call_stack_ptr)goto fail;
    return j;
fail:cJSON_Delete(j);return NULL;
}
static bool r12_live(struct r12_image *c,int slot)
{return slot>=0 && (size_t)slot<c->cap && c->heap[slot].ref>0;}
static struct page *r12_page(struct r12_image *c,int slot,int type)
{return r12_live(c,slot)&&c->heap[slot].type==VM_PAGE&&c->heap[slot].page&&(type<0||(int)c->heap[slot].page->type==type)?c->heap[slot].page:NULL;}
static bool r12_decode(const cJSON *j,const char *key,struct r12_image *c);
static bool r12_validate(struct r12_image *c);
static bool r12_prepare(const cJSON *j,const char *key,struct r12_image *c)
{
    memset(c,0,sizeof(*c));r12_reset_problem("decode-header");
    if(r12_decode(j,key,c)){r12_stage="validate-header";if(r12_validate(c))return true;}
    r12_free_image(c);return false;
}
static int r12_save_image(const char *key,const char *path)
{
    struct r12_save_perf perf;r12_save_perf_begin(&perf);
    /* Preserve comments before constructing the next full image. Large Parts
     * snapshots contain millions of words: keep only one complete JSON DOM
     * live at a time while retaining the atomic file replacement below. */
    cJSON *kept_comments=NULL;
    cJSON *old=r12_read_json(path);
    if(old) {
        const cJSON *old_key=cJSON_GetObjectItemCaseSensitive(old,"key"),*comments=cJSON_GetObjectItemCaseSensitive(old,"comments-hex");
        if(cJSON_IsString(old_key)&&!strcmp(old_key->valuestring,key)&&cJSON_IsArray(comments)) {
            kept_comments=cJSON_Duplicate(comments,true);
            if(!kept_comments){cJSON_Delete(old);r12_save_perf_end(&perf,false);return 0;}
        }
        cJSON_Delete(old);
    } else {
        char *full=savedir_path(path);bool exists=file_exists(full);free(full);
        if(exists){WARNING("R10VM1: unreadable existing image retained");r12_save_perf_end(&perf,false);return 0;}
    }
    r12_save_perf_phase(&perf,R12_PERF_BUILD);
    cJSON *j=r12_build_json(key);
    if(!j){cJSON_Delete(kept_comments);WARNING("R10VM1: runtime image exceeds supported limits");r12_save_perf_end(&perf,false);return 0;}
    if(kept_comments && !r12_put(j,"comments-hex",kept_comments)){cJSON_Delete(j);r12_save_perf_end(&perf,false);return 0;}
    r12_save_perf_phase(&perf,R12_PERF_PREPARE);
    struct r12_image *c=calloc(1,sizeof(*c));bool ok=c && r12_prepare(j,key,c);
    if(!ok)r12_save_perf_failure(&perf);
    r12_save_perf_phase(&perf,R12_PERF_FREE_CANDIDATE);
    if(c){r12_free_image(c);free(c);}
    if(ok)ok=r12_write_json_timed(path,j,&perf);
    r12_save_perf_phase(&perf,R12_PERF_CLEANUP);
    cJSON_Delete(j);
    if(!ok)WARNING("R10VM1: image validation/write failed at %s slot=%d type=%d value=%d; previous file retained",r12_stage,r12_problem_slot,r12_problem_type,r12_problem_value);
    r12_save_perf_end(&perf,ok);
    return ok;
}

struct r12_type_cache {const cJSON *json;const struct ain_type *type;};
static bool r12_decode(const cJSON *j,const char *key,struct r12_image *c)
{
    char id[64];r12_program_id(id);const cJSON *format=cJSON_GetObjectItemCaseSensitive(j,"format");
    const cJSON *program=cJSON_GetObjectItemCaseSensitive(j,"program"),*name=cJSON_GetObjectItemCaseSensitive(j,"key");double n;
    r12_stage="decode-format";
    if(!cJSON_IsObject(j)||!cJSON_IsString(format)||strcmp(format->valuestring,"R10VM1"))return false;
    r12_stage="decode-program";
    if(!cJSON_IsString(program)||strcmp(program->valuestring,id))return false;
    r12_stage="decode-key";
    if(!cJSON_IsString(name)||strcmp(name->valuestring,key))return false;
    r12_stage="decode-header";
    if(!r12_number(cJSON_GetObjectItemCaseSensitive(j,"heap-capacity"),1,R12_HEAP_LIMIT,&n))return false;
    c->cap=n;
    if(!r12_number(cJSON_GetObjectItemCaseSensitive(j,"next-seq"),1,UINT32_MAX,&n))return false;
    c->seq=n;
    if(!r12_number(cJSON_GetObjectItemCaseSensitive(j,"ip"),0,ain->code_size?ain->code_size-1:0,&n))return false;
    c->ip=n;
    const cJSON *heap_json=cJSON_GetObjectItemCaseSensitive(j,"heap"),*stack_json=cJSON_GetObjectItemCaseSensitive(j,"stack"),*frames=cJSON_GetObjectItemCaseSensitive(j,"frames");
    if(!cJSON_IsArray(heap_json)||!cJSON_IsArray(stack_json)||!cJSON_IsArray(frames)||cJSON_GetArraySize(heap_json)>(int)c->cap)return false;
    c->sp=cJSON_GetArraySize(stack_json);c->fp=cJSON_GetArraySize(frames);
    if(c->sp>4095||c->fp<1||c->fp>4096)return false;
    c->heap=calloc(c->cap,sizeof(*c->heap));c->free_slots=calloc(c->cap,sizeof(int32_t));c->stack=calloc(4096,sizeof(*c->stack));
    struct r12_type_cache *cache=calloc(8192,sizeof(*cache));int cache_n=0;size_t words=0;
    if(!c->heap||!c->free_slots||!c->stack||!cache){free(cache);return false;}
    const cJSON *entry;
    cJSON_ArrayForEach(entry,heap_json) {
        int slot,ref,kind;double seq;
        if(!cJSON_IsArray(entry)||cJSON_GetArraySize(entry)!=5||!r12_int(cJSON_GetArrayItem(entry,0),&slot)||slot<0||(size_t)slot>=c->cap||c->heap[slot].ref||!r12_int(cJSON_GetArrayItem(entry,1),&ref)||ref<=0||!r12_number(cJSON_GetArrayItem(entry,2),0,UINT32_MAX,&seq)||!r12_int(cJSON_GetArrayItem(entry,3),&kind)||(kind!=VM_PAGE&&kind!=VM_STRING))goto fail;
        r12_stage="decode-object";r12_problem_slot=slot;r12_problem_word=-1;
        struct vm_pointer *h=&c->heap[slot];h->ref=ref;h->seq=seq;h->type=kind;
        const cJSON *payload=cJSON_GetArrayItem(entry,4);
        if(kind==VM_STRING){h->s=r12_unhex(payload);if(!h->s)goto fail;continue;}
        if(cJSON_IsNull(payload))continue;
        int type,index;
        const cJSON *values=cJSON_GetObjectItemCaseSensitive(payload,"values");
        if(!cJSON_IsObject(payload)||!r12_field(payload,"type",&type)||type<GLOBAL_PAGE||type>DELEGATE_PAGE||!r12_field(payload,"index",&index)||!cJSON_IsArray(values))goto fail;
        int count=cJSON_GetArraySize(values);if((words+=(unsigned)count)>R12_WORD_LIMIT)goto fail;
        struct page *p=alloc_page(type,index,count);h->page=p;int k=0;const cJSON *v;
        cJSON_ArrayForEach(v,values)if(!r12_int(v,&p->values[k++].i))goto fail;
        if(type==ARRAY_PAGE) {
            if(!r12_field(payload,"rank",&p->array.rank)||p->array.rank<1||p->array.rank>4||!r12_field(payload,"struct-type",&p->array.struct_type))goto fail;
            const cJSON *element=cJSON_GetObjectItemCaseSensitive(payload,"element");if(!element)goto fail;
            if(!cJSON_IsNull(element)) {
                const struct ain_type *resolved=NULL;
                for(k=0;k<cache_n;k++)if(cJSON_Compare(cache[k].json,element,true)){resolved=cache[k].type;break;}
                if(!resolved){r12_stage="resolve-element";resolved=r12_type_resolve(element,ain);if(!resolved||cache_n==8192)goto fail;cache[cache_n++]=(struct r12_type_cache){element,resolved};}
                page_set_element_type(p,resolved);
            }
        } else if(type==LOCAL_PAGE) {
            if(!r12_field(payload,"parent",&p->local.parent)||!r12_bool(payload,"parent-owned",&p->local_parent_owned)||!r12_field(payload,"struct",&p->local.struct_ptr))goto fail;
        } else if(type==DELEGATE_PAGE) {
            const cJSON *env=cJSON_GetObjectItemCaseSensitive(payload,"environments");
            if(count%3 || !cJSON_IsArray(env)||cJSON_GetArraySize(env)!=count/3)goto fail;
            if(count){p->delegate_environments=calloc(count/3,sizeof(int));if(!p->delegate_environments)goto fail;}
            k=0;cJSON_ArrayForEach(v,env)if(!r12_int(v,&p->delegate_environments[k++]))goto fail;
        }
    }
    r12_stage="decode-stack";r12_problem_slot=-1;
    int k=0;const cJSON *v;cJSON_ArrayForEach(v,stack_json)if(!r12_int(v,&c->stack[k++].i))goto fail;
    r12_stage="decode-frame";k=0;cJSON_ArrayForEach(entry,frames) {
        r12_problem_slot=k;
        if(!cJSON_IsArray(entry)||cJSON_GetArraySize(entry)!=5)goto fail;
        struct function_call *f=&c->frames[k++];
        if(!r12_int(cJSON_GetArrayItem(entry,0),&f->fno)||!r12_number(cJSON_GetArrayItem(entry,1),0,UINT32_MAX,&n))goto fail;
        f->call_address=n;
        if(!r12_number(cJSON_GetArrayItem(entry,2),0,UINT32_MAX,&n))goto fail;
        f->return_address=n;
        if(!r12_int(cJSON_GetArrayItem(entry,3),&f->page_slot)||!r12_int(cJSON_GetArrayItem(entry,4),&f->struct_page))goto fail;
    }
    free(cache);return true;
fail:free(cache);return false;
}
static bool r12_value(struct r12_image *c,enum ain_data_type type,int value)
{
    if(type==AIN_STRING)return value==-1 || (r12_live(c,value)&&c->heap[value].type==VM_STRING);
    switch(type) {
    case AIN_STRUCT:case AIN_IFACE:
        return value==-1 || r12_page(c,value,STRUCT_PAGE);
    case AIN_WRAP:case AIN_IFACE_WRAP:
        /* These are non-owning handles: page.c does not ref/unref their
         * targets. Retain stale handles without treating them as strong refs. */
        return value==-1 || (value>=0 && (size_t)value<c->cap);
    case AIN_ARRAY:case AIN_ARRAY_TYPE:
        return value==-1 || (r12_live(c,value)&&c->heap[value].type==VM_PAGE&&(!c->heap[value].page||c->heap[value].page->type==ARRAY_PAGE));
    case AIN_DELEGATE:
        return value==-1 || (r12_live(c,value)&&c->heap[value].type==VM_PAGE&&(!c->heap[value].page||c->heap[value].page->type==DELEGATE_PAGE));
    case AIN_REF_TYPE:
        return value==-1 || (r12_live(c,value)&&c->heap[value].type==VM_PAGE);
    default:return true;
    }
}
static bool r12_typed_value(struct r12_image *c,const struct ain_type *t,int value)
{
    if(!r12_value(c,t->data,value))return false;
    if(value==-1)return true;
    if(t->data==AIN_STRUCT && t->struc>=0) {
        struct page *p=r12_page(c,value,STRUCT_PAGE);
        return p && p->index==t->struc;
    }
    if(t->data==AIN_ARRAY && t->array_type && r12_live(c,value) && c->heap[value].type==VM_PAGE) {
        struct page *p=c->heap[value].page;
        /* Native HLL output buffers can use legacy array pages even when
         * their AIN11+ caller declares ARRAY<T>. Bind only an exact shape and
         * element match, on the detached candidate, before publishing it. */
        if(!p && t->rank>=1 && t->rank<=4) {
            p=alloc_page(ARRAY_PAGE,AIN_ARRAY,0);page_set_element_type(p,t->array_type);
            p->array.rank=t->rank;p->array.struct_type=t->array_type->struc;c->heap[value].page=p;
        }
        if(!p)return true;
        if(t->rank>0 && p->array.rank!=t->rank)return false;
        if(!p->element_type && p->index!=AIN_ARRAY) {
            switch(p->index){case AIN_ARRAY_TYPE:case AIN_REF_ARRAY_TYPE:break;default:return false;}
            enum ain_data_type element=array_type(p->index);
            if(element!=t->array_type->data)return false;
            if(element==AIN_STRUCT && p->array.struct_type!=t->array_type->struc)return false;
            page_set_element_type(p,t->array_type);p->index=AIN_ARRAY;
        }
        if(t->array_type && (!p->element_type || !r12_type_matches(p->element_type,t->array_type,0)))return false;
    }
    return true;
}
static bool r12_validate(struct r12_image *c)
{
    if(!r12_page(c,0,GLOBAL_PAGE)||r12_page(c,0,GLOBAL_PAGE)->nr_vars!=ain->nr_globals)return false;
    /* Validate instruction boundaries before committing any heap state. */
    uint8_t *boundary=calloc(ain->code_size+1,1);if(!boundary)return false;
    for(size_t ip=0;ip<ain->code_size;) {
        if(ain->code_size-ip<2)goto fail;
        int op=LittleEndian_getW(ain->code,ip);
        if(op<0||op>=NR_OPCODES)goto fail;
        size_t width=instruction_width(op);
        if(width>ain->code_size-ip)goto fail;
        boundary[ip]=1;ip+=width;
    }
    if(c->ip>=ain->code_size||!boundary[c->ip])goto fail;
    uint32_t highest=0;size_t free_at=0;
    for(size_t i=0;i<c->cap;i++)if(c->heap[i].ref) {
        r12_stage="validate-page";r12_problem_slot=i;r12_problem_word=-1;r12_problem_type=0;r12_problem_value=0;
        c->free_slots[c->used++]=i;highest=max(highest,c->heap[i].seq);
        if(c->heap[i].type==VM_STRING)continue;
        struct page *p=c->heap[i].page;if(!p)continue;
        struct ain_variable *vars=NULL;
        if(p->type==GLOBAL_PAGE){if(i!=0||p->nr_vars!=ain->nr_globals)goto fail;vars=ain->globals;}
        else if(p->type==LOCAL_PAGE) {
            if(p->index<0||p->index>=ain->nr_functions||p->nr_vars!=ain->functions[p->index].nr_vars)goto fail;
            vars=ain->functions[p->index].vars;
            if(p->local.parent<-1 || (p->local.parent>=0&&!r12_page(c,p->local.parent,LOCAL_PAGE)))goto fail;
            if(p->local_parent_owned!=(p->local.parent>=0))goto fail;
            if(p->local.struct_ptr!=-1&&!r12_page(c,p->local.struct_ptr,STRUCT_PAGE))goto fail;
            int at=i;
            for(int depth=0;;depth++) {struct page *ancestor=r12_page(c,at,LOCAL_PAGE);if(!ancestor||ancestor->local.parent<0)break;if(depth>=1024)goto fail;at=ancestor->local.parent;}
        } else if(p->type==STRUCT_PAGE) {
            if(p->index<0||p->index>=ain->nr_structures||p->nr_vars!=ain->structures[p->index].nr_members)goto fail;
            vars=ain->structures[p->index].members;
        } else if(p->type==DELEGATE_PAGE) {
            for(int k=0;k<p->nr_vars;k+=3) {
                int obj=p->values[k].i,fun=p->values[k+1].i,env=delegate_get_environment(p,k/3);
                if(obj<-1||(obj>=0&&(size_t)obj>=c->cap)||fun<1||fun>=ain->nr_functions||env<-1||(env>=0&&!r12_page(c,env,LOCAL_PAGE)))goto fail;
            }
        } else if(p->type==ARRAY_PAGE) {
            if(p->element_type) {
                r12_stage="validate-array-descriptor";if(p->index!=AIN_ARRAY)goto fail;
                if(p->element_type->data==AIN_IFACE&&p->array.rank==1&&p->nr_vars%2)goto fail;
            } else {
                switch(p->index){case AIN_ARRAY_TYPE:case AIN_REF_ARRAY_TYPE:break;default:goto fail;}
            }
        } else goto fail;
        for(int k=0;k<p->nr_vars;k++) {
            enum ain_data_type type=vars?vars[k].type.data:variable_type(p,k,NULL,NULL);
            if(p->type==DELEGATE_PAGE)break;
            r12_stage="validate-reference";r12_problem_word=k;r12_problem_type=type;r12_problem_value=p->values[k].i;if(!r12_value(c,type,p->values[k].i))goto fail;
            if(vars && !r12_typed_value(c,&vars[k].type,p->values[k].i))goto fail;
            if(p->type==ARRAY_PAGE && p->array.rank==1 && p->element_type && p->element_type->data!=AIN_IFACE && !r12_typed_value(c,p->element_type,p->values[k].i))goto fail;
            if(p->type==ARRAY_PAGE&&p->array.rank>1) {
                if(p->element_type) {
                    struct ain_type child_type={.data=AIN_ARRAY,.rank=p->array.rank-1,.array_type=(struct ain_type*)p->element_type};
                    if(!r12_typed_value(c,&child_type,p->values[k].i))goto fail;
                }
                struct page *child=r12_page(c,p->values[k].i,ARRAY_PAGE);
                if(!child||child->array.rank!=p->array.rank-1||child->index!=p->index)goto fail;
                if(p->element_type && (!child->element_type || !r12_type_matches(p->element_type,child->element_type,0)))goto fail;
            }
        }
    }
    r12_stage="validate-sequence";if(c->seq<=highest)goto fail;
    free_at=c->used;for(size_t i=0;i<c->cap;i++)if(!c->heap[i].ref)c->free_slots[free_at++]=i;
    for(int i=0;i<c->fp;i++) {
        r12_stage="validate-frame";r12_problem_slot=i;r12_problem_word=-1;r12_problem_type=0;r12_problem_value=0;
        struct function_call *f=&c->frames[i];struct page *local=r12_page(c,f->page_slot,LOCAL_PAGE);
        if(f->fno<0||f->fno>=ain->nr_functions||!local||local->index!=f->fno)goto fail;
        if(f->struct_page!=-1&&!r12_page(c,f->struct_page,STRUCT_PAGE))goto fail;
        if(f->call_address!=UINT32_MAX&&(f->call_address>=ain->code_size||!boundary[f->call_address]))goto fail;
        if(f->return_address!=UINT32_MAX&&(f->return_address>=ain->code_size||!boundary[f->return_address]))goto fail;
    }
    free(boundary);return true;
fail:free(boundary);return false;
}
static bool r12_load_image(const char *key,const char *path)
{
    cJSON *j=r12_read_json(path);
    if(!j){WARNING("R10VM1: load failed at %s; current VM retained",r12_stage);return false;}
    r12_stage="allocate-candidate";
    struct r12_image *c=calloc(1,sizeof(*c));bool ok=c&&r12_prepare(j,key,c);cJSON_Delete(j);
    if(!ok){
        WARNING("R10VM1: load failed at %s slot=%d word=%d type=%d value=%d; current VM retained",r12_stage,r12_problem_slot,r12_problem_word,r12_problem_type,r12_problem_value);
        if(c){r12_free_image(c);free(c);}return false;
    }
    /* No parser, resource loads, VM destructors or fallible allocations after
     * this point. Swap the complete candidate before releasing the old graph. */
    struct r12_image old={.heap=heap,.cap=heap_size,.free_slots=heap_free_stack,.stack=stack};
    heap=c->heap;heap_size=c->cap;heap_free_stack=c->free_slots;heap_free_ptr=c->used;heap_next_seq=c->seq;
    stack=c->stack;stack_ptr=c->sp;call_stack_ptr=c->fp;instr_ptr=c->ip;
    memcpy(call_stack,c->frames,c->fp*sizeof(*call_stack));
    c->heap=NULL;c->free_slots=NULL;c->stack=NULL;r12_free_image(&old);free(c);return true;
}

static bool r12_comment_header(const cJSON *j,const char *key)
{
    const cJSON *format=cJSON_GetObjectItemCaseSensitive(j,"format"),*name=cJSON_GetObjectItemCaseSensitive(j,"key");
    return cJSON_IsObject(j)&&cJSON_IsString(format)&&!strcmp(format->valuestring,"R10VM1")&&cJSON_IsString(name)&&!strcmp(name->valuestring,key);
}
static struct page *r12_load_comments(const char *key,const char *path,int *success)
{
    *success=0;cJSON *j=r12_read_json(path);if(!j)return NULL;
    const cJSON *comments=cJSON_GetObjectItemCaseSensitive(j,"comments-hex"),*v;
    if(!r12_comment_header(j,key)||!cJSON_IsArray(comments)||cJSON_GetArraySize(comments)>1024)goto fail;
    struct string *strings[1024]={0};int count=cJSON_GetArraySize(comments),n=0;
    cJSON_ArrayForEach(v,comments){strings[n]=r12_unhex(v);if(!strings[n]){for(int k=0;k<n;k++)free_string(strings[k]);goto fail;}n++;}
    struct ain_type element=AIN_STRING_TYPE;
    struct page *p=alloc_page(ARRAY_PAGE,AIN_ARRAY,count);page_set_element_type(p,&element);
    p->array.rank=1;p->array.struct_type=-1;
    for(int i=0;i<count;i++)p->values[i]=vm_int(heap_alloc_string(strings[i]));
    *success=1;cJSON_Delete(j);return p;
fail:cJSON_Delete(j);return NULL;
}
static int r12_write_comments(const char *key,const char *path,struct page *comments)
{
    if(!comments||comments->type!=ARRAY_PAGE||comments->array.rank!=1||comments->nr_vars>1024)return 0;
    for(int i=0;i<comments->nr_vars;i++)if(!string_index_valid(comments->values[i].i)||heap_get_string(comments->values[i].i)->size>1024*1024)return 0;
    cJSON *j=r12_read_json(path);
    if(!j) {
        char *full=savedir_path(path);bool exists=file_exists(full);free(full);if(exists)return 0;
        j=cJSON_CreateObject();cJSON_AddStringToObject(j,"format","R10VM1");cJSON_AddStringToObject(j,"key",key);cJSON_AddBoolToObject(j,"comments-only",true);
    }
    if(!r12_comment_header(j,key)){cJSON_Delete(j);return 0;}
    cJSON *a=cJSON_CreateArray();if(!a){cJSON_Delete(j);return 0;}
    for(int i=0;i<comments->nr_vars;i++) {
        char *hex=r12_hex(heap_get_string(comments->values[i].i));
        if(!hex){cJSON_Delete(a);cJSON_Delete(j);return 0;}
        cJSON *v=cJSON_CreateString(hex);free(hex);
        if(!v){cJSON_Delete(a);cJSON_Delete(j);return 0;}cJSON_AddItemToArray(a,v);
    }
    cJSON_DeleteItemFromObjectCaseSensitive(j,"comments-hex");
    if(!r12_put(j,"comments-hex",a)){cJSON_Delete(j);return 0;}
    bool ok=r12_write_json(path,j);cJSON_Delete(j);return ok;
}
