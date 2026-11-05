/* SPDX-License-Identifier: GPL-2.0-or-later
 * Read-only strong-reference collection for the future R10GS1 codec.
 * This is not a serializer or a complete type/bytecode validator. */
#ifndef R10_GLOBAL_GRAPH_WALK_H
#define R10_GLOBAL_GRAPH_WALK_H
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "system4/string.h"
#include "vm/heap.h"
#include "vm/page.h"

#define G12_NODE_LIMIT (1024u*1024u)
#define G12_WORD_LIMIT (16u*1024u*1024u)
#define G12_STRING_LIMIT (16u*1024u*1024u)
struct g12_root { enum ain_data_type type; int value; };
struct g12_graph {
    int *ids,*slots,*incoming;
    const struct vm_pointer *view;
    const int *external;
    size_t count,capacity,words,string_bytes;
    unsigned global_refs,external_weak,stale_weak;
    const char *stage;
    int problem_slot,problem_type,problem_value;
};
static void g12_graph_free(struct g12_graph *g)
{free(g->ids);free(g->slots);free(g->incoming);memset(g,0,sizeof(*g));}
static bool g12_bad(struct g12_graph *g,const char *stage,int slot,int type,int value)
{g->stage=stage;g->problem_slot=slot;g->problem_type=type;g->problem_value=value;return false;}
static bool g12_alive(struct g12_graph *g,int slot)
{return slot>=0 && (size_t)slot<g->capacity && g->view[slot].ref>0;}
static bool g12_page_is(struct g12_graph *g,int slot,int type,bool nullable)
{
    if(slot<0 || (size_t)slot>=g->capacity || g->view[slot].ref<=0 || g->view[slot].type!=VM_PAGE)return false;
    struct page *p=g->view[slot].page;
    return p ? (type<0 || (int)p->type==type) : nullable;
}
static bool g12_strong(struct g12_graph *g,int value,int type,bool nullable,bool global)
{
    if(value==-1)return true;
    if(value==0 && global) {
        if(!g12_page_is(g,0,GLOBAL_PAGE,false) || g->global_refs==UINT_MAX)
            return g12_bad(g,"global-reference",0,type,value);
        g->global_refs++;return true;
    }
    if(value<=0 || !g12_alive(g,value) ||
       (type==-2 ? g->view[value].type!=VM_STRING : !g12_page_is(g,value,type,nullable)))
        return g12_bad(g,"strong-reference",value,type,value);
    if(g->external && g->external[value])return g12_bad(g,"external-strong",value,type,value);
    int id=g->ids[value];
    if(id<0) {
        if(g->count>=g->capacity)return g12_bad(g,"node-limit",value,type,value);
        id=g->count++;g->ids[value]=id;g->slots[id]=value;
    }
    if(g->incoming[id]==INT_MAX)return g12_bad(g,"reference-count",value,type,value);
    g->incoming[id]++;return true;
}
static bool g12_weak(struct g12_graph *g,int value,int type)
{
    if(value==-1)return true;
    if(value<0 || (size_t)value>=g->capacity)return g12_bad(g,"weak-range",value,type,value);
    if(!g12_alive(g,value)) {
        g->stale_weak++;
        // Expired external IDs are counted only if an actual weak edge reaches
        // them. This retains the all-nodes-reachable validation requirement.
        if(g->external && g->external[value]<0 && g->ids[value]<0) {
            if(g->count>=g->capacity)return g12_bad(g,"node-limit",value,type,value);
            g->ids[value]=g->count;g->slots[g->count++]=value;
        }
        return true;
    }
    if(type>=0 && !g12_page_is(g,value,type,false))return g12_bad(g,"weak-type",value,type,value);
    if(g->ids[value]<0) {
        g->external_weak++;
        if(!g->external || !g->external[value])return g12_bad(g,"external-weak",value,type,value);
        if(g->count>=g->capacity)return g12_bad(g,"node-limit",value,type,value);
        g->ids[value]=g->count;g->slots[g->count++]=value;
    }
    return true;
}
static bool g12_word(struct g12_graph *g,enum ain_data_type type,int value,bool weak_pass)
{
    switch(type) {
    case AIN_STRING:return weak_pass || g12_strong(g,value,-2,false,false);
    case AIN_STRUCT:case AIN_IFACE:return weak_pass || g12_strong(g,value,STRUCT_PAGE,false,false);
    case AIN_DELEGATE:return weak_pass || g12_strong(g,value,DELEGATE_PAGE,true,false);
    case AIN_ARRAY:case AIN_ARRAY_TYPE:return weak_pass || g12_strong(g,value,ARRAY_PAGE,true,false);
    case AIN_REF_TYPE:return weak_pass || g12_strong(g,value,-1,true,true);
    case AIN_WRAP:case AIN_IFACE_WRAP:return !weak_pass || g12_weak(g,value,-1);
    case AIN_VOID:case AIN_INT:case AIN_FLOAT:case AIN_FUNC_TYPE:
    case AIN_BOOL:case AIN_LONG_INT:case AIN_ENUM:case AIN_ENUM2:return true;
    default:return g12_bad(g,"unsupported-word",-1,type,value);
    }
}
static bool g12_page_shape(struct g12_graph *g,int slot,struct page *p)
{
    if(p->nr_vars<0 || (size_t)p->nr_vars>G12_WORD_LIMIT-g->words)
        return g12_bad(g,"word-limit",slot,p->type,p->nr_vars);
    g->words+=p->nr_vars;
    switch(p->type) {
    case STRUCT_PAGE:
        if(p->index<0 || p->index>=ain->nr_structures || p->nr_vars!=ain->structures[p->index].nr_members)
            return g12_bad(g,"struct-shape",slot,p->type,p->index);
        break;
    case LOCAL_PAGE:
        if(p->index<0 || p->index>=ain->nr_functions || p->nr_vars!=ain->functions[p->index].nr_vars)
            return g12_bad(g,"local-shape",slot,p->type,p->index);
        if(p->local.parent<-1 || p->local_parent_owned!=(p->local.parent>=0))
            return g12_bad(g,"local-parent",slot,p->type,p->local.parent);
        if(!g12_strong(g,p->local.parent,LOCAL_PAGE,false,false))return false;
        // Lexical ancestry cannot cycle; ordinary object cycles may.
        for(int at=slot,depth=0;at>=0;depth++) {
            if(depth>=1024 || !g12_page_is(g,at,LOCAL_PAGE,false))
                return g12_bad(g,"parent-cycle-or-depth",slot,p->type,at);
            at=g->view[at].page->local.parent;
        }
        break;
    case ARRAY_PAGE:
        if(p->array.rank<1 || p->array.rank>4)
            return g12_bad(g,"array-rank",slot,p->type,p->array.rank);
        if(p->element_type) {
            if(p->index!=AIN_ARRAY || (p->array.rank==1 && p->element_type->data==AIN_IFACE && p->nr_vars%2))
                return g12_bad(g,"array-descriptor",slot,p->type,p->index);
        } else {
            switch(p->index){case AIN_ARRAY_TYPE:case AIN_REF_ARRAY_TYPE:break;
            default:return g12_bad(g,"array-kind",slot,p->type,p->index);}
        }
        break;
    case DELEGATE_PAGE:
        if(p->nr_vars%3)return g12_bad(g,"delegate-shape",slot,p->type,p->nr_vars);
        for(int i=0;i<p->nr_vars;i+=3) {
            int fn=p->values[i+1].i,env=delegate_get_environment(p,i/3);
            if(fn<1 || fn>=ain->nr_functions || p->values[i].i<-1)
                return g12_bad(g,"delegate-function",slot,p->type,fn);
            if(!g12_strong(g,env,LOCAL_PAGE,false,false))return false;
        }
        break;
    default:return g12_bad(g,"page-kind",slot,p->type,p->index);
    }
    return true;
}
static bool g12_collect_view_external(struct g12_graph *g,const struct vm_pointer *view,size_t capacity,const struct g12_root *roots,size_t count,const int *external)
{
    memset(g,0,sizeof(*g));g->problem_slot=-1;
    g->view=view;g->capacity=capacity;g->external=external;
    if(!ain || !view || capacity<1 || capacity>G12_NODE_LIMIT || (count && !roots))
        return g12_bad(g,"arguments",-1,0,0);
    
    g->ids=malloc(capacity*sizeof(int));g->slots=malloc(capacity*sizeof(int));g->incoming=calloc(capacity,sizeof(int));
    if(!g->ids || !g->slots || !g->incoming)return g12_bad(g,"allocation",-1,0,0);
    for(size_t i=0;i<capacity;i++)g->ids[i]=-1;
    for(size_t i=0;i<count;i++)if(!g12_word(g,roots[i].type,roots[i].value,false))return false;
    for(size_t i=0;i<g->count;i++) {
        int slot=g->slots[i];
        if(g->view[slot].type==VM_STRING) {
            struct string *s=g->view[slot].s;
            if(!s || s->size<0 || (size_t)s->size>G12_STRING_LIMIT ||
               (size_t)s->size>G12_WORD_LIMIT*4u-g->string_bytes)
                return g12_bad(g,"string-limit",slot,0,0);
            g->string_bytes+=s->size;continue;
        }
        struct page *p=g->view[slot].page;if(!p)continue;
        if(!g12_page_shape(g,slot,p))return false;
        if(p->type!=DELEGATE_PAGE)for(int k=0;k<p->nr_vars;k++)
            if(!g12_word(g,variable_type(p,k,NULL,NULL),p->values[k].i,false))return false;
    }
    // Check weak targets only after every strong path has been discovered.
    for(size_t i=0;i<count;i++)if(!g12_word(g,roots[i].type,roots[i].value,true))return false;
    for(size_t i=0;i<g->count;i++) {
        int slot=g->slots[i];if(g->view[slot].type!=VM_PAGE || !g->view[slot].page)continue;
        if(external && external[slot])continue;
        struct page *p=g->view[slot].page;
        if(p->type==LOCAL_PAGE && !g12_weak(g,p->local.struct_ptr,STRUCT_PAGE))return false;
        if(p->type==DELEGATE_PAGE) {
            for(int k=0;k<p->nr_vars;k+=3) {
                int obj=p->values[k].i;
                if((g12_alive(g,obj)?g->view[obj].seq:0)!=(uint32_t)p->values[k+2].i){g->stale_weak++;continue;}
                if(!g12_weak(g,obj,STRUCT_PAGE))return false;
            }
        } else for(int k=0;k<p->nr_vars;k++)
            if(!g12_word(g,variable_type(p,k,NULL,NULL),p->values[k].i,true))return false;
    }
    for(size_t i=0;i<g->count;i++)if(g->incoming[i]>g->view[g->slots[i]].ref)
        return g12_bad(g,"live-reference-count",g->slots[i],0,g->incoming[i]);
    g->stage="collected";return true;
}
static bool g12_collect_view(struct g12_graph *g,const struct vm_pointer *view,size_t capacity,const struct g12_root *roots,size_t count)
{return g12_collect_view_external(g,view,capacity,roots,count,NULL);}
static bool g12_collect(struct g12_graph *g,const struct g12_root *roots,size_t count)
{return g12_collect_view(g,heap,heap_size,roots,count);}
#endif
