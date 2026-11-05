/* SPDX-License-Identifier: GPL-2.0-or-later
 * Detached R10GS1 node parsing. Never installs a VM heap or runs script code. */
#ifndef R10_GLOBAL_GRAPH_DECODE_H
#define R10_GLOBAL_GRAPH_DECODE_H
#include <math.h>
#include "global_graph_walk.h"
#include "global_graph_envelope.h"
#include "global_graph_frame_binding.h"
#include "resume12_types.inc"
#define G12_EXTERNAL_LIMIT 256
struct g12_external_binding {int id;struct g12_binding path;};
struct g12_candidate {
    struct vm_pointer *view;size_t capacity;unsigned global_refs;bool validated;
    int *external;struct g12_external_binding *bindings;size_t nr_bindings;
    bool expire_frames;
};
static bool g12_int(const cJSON *j,int *out)
{int32_t value;if(!r12_json_read_int32(j,&value))return false;*out=value;return true;}
static bool g12_field(const cJSON *j,const char *key,int *out)
{return g12_int(cJSON_GetObjectItemCaseSensitive(j,key),out);}
static bool g12_keys(const cJSON *j,const char *const *keys,size_t count)
{
    if(!cJSON_IsObject(j) || count>16)return false;
    unsigned seen=0;const cJSON *v;
    cJSON_ArrayForEach(v,j) {
        if(!v->string)return false;size_t i;
        for(i=0;i<count;i++)if(!strcmp(v->string,keys[i]))break;
        if(i==count || (seen&(1u<<i)))return false;seen|=1u<<i;
    }
    return seen==((1u<<count)-1);
}
static cJSON *g12_parse_json(const uint8_t *bytes,size_t size)
{
    if(!bytes || size<2 || size>G12_RAW_LIMIT)return NULL;
    // Schema1 strings are hex or ASCII identifiers. Escapes, embedded NUL
    // and non-ASCII cannot be emitted by its encoder. Reject them before the
    // C-string JSON API could hide an escaped NUL and a trailing suffix.
    for(size_t i=0;i<size;i++)if(!bytes[i] || bytes[i]=='\\' || bytes[i]>=128)return NULL;
    char *text=malloc(size+1);if(!text)return NULL;memcpy(text,bytes,size);text[size]=0;
    const char *end=NULL;cJSON *j=cJSON_ParseWithOpts(text,&end,false);
    while(end && end<text+size && (*end==' ' || *end=='\t' || *end=='\r' || *end=='\n'))end++;
    if(!j || end!=text+size){cJSON_Delete(j);j=NULL;}free(text);return j;
}
static int g12_unhex_char(char ch)
{return ch>='0'&&ch<='9'?ch-'0':ch>='a'&&ch<='f'?ch-'a'+10:-1;}
static struct string *g12_unhex(const cJSON *j)
{
    if(!cJSON_IsString(j) || !j->valuestring)return NULL;size_t n=strlen(j->valuestring);
    if(n%2 || n>G12_STRING_LIMIT*2u)return NULL;
    char *bytes=malloc(n/2+1);if(!bytes)return NULL;
    for(size_t i=0;i<n;i+=2) {
        int a=g12_unhex_char(j->valuestring[i]),b=g12_unhex_char(j->valuestring[i+1]);
        if(a<0 || b<0){free(bytes);return NULL;}bytes[i/2]=(a<<4)|b;
    }
    struct string *s=make_string(bytes,n/2);free(bytes);return s;
}
static void g12_candidate_free(struct g12_candidate *c)
{
    if(!c)return;
    for(size_t i=0;c->view && i<c->capacity;i++) {
        struct vm_pointer *h=&c->view[i];
        if(c->external && c->external[i])continue;
        if(h->type==VM_STRING){if(h->s)free_string(h->s);}
        else if(h->page){free(h->page->delegate_environments);free_page(h->page);}
    }
    free(c->view);free(c->external);free(c->bindings);memset(c,0,sizeof(*c));
}
static bool g12_id(struct g12_candidate *c,int id,bool global)
{return id==-1 || (id>=(global?0:1) && (size_t)id<c->capacity);}
static bool g12_nodes_decode_bound_policy(const cJSON *nodes,struct g12_candidate *c,const bool *allowed,bool expire_frames)
{
    if(!c)return false;memset(c,0,sizeof(*c));if(!cJSON_IsArray(nodes))return false;
    c->expire_frames=expire_frames;
    int n=cJSON_GetArraySize(nodes);if(n<0 || (unsigned)n>=G12_NODE_LIMIT)return false;
    c->capacity=(size_t)n+1;c->view=calloc(c->capacity,sizeof(*c->view));if(!c->view)return false;
    struct g12_type_cache {const cJSON *json;const struct ain_type *type;};
    size_t cache_count=0,cache_limit=n<8192?(n?n:1):8192;
    struct g12_type_cache *cache=calloc(cache_limit,sizeof(*cache));if(!cache)goto fail;
    c->external=calloc(c->capacity,sizeof(int));c->bindings=calloc(G12_EXTERNAL_LIMIT,sizeof(*c->bindings));
    if(!c->external || !c->bindings)goto fail;
    c->view[0].ref=1;c->view[0].type=VM_PAGE;c->view[0].page=alloc_page(GLOBAL_PAGE,0,0);
    size_t at=1,words=0,string_bytes=0;const cJSON *entry;
    cJSON_ArrayForEach(entry,nodes) {
        struct vm_pointer *h=&c->view[at];int kind;
        if(!g12_field(entry,"kind",&kind) || kind<VM_PAGE || kind>3)goto fail;
        if(kind>=2) {
            const char *keys[]={"kind","global","structure","path"};
            const char *frame_keys[]={"kind","function","local","root-structure","structure","path"};
            const cJSON *path=cJSON_GetObjectItemCaseSensitive(entry,"path");
            struct g12_binding b;g12_binding_clear(&b);
            if(!allowed || c->nr_bindings>=G12_EXTERNAL_LIMIT ||
               !(kind==2?g12_keys(entry,keys,4):g12_keys(entry,frame_keys,6)) ||
               !(kind==2?g12_field(entry,"global",&b.global):
                 (g12_field(entry,"function",&b.function) && g12_field(entry,"local",&b.local) &&
                  g12_field(entry,"root-structure",&b.root_structure))) ||
               !g12_field(entry,"structure",&b.structure) ||
               !cJSON_IsArray(path) || cJSON_GetArraySize(path)>G12_BINDING_DEPTH)goto fail;
            if(kind==2 && b.global<0)goto fail;
            b.depth=cJSON_GetArraySize(path);unsigned k=0;const cJSON *word;
            cJSON_ArrayForEach(word,path)if(!g12_int(word,&b.words[k++]))goto fail;
            int slot;
            if(!g12_resolve_external_binding(&b,allowed,&slot)) {
                if(!expire_frames || kind!=3 || !g12_expirable_frame_binding(&b))goto fail;
                slot=-1;
            }
            for(size_t i=0;i<c->nr_bindings;i++) {
                const struct g12_binding *prior=&c->bindings[i].path;
                if((slot>0 && c->external[c->bindings[i].id]==slot) ||
                   (slot<0 && prior->global==b.global && prior->function==b.function && prior->local==b.local &&
                    prior->root_structure==b.root_structure && prior->structure==b.structure && prior->depth==b.depth &&
                    !memcmp(prior->words,b.words,b.depth*sizeof(int))))goto fail;
            }
            c->bindings[c->nr_bindings++]=(struct g12_external_binding){at,b};
            c->external[at]=slot;if(slot>0)*h=heap[slot];at++;continue;
        }
        h->type=kind;h->ref=INT_MAX;h->seq=at++;
        if(kind==VM_STRING) {
            const char *keys[]={"kind","bytes"};
            if(!g12_keys(entry,keys,2) || !(h->s=g12_unhex(cJSON_GetObjectItemCaseSensitive(entry,"bytes"))))goto fail;
            if((size_t)h->s->size>G12_WORD_LIMIT*4u-string_bytes)goto fail;string_bytes+=h->s->size;
            continue;
        }
        const cJSON *null=cJSON_GetObjectItemCaseSensitive(entry,"payload");
        if(null) {const char *keys[]={"kind","payload"};if(!g12_keys(entry,keys,2) || !cJSON_IsNull(null))goto fail;continue;}
        int type,index;const cJSON *values=cJSON_GetObjectItemCaseSensitive(entry,"values");
        if(!g12_field(entry,"type",&type) || type<LOCAL_PAGE || type>DELEGATE_PAGE ||
           !g12_field(entry,"index",&index) || !cJSON_IsArray(values))goto fail;
        int count=cJSON_GetArraySize(values);if(count<0 || (size_t)count>G12_WORD_LIMIT-words)goto fail;words+=count;
        struct page *p=alloc_page(type,index,count);h->page=p;int k=0;const cJSON *v;
        cJSON_ArrayForEach(v,values)if(!g12_int(v,&p->values[k++].i))goto fail;
        if(type==ARRAY_PAGE) {
            const char *keys[]={"kind","type","index","values","rank","struct-type","element"};
            const cJSON *element=cJSON_GetObjectItemCaseSensitive(entry,"element");
            if(!g12_keys(entry,keys,7) || !g12_field(entry,"rank",&p->array.rank) ||
               !g12_field(entry,"struct-type",&p->array.struct_type))goto fail;
            if(!cJSON_IsNull(element)) {
                const struct ain_type *t=NULL;
                for(size_t j=0;j<cache_count;j++)if(cJSON_Compare(cache[j].json,element,true)){t=cache[j].type;break;}
                if(!t){t=r12_type_resolve(element,ain);if(!t || cache_count==cache_limit)goto fail;cache[cache_count++]=(struct g12_type_cache){element,t};}
                page_set_element_type(p,t);
            }
        } else if(type==LOCAL_PAGE) {
            const char *keys[]={"kind","type","index","values","parent","parent-owned","struct"};
            const cJSON *owned=cJSON_GetObjectItemCaseSensitive(entry,"parent-owned");
            if(!g12_keys(entry,keys,7) || !g12_field(entry,"parent",&p->local.parent) ||
               !g12_id(c,p->local.parent,false) || !cJSON_IsBool(owned) ||
               !g12_field(entry,"struct",&p->local.struct_ptr) || !g12_id(c,p->local.struct_ptr,false))goto fail;
            p->local_parent_owned=cJSON_IsTrue(owned);
        } else if(type==DELEGATE_PAGE) {
            const char *keys[]={"kind","type","index","values","environments"};
            const cJSON *env=cJSON_GetObjectItemCaseSensitive(entry,"environments");
            if(!g12_keys(entry,keys,5) || count%3 || !cJSON_IsArray(env) || cJSON_GetArraySize(env)!=count/3)goto fail;
            if(count){p->delegate_environments=malloc((size_t)(count/3)*sizeof(int));if(!p->delegate_environments)goto fail;}
            k=0;cJSON_ArrayForEach(v,env)if(!g12_int(v,&p->delegate_environments[k]) || !g12_id(c,p->delegate_environments[k++],false))goto fail;
        } else {const char *keys[]={"kind","type","index","values"};if(!g12_keys(entry,keys,4))goto fail;}
    }
    // Rebuild sequence guards within the detached ID domain; no live IDs occur.
    for(size_t i=1;i<c->capacity;i++) {
        struct vm_pointer *h=&c->view[i];if(h->type!=VM_PAGE || !h->page || h->page->type!=DELEGATE_PAGE)continue;
        struct page *p=h->page;
        for(int k=0;k<p->nr_vars;k+=3) {
            int obj=p->values[k].i,seq=p->values[k+2].i;
            if(!g12_id(c,obj,false) || (obj==-1?(seq!=0 && seq!=1):seq!=0))goto fail;
            if(obj>0)p->values[k+2].i=c->view[obj].seq;
        }
    }
    free(cache);return true;
fail:free(cache);g12_candidate_free(c);return false;
}
static bool g12_nodes_decode_bound(const cJSON *nodes,struct g12_candidate *c,const bool *allowed)
{return g12_nodes_decode_bound_policy(nodes,c,allowed,false);}
static bool g12_nodes_decode(const cJSON *nodes,struct g12_candidate *c)
{return g12_nodes_decode_bound(nodes,c,NULL);}
static bool g12_candidate_type(struct g12_candidate *c,const struct ain_type *t,int value,bool *changed)
{
    if(!t)return false;
    if(value==-1)return true;
    if(t->data==AIN_STRUCT && t->struc>=0) {
        if(!g12_id(c,value,false) || c->view[value].type!=VM_PAGE || !c->view[value].page)return false;
        return c->view[value].page->type==STRUCT_PAGE && c->view[value].page->index==t->struc;
    }
    if(t->data!=AIN_ARRAY || !t->array_type)return true;
    if(!g12_id(c,value,false) || c->view[value].type!=VM_PAGE || t->rank<1 || t->rank>4)return false;
    struct page *p=c->view[value].page;
    if(!p) {
        p=alloc_page(ARRAY_PAGE,AIN_ARRAY,0);p->array.rank=t->rank;p->array.struct_type=t->array_type->struc;
        page_set_element_type(p,t->array_type);c->view[value].page=p;*changed=true;return true;
    }
    if(p->type!=ARRAY_PAGE || p->array.rank!=t->rank)return false;
    if(!p->element_type && p->index!=AIN_ARRAY) {
        switch(p->index){case AIN_ARRAY_TYPE:case AIN_REF_ARRAY_TYPE:break;default:return false;}
        if(array_type(p->index)!=t->array_type->data ||
           (t->array_type->data==AIN_STRUCT && p->array.struct_type!=t->array_type->struc))return false;
        page_set_element_type(p,t->array_type);p->index=AIN_ARRAY;*changed=true;
    }
    return p->element_type && r12_type_matches(p->element_type,t->array_type,0);
}
static bool g12_candidate_validate(struct g12_candidate *c,const struct g12_root *roots,size_t count,
                                   const struct ain_type *const *root_types)
{
    if(!c || !c->view || (count && (!roots || !root_types)))return false;
    c->validated=false;
    struct g12_graph graph;
    if(!g12_collect_view_external(&graph,c->view,c->capacity,roots,count,c->external)){g12_graph_free(&graph);return false;}
    bool ok=graph.count+1==c->capacity;g12_graph_free(&graph);if(!ok)return false;
    bool changed=true;
    for(int pass=0;changed;pass++) {
    if(pass>=R12_TYPE_MAX_DEPTH+4)return false;changed=false;
    for(size_t i=0;i<count;i++)if(!g12_candidate_type(c,root_types[i],roots[i].value,&changed))return false;
    for(size_t i=1;i<c->capacity;i++) {
        if(c->external && c->external[i])continue;
        struct vm_pointer *h=&c->view[i];if(h->type!=VM_PAGE || !h->page)continue;struct page *p=h->page;
        const struct ain_variable *vars=p->type==STRUCT_PAGE?ain->structures[p->index].members:
            p->type==LOCAL_PAGE?ain->functions[p->index].vars:NULL;
        for(int k=0;k<p->nr_vars;k++) {
            if(vars){if(!g12_candidate_type(c,&vars[k].type,p->values[k].i,&changed))return false;}
            else if(p->type==ARRAY_PAGE && p->element_type && p->array.rank>1) {
                struct ain_type child={.data=AIN_ARRAY,.rank=p->array.rank-1,.array_type=(struct ain_type*)p->element_type};
                if(!g12_candidate_type(c,&child,p->values[k].i,&changed))return false;
            } else if(p->type==ARRAY_PAGE && p->element_type && p->element_type->data!=AIN_IFACE &&
                      !g12_candidate_type(c,p->element_type,p->values[k].i,&changed))return false;
        }
    }
    }
    if(!g12_collect_view_external(&graph,c->view,c->capacity,roots,count,c->external)){g12_graph_free(&graph);return false;}
    if(graph.count+1!=c->capacity){g12_graph_free(&graph);return false;}
    for(size_t i=0;i<graph.count;i++)if(!c->external || !c->external[graph.slots[i]])c->view[graph.slots[i]].ref=graph.incoming[i];
    c->global_refs=graph.global_refs;c->validated=true;g12_graph_free(&graph);return true;
}
#endif
