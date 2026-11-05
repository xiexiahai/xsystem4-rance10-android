/* SPDX-License-Identifier: GPL-2.0-or-later */
/* R10GS1 node JSON encoding. Read-only graph view; no files or VM mutation. */
#ifndef R10_GLOBAL_GRAPH_JSON_H
#define R10_GLOBAL_GRAPH_JSON_H
#include "global_graph_walk.h"
#include "global_graph_frame_binding.h"
#include "resume12_types.inc"
#include "cJSON.h"

static bool g12_json_basic(const struct g12_graph *g)
{
    return g && g->view && g->ids && g->slots && g->stage &&
        !strcmp(g->stage,"collected") && g->capacity>=1 &&
        g->capacity<=G12_NODE_LIMIT && g->count<=g->capacity;
}
static bool g12_json_valid(const struct g12_graph *g)
{
    if(!g12_json_basic(g))return false;
    for(size_t i=0;i<g->count;i++) {
        int slot=g->slots[i];
        if(slot<0 || (size_t)slot>=g->capacity ||
           g->view[slot].ref<=0 || g->ids[slot]!=(int)i)return false;
    }
    return true;
}
static bool g12_json_put(cJSON *object,const char *key,cJSON *item)
{
    if(!object || !key || !item)return false;
    cJSON_AddItemToObject(object,key,item);
    if(cJSON_GetObjectItemCaseSensitive(object,key)==item)return true;
    cJSON_Delete(item);return false;
}
static bool g12_json_add(cJSON *array,cJSON **tail,cJSON *item)
{
    if(!array || !tail || !item)return false;
    if(*tail) {
        (*tail)->next=item;
        item->prev=*tail;
    } else {
        array->child=item;
    }
    *tail=item;
    return true;
}
static bool g12_json_add_num(cJSON *array,cJSON **tail,int value)
{
    cJSON *item=cJSON_CreateNumber(value);
    if(!item)return false;
    if(!g12_json_add(array,tail,item)){cJSON_Delete(item);return false;}
    return true;
}
static bool g12_json_put_num(cJSON *object,const char *key,int value)
{
    cJSON *item=cJSON_CreateNumber(value);
    if(!item)return false;
    return g12_json_put(object,key,item);
}
static bool g12_json_put_bool(cJSON *object,const char *key,bool value)
{
    cJSON *item=cJSON_CreateBool(value);
    if(!item)return false;
    return g12_json_put(object,key,item);
}
static bool g12_json_strong(const struct g12_graph *g,int raw,int *out)
{
    if(!g || !out)return false;
    if(raw==-1){*out=-1;return true;}
    if(raw<=0 || (size_t)raw>=g->capacity)return false;
    int id=g->ids[raw];
    if(id<0)return false;
    *out=id+1;return true;
}
static bool g12_json_weak(const struct g12_graph *g,int raw,int *out)
{
    if(!g || !out)return false;
    if(raw==-1){*out=-1;return true;}
    if(raw<0 || (size_t)raw>=g->capacity)return false;
    if(g->view[raw].ref<=0){*out=-1;return true;}
    int id=g->ids[raw];
    if(id<0)return false;
    *out=id+1;return true;
}
static bool g12_word_encode(const struct g12_graph *g,enum ain_data_type type,int raw,int *out)
{
    if(!g12_json_basic(g) || !out)return false;
    *out=-1;
    switch(type) {
    case AIN_VOID:
    case AIN_INT:
    case AIN_FLOAT:
    case AIN_FUNC_TYPE:
    case AIN_BOOL:
    case AIN_LONG_INT:
    case AIN_ENUM:
    case AIN_ENUM2:
        *out=raw;return true;
    case AIN_STRING:
    case AIN_STRUCT:
    case AIN_IFACE:
    case AIN_DELEGATE:
    case AIN_ARRAY:
    case AIN_ARRAY_TYPE:
        return g12_json_strong(g,raw,out);
    case AIN_REF_TYPE:
        if(raw==0){*out=0;return true;}
        return g12_json_strong(g,raw,out);
    case AIN_WRAP:
    case AIN_IFACE_WRAP:
        return g12_json_weak(g,raw,out);
    default:
        return false;
    }
}
static cJSON *g12_json_string(const struct string *s)
{
    if(!s || s->size<0 || (size_t)s->size>G12_STRING_LIMIT)return NULL;
    size_t n=(size_t)s->size;
    char *hex=malloc(n*2+1);
    if(!hex)return NULL;
    static const char digits[]="0123456789abcdef";
    for(size_t i=0;i<n;i++) {
        unsigned c=(unsigned char)s->text[i];
        hex[i*2]=digits[c>>4];hex[i*2+1]=digits[c&15];
    }
    hex[n*2]=0;
    cJSON *node=cJSON_CreateObject();
    if(!node){free(hex);return NULL;}
    if(!g12_json_put_num(node,"kind",1)) {
        free(hex);cJSON_Delete(node);return NULL;
    }
    cJSON *bytes=cJSON_CreateString(hex);
    free(hex);
    if(!bytes){cJSON_Delete(node);return NULL;}
    if(!g12_json_put(node,"bytes",bytes)) {
        cJSON_Delete(node);return NULL;
    }
    return node;
}
static cJSON *g12_json_flat_values(const struct g12_graph *g,const struct page *p)
{
    cJSON *values=cJSON_CreateArray();
    if(!values)return NULL;
    cJSON *tail=NULL;
    for(int k=0;k<p->nr_vars;k++) {
        enum ain_data_type type=variable_type((struct page*)p,k,NULL,NULL);
        int out;
        if(!g12_word_encode(g,type,p->values[k].i,&out) ||
           !g12_json_add_num(values,&tail,out)) {
            cJSON_Delete(values);return NULL;
        }
    }
    return values;
}
static cJSON *g12_json_delegate_values(const struct g12_graph *g,const struct page *p)
{
    cJSON *values=cJSON_CreateArray();
    if(!values)return NULL;
    cJSON *tail=NULL;
    for(int k=0;k<p->nr_vars;k+=3) {
        int obj=p->values[k].i,fun=p->values[k+1].i,seq=p->values[k+2].i;
        int out_obj=-1,out_seq=1;
        if(obj==-1 && seq==0) {
            out_obj=-1;out_seq=0;
        } else if(obj>=0 && (size_t)obj<g->capacity &&
                  g->view[obj].ref>0 &&
                  g->view[obj].seq==(uint32_t)seq) {
            if(g->ids[obj]<0){cJSON_Delete(values);return NULL;}
            out_obj=g->ids[obj]+1;out_seq=0;
        }
        if(!g12_json_add_num(values,&tail,out_obj) ||
           !g12_json_add_num(values,&tail,fun) ||
           !g12_json_add_num(values,&tail,out_seq)) {
            cJSON_Delete(values);return NULL;
        }
    }
    return values;
}
static cJSON *g12_json_environments(const struct g12_graph *g,const struct page *p)
{
    cJSON *envs=cJSON_CreateArray();
    if(!envs)return NULL;
    cJSON *tail=NULL;
    for(int k=0;k<p->nr_vars/3;k++) {
        int out;
        if(!g12_json_strong(g,delegate_get_environment((struct page*)p,k),&out) ||
           !g12_json_add_num(envs,&tail,out)) {
            cJSON_Delete(envs);return NULL;
        }
    }
    return envs;
}
static cJSON *g12_binding_encode(const struct g12_binding *b)
{
    if(!b || b->global<-1 || b->depth>G12_BINDING_DEPTH)return NULL;
    cJSON *node=cJSON_CreateObject(),*path=cJSON_CreateArray(),*tail=NULL;
    if(!node || !path){cJSON_Delete(node);cJSON_Delete(path);return NULL;}
    for(unsigned i=0;i<b->depth;i++)if(!g12_json_add_num(path,&tail,b->words[i])){cJSON_Delete(node);cJSON_Delete(path);return NULL;}
    if(!g12_json_put_num(node,"kind",b->global>=0?2:3) ||
       !g12_json_put_num(node,"structure",b->structure)){cJSON_Delete(node);cJSON_Delete(path);return NULL;}
    if(b->global>=0 ? !g12_json_put_num(node,"global",b->global) :
       (!g12_json_put_num(node,"function",b->function) || !g12_json_put_num(node,"local",b->local) ||
        !g12_json_put_num(node,"root-structure",b->root_structure))) {
        cJSON_Delete(node);cJSON_Delete(path);return NULL;
    }
    if(!g12_json_put(node,"path",path)){cJSON_Delete(node);return NULL;}return node;
}
static cJSON *g12_nodes_encode_bound(const struct g12_graph *g,const struct g12_binding *bindings,const int *slots,size_t count)
{
    if(!g12_json_valid(g))return NULL;
    cJSON *nodes=cJSON_CreateArray();
    if(!nodes)return NULL;
    cJSON *tail=NULL;
    for(size_t i=0;i<g->count;i++) {
        int slot=g->slots[i];
        const struct vm_pointer *v=&g->view[slot];
        cJSON *node=NULL;
        if(g->external && g->external[slot]) {
            size_t j;for(j=0;j<count;j++)if(slots[j]==slot)break;
            if(j==count){cJSON_Delete(nodes);return NULL;}node=g12_binding_encode(&bindings[j]);
        } else if(v->type==VM_STRING) {
            node=g12_json_string(v->s);
        } else if(v->type==VM_PAGE) {
            struct page *p=v->page;
            node=cJSON_CreateObject();
            if(node && !g12_json_put_num(node,"kind",0)) {
                cJSON_Delete(node);node=NULL;
            }
            if(node && !p) {
                cJSON *null=cJSON_CreateNull();
                if(!null || !g12_json_put(node,"payload",null)) {
                    cJSON_Delete(node);node=NULL;
                }
            } else if(node) {
                if(!g12_json_put_num(node,"type",p->type) ||
                   !g12_json_put_num(node,"index",p->index)) {
                    cJSON_Delete(node);node=NULL;
                }
            }
            if(node && p) {
                cJSON *values=p->type==DELEGATE_PAGE ?
                    g12_json_delegate_values(g,p) :
                    g12_json_flat_values(g,p);
                if(!values || !g12_json_put(node,"values",values)) {
                    if(!values)cJSON_Delete(node);
                    else cJSON_Delete(node); /* put() owns values on failure */
                    node=NULL;
                }
            }
            if(node && p && p->type==ARRAY_PAGE) {
                cJSON *element=p->element_type?r12_type_encode(p->element_type):
                    cJSON_CreateNull();
                if(!element) {
                    cJSON_Delete(node);node=NULL;
                } else if(!g12_json_put_num(node,"rank",p->array.rank) ||
                          !g12_json_put_num(node,"struct-type",p->array.struct_type)) {
                    cJSON_Delete(element);cJSON_Delete(node);node=NULL;
                } else if(!g12_json_put(node,"element",element)) {
                    cJSON_Delete(node);node=NULL; /* put() owns element on failure */
                }
            }
            if(node && p && p->type==LOCAL_PAGE) {
                int parent,weak;
                if(!g12_json_strong(g,p->local.parent,&parent) ||
                   !g12_word_encode(g,AIN_WRAP,p->local.struct_ptr,&weak) ||
                   !g12_json_put_num(node,"parent",parent) ||
                   !g12_json_put_bool(node,"parent-owned",p->local_parent_owned) ||
                   !g12_json_put_num(node,"struct",weak)) {
                    cJSON_Delete(node);node=NULL;
                }
            }
            if(node && p && p->type==DELEGATE_PAGE) {
                cJSON *envs=g12_json_environments(g,p);
                if(!envs || !g12_json_put(node,"environments",envs)) {
                    cJSON_Delete(node);node=NULL; /* put() owns envs on failure */
                }
            }
        }
        if(!node || !g12_json_add(nodes,&tail,node)) {
            cJSON_Delete(node);cJSON_Delete(nodes);return NULL;
        }
    }
    return nodes;
}
static cJSON *g12_nodes_encode(const struct g12_graph *g)
{return g12_nodes_encode_bound(g,NULL,NULL,0);}
#endif /* R10_GLOBAL_GRAPH_JSON_H */
