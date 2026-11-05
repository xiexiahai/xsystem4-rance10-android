/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GLOBAL_GRAPH_FRAME_BINDING_H
#define GLOBAL_GRAPH_FRAME_BINDING_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "global_graph_binding.h"
#include "vm.h"

static bool g12_frame_local_valid(int slot,int *function)
{
    if(slot<=0 || !heap_index_valid(slot) || heap[slot].ref<=0 ||
       heap[slot].type!=VM_PAGE)
        return false;
    struct page *page=heap_get_page(slot);
    if(!page || page->type!=LOCAL_PAGE || page->index<0 ||
       page->index>=ain->nr_functions ||
       page->nr_vars!=ain->functions[page->index].nr_vars)
        return false;
    if(page->nr_vars>0 && !ain->functions[page->index].vars)
        return false;
    *function=page->index;
    return true;
}

static bool g12_find_frame_binding(int target,struct g12_binding *out)
{
    g12_binding_clear(out);
    if(!out || !ain || !heap || heap_size<1 ||
       heap_size>G12_BINDING_NODE_LIMIT || ain->nr_functions<0)
        return false;
    if(ain->nr_functions>0 && !ain->functions)
        return false;

    if(target<=0 || !heap_index_valid(target) || heap[target].ref<=0 ||
       heap[target].type!=VM_PAGE || !heap[target].page ||
       heap[target].page->type!=STRUCT_PAGE)
        return false;
    if(!g12_binding_shape(heap[target].page,-1))return false;

    unsigned char *flags=calloc(ain->nr_functions ? (size_t)ain->nr_functions : 1,1);
    struct g12_binding_visit *visited=calloc(heap_size,sizeof(*visited));
    int *queue=malloc(heap_size*sizeof(*queue));
    if(!flags || !visited || !queue) {
        free(flags);free(visited);free(queue);
        return false;
    }

    size_t frames=vm_active_frame_count();
    for(size_t i=0;i<frames;i++) {
        int fno;
        if(!g12_frame_local_valid(vm_active_frame_page(i),&fno))continue;
        if(flags[fno]&1)flags[fno]|=2;
        else flags[fno]=1;
    }

    size_t head=0,tail=0;
    for(size_t i=0;i<frames;i++) {
        int slot=vm_active_frame_page(i),fno;
        if(!g12_frame_local_valid(slot,&fno))continue;
        struct page *page=heap_get_page(slot);
        for(int k=0;k<page->nr_vars;k++) {
            const struct ain_type *type=&ain->functions[fno].vars[k].type;
            int value=page->values[k].i;
            if(!g12_binding_edge(type->data,type->struc,value))continue;
            // Generic controller functions can be nested with different
            // interface implementations. Require one matching runtime root
            // type, rather than rejecting every repeated function number.
            unsigned matches=0;int root_type=heap[value].page->index;
            for(size_t j=0;j<frames;j++) {
                int other_function,other=vm_active_frame_page(j);
                if(!g12_frame_local_valid(other,&other_function) || other_function!=fno)continue;
                struct page *local=heap[other].page;
                int object=local->values[k].i;
                if(g12_binding_edge(type->data,type->struc,object) && heap[object].page->index==root_type)matches++;
            }
            if(matches!=1)continue;
            if(visited[value].seen)continue;
            visited[value].seen=1;
            visited[value].parent=-1;
            visited[value].edge=k;
            visited[value].root=(int)i;
            visited[value].depth=0;
            queue[tail++]=value;
        }
    }

    while(head<tail) {
        int slot=queue[head++];
        if(slot==target) {
            unsigned depth=visited[slot].depth;
            if(depth>G12_BINDING_DEPTH) {
                free(flags);free(visited);free(queue);
                g12_binding_clear(out);
                return false;
            }
            int at=slot;
            for(unsigned i=0;i<depth;i++) {
                int edge=visited[at].edge;
                int parent=visited[at].parent;
                if(edge<0 || parent<0) {
                    free(flags);free(visited);free(queue);
                    g12_binding_clear(out);
                    return false;
                }
                out->words[depth-1-i]=edge;
                at=parent;
            }
            int root_frame=visited[at].root;
            int frame_slot=vm_active_frame_page((size_t)root_frame);
            int frame_function;
            if(!g12_frame_local_valid(frame_slot,&frame_function)) {
                free(flags);free(visited);free(queue);
                g12_binding_clear(out);
                return false;
            }
            out->global=-1;
            out->function=frame_function;
            out->local=visited[at].edge;
            out->root_structure=heap_get_page(at)->index;
            out->structure=heap_get_page(slot)->index;
            out->depth=depth;
            free(flags);free(visited);free(queue);
            return true;
        }
        if(visited[slot].depth>=G12_BINDING_DEPTH)continue;
        struct page *page=heap_get_page(slot);
        if(!page || page->type!=STRUCT_PAGE || page->index<0 ||
           page->index>=ain->nr_structures ||
           page->nr_vars!=ain->structures[page->index].nr_members)
            continue;
        const struct ain_variable *members=ain->structures[page->index].members;
        for(int k=0;k<page->nr_vars;k++) {
            const struct ain_type *type=&members[k].type;
            int value=page->values[k].i;
            if(!g12_binding_edge(type->data,type->struc,value))continue;
            if(visited[value].seen)continue;
            visited[value].seen=1;
            visited[value].parent=slot;
            visited[value].edge=k;
            visited[value].root=visited[slot].root;
            visited[value].depth=visited[slot].depth+1;
            queue[tail++]=value;
        }
    }

    free(flags);free(visited);free(queue);
    g12_binding_clear(out);
    return false;
}

