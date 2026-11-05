/* SPDX-License-Identifier: GPL-2.0-or-later
 * Prepare a replacement heap table before publishing decoded global roots.
 * Existing pages, unselected globals, stack and instruction pointer stay put. */
#ifndef R10_GLOBAL_GRAPH_INSTALL_H
#define R10_GLOBAL_GRAPH_INSTALL_H
#include "global_graph_decode.h"
// Core heap bookkeeping, also declared by vm/heap.h under VM_PRIVATE.
extern int32_t *heap_free_stack;
extern size_t heap_free_ptr;
extern uint32_t heap_next_seq;
static bool g12_remap_word(enum ain_data_type type,int raw,const int *map,size_t capacity,int *out)
{
    switch(type) {
    case AIN_STRING:case AIN_STRUCT:case AIN_IFACE:case AIN_DELEGATE:
    case AIN_ARRAY:case AIN_ARRAY_TYPE:case AIN_WRAP:case AIN_IFACE_WRAP:
        if(raw==-1){*out=-1;return true;}
        if(raw<1 || (size_t)raw>=capacity)return false;*out=map[raw];return true;
    case AIN_REF_TYPE:
        if(raw==0){*out=0;return true;}
        if(raw==-1){*out=-1;return true;}
        if(raw<1 || (size_t)raw>=capacity)return false;*out=map[raw];return true;
    case AIN_VOID:case AIN_INT:case AIN_FLOAT:case AIN_FUNC_TYPE:
    case AIN_BOOL:case AIN_LONG_INT:case AIN_ENUM:case AIN_ENUM2:*out=raw;return true;
    default:return false;
    }
}
static bool g12_remap_page(struct page *p,const int *map,size_t capacity,
                           const struct vm_pointer *next,bool write)
{
    if(!p)return true;
    if(p->type==DELEGATE_PAGE) {
        for(int k=0;k<p->nr_vars;k+=3) {
            int obj,env,original_obj=p->values[k].i;
            if(!g12_remap_word(AIN_WRAP,original_obj,map,capacity,&obj) ||
               !g12_remap_word(AIN_REF_INT,delegate_get_environment(p,k/3),map,capacity,&env))return false;
            if(write) {
                p->values[k].i=obj;
                if(obj>0)p->values[k+2].i=next[obj].seq;
                else if(original_obj>0 && obj==-1) {
                    // A mapped-away receiver must be stale (-1,seq=1), never
                    // a live static function (-1,seq=0).
                    p->values[k+2].i=1;
                }
                if(p->delegate_environments)p->delegate_environments[k/3]=env;
            }
        }
        return true;
    }
    for(int k=0;k<p->nr_vars;k++) {
        int value;if(!g12_remap_word(variable_type(p,k,NULL,NULL),p->values[k].i,map,capacity,&value))return false;
        if(write)p->values[k].i=value;
    }
    if(p->type==LOCAL_PAGE) {
        int parent,object;
        if(!g12_remap_word(AIN_REF_INT,p->local.parent,map,capacity,&parent) ||
           !g12_remap_word(AIN_WRAP,p->local.struct_ptr,map,capacity,&object))return false;
        if(write){p->local.parent=parent;p->local.struct_ptr=object;}
    }
    return true;
}
static bool g12_owns_word(enum ain_data_type type)
{
    switch(type){case AIN_STRING:case AIN_STRUCT:case AIN_IFACE:case AIN_DELEGATE:
    case AIN_ARRAY:case AIN_ARRAY_TYPE:case AIN_REF_TYPE:return true;default:return false;}
}
// Consumes the candidate on success. Caller always frees it after this call.
static bool g12_install(struct g12_candidate *c,const int *global_ids,
                        const struct g12_root *roots,size_t count)
{
    if(!c || !c->validated || !c->view || c->capacity<1 ||
       (count && (!global_ids || !roots)) || !heap || !heap_free_stack ||
       heap_size<1 || heap_size>G12_NODE_LIMIT || heap_free_ptr>heap_size ||
       !page_index_valid(0) || !heap[0].page || heap[0].page->type!=GLOBAL_PAGE ||
       heap[0].page->nr_vars!=ain->nr_globals || count>(size_t)ain->nr_globals)return false;
    if(c->nr_bindings>=c->capacity || c->nr_bindings>G12_EXTERNAL_LIMIT)return false;
    size_t nodes=c->capacity-1-c->nr_bindings,available=heap_size-heap_free_ptr;
    if(nodes>G12_NODE_LIMIT || !heap_next_seq || nodes>UINT32_MAX-heap_next_seq ||
       c->global_refs>(unsigned)(INT_MAX-heap[0].ref))return false;
    size_t capacity=heap_size+(nodes>available?nodes-available:0);
    if(capacity>G12_NODE_LIMIT)return false;
    struct vm_pointer *next=calloc(capacity,sizeof(*next));
    int32_t *free_slots=malloc(capacity*sizeof(*free_slots));
    int *map=malloc(c->capacity*sizeof(*map));
    union vm_value *old=malloc((count?count:1)*sizeof(*old)),*values=malloc((count?count:1)*sizeof(*values));
    bool *selected=calloc(ain->nr_globals?ain->nr_globals:1,sizeof(bool));
    const struct ain_type **types=malloc((count?count:1)*sizeof(*types));
    if(!next || !free_slots || !map || !old || !values || !selected || !types)goto fail;
    for(size_t i=0;i<count;i++) {
        int global=global_ids[i];
        if(global<0 || global>=ain->nr_globals || selected[global] || roots[i].type!=ain->globals[global].type.data)goto fail;
        selected[global]=true;types[i]=&ain->globals[global].type;old[i]=heap[0].page->values[global];
        if(g12_owns_word(roots[i].type) && old[i].i!=-1 && !heap_index_valid(old[i].i))goto fail;
    }
    // A bound receiver belongs to an unselected global structure path.
    // Re-resolve it immediately before publishing; it remains weak and keeps
    // its existing slot, sequence guard, and reference count.
    for(int i=0;i<ain->nr_globals;i++)selected[i]=!selected[i];
    for(size_t i=0;i<c->nr_bindings;i++) {
        int id=c->bindings[i].id,slot;
        if(id<1 || (size_t)id>=c->capacity || !c->external)goto fail;
        if(c->external[id]<0) {
            if(!c->expire_frames || !g12_expirable_frame_binding(&c->bindings[i].path))goto fail;
            continue;
        }
        if(!g12_resolve_external_binding(&c->bindings[i].path,selected,&slot) || c->external[id]!=slot ||
           heap[slot].seq!=c->view[id].seq || heap[slot].page!=c->view[id].page)goto fail;
    }
    if(!g12_candidate_validate(c,roots,count,types) || c->global_refs>(unsigned)(INT_MAX-heap[0].ref))goto fail;
    memcpy(next,heap,heap_size*sizeof(*next));memcpy(free_slots,heap_free_stack,heap_size*sizeof(*free_slots));
    for(size_t i=heap_size;i<capacity;i++)free_slots[i]=i;
    size_t used=heap_free_ptr;uint32_t seq=heap_next_seq;map[0]=0;
    for(size_t i=1;i<c->capacity;i++) {
        if(c->external && c->external[i]){map[i]=c->external[i];continue;}
        int slot=free_slots[used++];
        if(slot<=0 || (size_t)slot>=capacity || next[slot].ref || c->view[i].ref<1)goto fail;
        map[i]=slot;next[slot]=c->view[i];next[slot].seq=seq++;
    }
    for(size_t i=0;i<count;i++) {
        if(!g12_remap_word(roots[i].type,roots[i].value,map,c->capacity,&values[i].i))goto fail;
    }
    for(size_t i=1;i<c->capacity;i++)if((!c->external || !c->external[i]) && c->view[i].type==VM_PAGE &&
       !g12_remap_page(c->view[i].page,map,c->capacity,next,false))goto fail;
    // No allocation or input validation remains after this point. Only the
    // candidate's pages are rewritten before the table is published.
    c->validated=false;
    for(size_t i=1;i<c->capacity;i++)if((!c->external || !c->external[i]) && c->view[i].type==VM_PAGE &&
       !g12_remap_page(c->view[i].page,map,c->capacity,next,true))goto fail;
    next[0].ref+=c->global_refs;
    struct vm_pointer *previous=heap;int32_t *previous_free=heap_free_stack;
    heap=next;heap_size=capacity;heap_free_stack=free_slots;heap_free_ptr=used;heap_next_seq=seq;
    for(size_t i=0;i<count;i++)heap[0].page->values[global_ids[i]]=values[i];
    for(size_t i=1;i<c->capacity;i++) {
        if(c->external && c->external[i])continue;
        // Ownership of these strings/pages now belongs to the live heap.
        if(c->view[i].type==VM_STRING)c->view[i].s=NULL;else c->view[i].page=NULL;
    }
    free(previous);free(previous_free);
    for(size_t i=0;i<count;i++)variable_fini(old[i],roots[i].type,false);
    free(map);free(old);free(values);free(selected);free(types);return true;
fail:free(next);free(free_slots);free(map);free(old);free(values);free(selected);free(types);return false;
}
#endif
