/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GLOBAL_GRAPH_PROGRAM_H
#define GLOBAL_GRAPH_PROGRAM_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <zlib.h>
#include "system4/ain.h"

#define G12_PROGRAM_TYPE_DEPTH 32

static void g12_program_le32(uint32_t *crc,uint32_t value)
{
    uint8_t bytes[4]={(uint8_t)value,(uint8_t)(value>>8),
        (uint8_t)(value>>16),(uint8_t)(value>>24)};
    *crc=(uint32_t)crc32(*crc,bytes,sizeof(bytes));
}

static bool g12_program_type(uint32_t *crc,const struct ain_type *type,
        int depth,const struct ain_type *seen[G12_PROGRAM_TYPE_DEPTH+1])
{
    if(!type || depth>=G12_PROGRAM_TYPE_DEPTH)
        return false;
    for(int i=0;i<depth;i++)
        if(seen[i]==type)
            return false;
    seen[depth]=type;
    g12_program_le32(crc,(uint32_t)type->data);
    g12_program_le32(crc,(uint32_t)type->struc);
    g12_program_le32(crc,(uint32_t)type->rank);
    g12_program_le32(crc,type->array_type ? 1u : 0u);
    if(type->array_type)
        return g12_program_type(crc,type->array_type,depth+1,seen);
    return true;
}

static bool g12_program_id(const struct ain *program,char out[64])
{
    if(out)
        out[0]=0;
    if(!program || !out)
        return false;
    if(program->nr_globals<0 || program->nr_functions<0 ||
            program->nr_structures<0 || program->nr_global_groups<0)
        return false;
    if(program->nr_globals>0 && !program->globals)
        return false;
    if(program->nr_functions>0 && !program->functions)
        return false;
    if(program->nr_structures>0 && !program->structures)
        return false;
    if(program->nr_global_groups>0 && !program->global_group_names)
        return false;
    if(program->code_size>(size_t)UINT_MAX)
        return false;
    if(program->code_size>0 && !program->code)
        return false;

    uint32_t crc=crc32(0L,Z_NULL,0);
    if(program->code_size)
        crc=(uint32_t)crc32(crc,(const Bytef *)program->code,
            (uInt)program->code_size);

    g12_program_le32(&crc,(uint32_t)program->version);
    g12_program_le32(&crc,(uint32_t)program->minor_version);
    g12_program_le32(&crc,(uint32_t)program->nr_globals);
    g12_program_le32(&crc,(uint32_t)program->nr_functions);
    g12_program_le32(&crc,(uint32_t)program->nr_structures);
    g12_program_le32(&crc,(uint32_t)program->nr_global_groups);

    const struct ain_type *seen[G12_PROGRAM_TYPE_DEPTH+1];
    for(int i=0;i<program->nr_globals;i++) {
        const char *name=program->globals[i].name ? program->globals[i].name : "";
        crc=(uint32_t)crc32(crc,(const Bytef *)name,(uInt)strlen(name)+1);
        memset(seen,0,sizeof(seen));
        if(!g12_program_type(&crc,&program->globals[i].type,0,seen))
            return false;
        g12_program_le32(&crc,(uint32_t)program->globals[i].group_index);
    }

    for(int i=0;i<program->nr_functions;i++) {
        const struct ain_function *fn=&program->functions[i];
        if(fn->nr_vars<0 || fn->nr_args<0)
            return false;
        if(fn->nr_vars>0 && !fn->vars)
            return false;
        const char *name=fn->name ? fn->name : "";
        crc=(uint32_t)crc32(crc,(const Bytef *)name,(uInt)strlen(name)+1);
        g12_program_le32(&crc,(uint32_t)fn->address);
        memset(seen,0,sizeof(seen));
        if(!g12_program_type(&crc,&fn->return_type,0,seen))
            return false;
        g12_program_le32(&crc,(uint32_t)fn->nr_vars);
        g12_program_le32(&crc,(uint32_t)fn->nr_args); /* ain.h's nr_args */
        for(int k=0;k<fn->nr_vars;k++) {
            memset(seen,0,sizeof(seen));
            if(!g12_program_type(&crc,&fn->vars[k].type,0,seen))
                return false;
        }
    }

    for(int i=0;i<program->nr_structures;i++) {
        const struct ain_struct *st=&program->structures[i];
        if(st->nr_members<0)
            return false;
        if(st->nr_members>0 && !st->members)
            return false;
        const char *name=st->name ? st->name : "";
        crc=(uint32_t)crc32(crc,(const Bytef *)name,(uInt)strlen(name)+1);
        g12_program_le32(&crc,(uint32_t)st->nr_members);
        for(int k=0;k<st->nr_members;k++) {
            const char *member=st->members[k].name ? st->members[k].name : "";
            crc=(uint32_t)crc32(crc,(const Bytef *)member,
                (uInt)strlen(member)+1);
            memset(seen,0,sizeof(seen));
            if(!g12_program_type(&crc,&st->members[k].type,0,seen))
                return false;
        }
    }

    for(int i=0;i<program->nr_global_groups;i++) {
        const char *name=program->global_group_names[i] ?
            program->global_group_names[i] : "";
        crc=(uint32_t)crc32(crc,(const Bytef *)name,(uInt)strlen(name)+1);
    }

    int n=snprintf(out,64,"AIN%d.%d-%zu-%08x",program->version,
        program->minor_version,program->code_size,crc);
    if(n<0 || n>=64) {
        out[0]=0;
        return false;
    }
    return true;
}

#endif