static bool g12_resolve_frame_binding(const struct g12_binding *binding,int *out)
{
    if(out)*out=-1;
    if(!binding || !out || !ain || !heap || heap_size<1)
        return false;
    if(binding->global!=-1 || binding->function<0 ||
       binding->function>=ain->nr_functions || binding->local<0 ||
       binding->root_structure<0 || binding->root_structure>=ain->nr_structures ||
       binding->depth>G12_BINDING_DEPTH || binding->structure<0 ||
       binding->structure>=ain->nr_structures)
        return false;
    if(ain->nr_functions>0 && !ain->functions)
        return false;
    if(ain->functions[binding->function].nr_vars<=binding->local)
        return false;

    int frame=-1,count=0;
    size_t frames=vm_active_frame_count();
    for(size_t i=0;i<frames;i++) {
        int fno;
        int slot=vm_active_frame_page(i);
        if(!g12_frame_local_valid(slot,&fno))continue;
        if(fno!=binding->function)continue;
        const struct ain_type *type=&ain->functions[fno].vars[binding->local].type;
        int value=heap[slot].page->values[binding->local].i;
        if(g12_binding_edge(type->data,type->struc,value) && heap[value].page->index==binding->root_structure){frame=slot;count++;}
    }
    if(count!=1 || frame<0)
        return false;

    struct page *page=heap_get_page(frame);
    if(!page || page->type!=LOCAL_PAGE || page->index!=binding->function ||
       binding->local>=page->nr_vars)
        return false;
    const struct ain_type *root=&ain->functions[binding->function].vars[binding->local].type;
    int slot=page->values[binding->local].i;
    if(!g12_binding_edge(root->data,root->struc,slot))
        return false;

    for(unsigned i=0;i<binding->depth;i++) {
        page=heap_get_page(slot);
        if(!page || page->type!=STRUCT_PAGE || page->index<0 ||
           page->index>=ain->nr_structures ||
           page->nr_vars!=ain->structures[page->index].nr_members)
            return false;
        int word=binding->words[i];
        if(word<0 || word>=page->nr_vars)
            return false;
        const struct ain_type *type=&ain->structures[page->index].members[word].type;
        int value=page->values[word].i;
        if(!g12_binding_edge(type->data,type->struc,value))
            return false;
        slot=value;
    }

    page=heap_get_page(slot);
    if(!page || page->type!=STRUCT_PAGE || page->index!=binding->structure)
        return false;
    *out=slot;
    return true;
}

static bool g12_find_external_binding(int target,const bool *allowed,struct g12_binding *out)
{return g12_find_binding(target,allowed,out) || g12_find_frame_binding(target,out);}
static bool g12_resolve_external_binding(const struct g12_binding *binding,const bool *allowed,int *out)
{return binding && binding->global>=0?g12_resolve_binding(binding,allowed,out):g12_resolve_frame_binding(binding,out);}

// A returned frame, reset or cold start ends controller lifetimes. Only statically provable
// STRUCT/REF_STRUCT member paths may expire; absent metadata, dynamic member
// interfaces, ambiguous/live matching roots and global bindings remain strict.
static bool g12_expirable_frame_binding(const struct g12_binding *b)
{
    if(!b || !ain || !heap || b->global!=-1 || b->function<0 || b->function>=ain->nr_functions ||
       !ain->functions || !ain->structures || b->local<0 || b->depth>G12_BINDING_DEPTH ||
       b->root_structure<0 || b->root_structure>=ain->nr_structures || b->structure<0 || b->structure>=ain->nr_structures)
        return false;
    const struct ain_function *f=&ain->functions[b->function];
    if(b->local>=f->nr_vars || !f->vars)return false;
    const struct ain_type *root=&f->vars[b->local].type;
    if(root->data==AIN_STRUCT || root->data==AIN_REF_STRUCT) {
        if(root->struc!=b->root_structure)return false;
    } else if(root->data==AIN_IFACE) {
        const struct ain_struct *s=&ain->structures[b->root_structure];bool implements=false;
        if(root->struc<0 || root->struc>=ain->nr_structures || s->nr_interfaces<0 ||
           (s->nr_interfaces && !s->interfaces))return false;
        for(int i=0;i<s->nr_interfaces;i++)if(s->interfaces[i].struct_type==root->struc)implements=true;
        if(!implements)return false;
    } else return false;
    int structure=b->root_structure;
    for(unsigned i=0;i<b->depth;i++) {
        const struct ain_struct *s=&ain->structures[structure];int word=b->words[i];
        if(word<0 || word>=s->nr_members || !s->members)return false;
        const struct ain_type *t=&s->members[word].type;
        if((t->data!=AIN_STRUCT && t->data!=AIN_REF_STRUCT) || t->struc<0 || t->struc>=ain->nr_structures)return false;
        structure=t->struc;
    }
    if(structure!=b->structure)return false;
    for(size_t i=0;i<vm_active_frame_count();i++) {
        int function,local=vm_active_frame_page(i);
        if(!g12_frame_local_valid(local,&function) || function!=b->function)continue;
        int value=heap[local].page->values[b->local].i;
        if(value==-1)continue;
        if(!g12_binding_edge(root->data,root->struc,value))return false;
        if(heap[value].page->index==b->root_structure)return false;
    }
    return true;
}
#endif
