/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GLOBAL_GRAPH_BINDING_H
#define GLOBAL_GRAPH_BINDING_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "system4/ain.h"
#include "vm/heap.h"
#include "vm/page.h"

#define G12_BINDING_DEPTH 32
#define G12_BINDING_NODE_LIMIT (1024u * 1024u)

struct g12_binding {
    int global,structure;
    int function,local,root_structure;
    unsigned depth;
    int words[G12_BINDING_DEPTH];
};

struct g12_binding_visit {
    int parent,edge,root;
    unsigned depth;
    unsigned char seen;
};

static void g12_binding_clear(struct g12_binding *out)
{
    if(!out)return;
    out->global=-1;
    out->structure=-1;
    out->function=-1;out->local=-1;out->root_structure=-1;
    out->depth=0;
    for(unsigned i=0;i<G12_BINDING_DEPTH;i++)out->words[i]=0;
}

static bool g12_binding_shape(struct page *page,int expected_structure)
{
    if(!page || page->type!=STRUCT_PAGE)return false;
    if(page->index<0 || page->index>=ain->nr_structures)return false;
    if(page->nr_vars!=ain->structures[page->index].nr_members)return false;
    if(expected_structure>=0 && page->index!=expected_structure)return false;
    return true;
}

static bool g12_binding_edge(enum ain_data_type type,int struc,int slot)
{
    if(type!=AIN_STRUCT && type!=AIN_REF_STRUCT && type!=AIN_IFACE)
        return false;
    if(slot<=0 || !heap_index_valid(slot) || heap[slot].ref<=0 ||
       heap[slot].type!=VM_PAGE)
        return false;
    struct page *page=heap_get_page(slot);
    if(!page || page->type!=STRUCT_PAGE)return false;
    if(page->index<0 || page->index>=ain->nr_structures)return false;
    if(page->nr_vars!=ain->structures[page->index].nr_members)return false;
    if((type==AIN_STRUCT || type==AIN_REF_STRUCT) && struc>=0 &&
       page->index!=struc)
        return false;
    return true;
}

static bool g12_find_binding(int target,const bool *allowed,
        struct g12_binding *out)
{
    g12_binding_clear(out);
    if(!allowed || !out || !ain || !heap || heap_size<1 ||
       heap_size>G12_BINDING_NODE_LIMIT || ain->nr_globals<0 ||
       ain->nr_structures<0)
        return false;
    if(!heap_index_valid(0) || heap[0].ref<=0 || heap[0].type!=VM_PAGE ||
       !heap[0].page || heap[0].page->type!=GLOBAL_PAGE ||
       heap[0].page->nr_vars<ain->nr_globals)
        return false;

    if(target<=0 || !heap_index_valid(target) || heap[target].ref<=0 ||
       heap[target].type!=VM_PAGE || !heap[target].page ||
       heap[target].page->type!=STRUCT_PAGE)
        return false;
    if(!g12_binding_shape(heap[target].page,-1))return false;

    struct g12_binding_visit *visited=calloc(heap_size,sizeof(*visited));
    int *queue=malloc(heap_size*sizeof(*queue));
    if(!visited || !queue) {
        free(visited);free(queue);
        return false;
    }

    size_t head=0,tail=0;
    for(int g=0;g<ain->nr_globals;g++) {
        if(!allowed[g])continue;
        const struct ain_type *type=&ain->globals[g].type;
        int value=heap[0].page->values[g].i;
        if(!g12_binding_edge(type->data,type->struc,value))continue;
        if(visited[value].seen)continue;
        visited[value].seen=1;
        visited[value].parent=-1;
        visited[value].edge=-1;
        visited[value].root=g;
        visited[value].depth=0;
        queue[tail++]=value;
    }

    while(head<tail) {
        int slot=queue[head++];
        if(slot==target) {
            unsigned depth=visited[slot].depth;
            if(depth>G12_BINDING_DEPTH) {
                free(visited);free(queue);
                g12_binding_clear(out);
                return false;
            }
            int at=slot;
            for(unsigned i=0;i<depth;i++) {
                int edge=visited[at].edge;
                int parent=visited[at].parent;
                if(edge<0 || parent<0) {
                    free(visited);free(queue);
                    g12_binding_clear(out);
                    return false;
                }
                out->words[depth-1-i]=edge;
                at=parent;
            }
            out->global=visited[slot].root;
            out->structure=heap_get_page(slot)->index;
            out->depth=depth;
            free(visited);free(queue);
            return true;
        }
        if(visited[slot].depth>=G12_BINDING_DEPTH)
            continue;
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

    free(visited);free(queue);
    g12_binding_clear(out);
    return false;
}

static bool g12_resolve_binding(const struct g12_binding *binding,
        const bool *allowed,int *out)
{
    if(out)*out=-1;
    if(!binding || !allowed || !out || !ain || !heap || heap_size<1)
        return false;
    if(binding->global<0 || binding->global>=ain->nr_globals ||
       !allowed[binding->global] || binding->depth>G12_BINDING_DEPTH ||
       binding->structure<0 || binding->structure>=ain->nr_structures)
        return false;
    if(!heap_index_valid(0) || heap[0].ref<=0 || heap[0].type!=VM_PAGE ||
       !heap[0].page || heap[0].page->type!=GLOBAL_PAGE ||
       heap[0].page->nr_vars<ain->nr_globals)
        return false;

    const struct ain_type *root=&ain->globals[binding->global].type;
    int slot=heap[0].page->values[binding->global].i;
    if(!g12_binding_edge(root->data,root->struc,slot))
        return false;

    for(unsigned i=0;i<binding->depth;i++) {
        struct page *page=heap_get_page(slot);
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

    struct page *page=heap_get_page(slot);
    if(!page || page->type!=STRUCT_PAGE || page->index!=binding->structure)
        return false;
    *out=slot;
    return true;
}

#endif
