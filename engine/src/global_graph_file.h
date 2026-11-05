/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GLOBAL_GRAPH_FILE_H
#define GLOBAL_GRAPH_FILE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global_graph_envelope.h"
#include "savedata.h"
#include "system4/file.h"

/* filename is relative to config.save_dir, as accepted by savedir_path(). */
static bool g12_write_envelope(const char *filename,const uint8_t *raw,size_t size)
{
    if(!filename || !filename[0])
        return false;

    uint8_t *packed=NULL;
    size_t packed_len=0;
    if(!g12_pack(raw,size,&packed,&packed_len))
        return false;

    char *path=savedir_path(filename);
    if(!path) {
        free(packed);
        return false;
    }
    struct save_file output;
    if(!begin_save_file(&output,path)) {
        free(path);
        free(packed);
        return false;
    }
    free(path);

    bool written = fwrite(packed,1,packed_len,output.file) == packed_len;
    bool success = end_save_file(&output,written);
    free(packed);
    return success;
}

static bool g12_read_envelope(const char *filename,uint8_t **raw,size_t *size)
{
    if(raw) *raw=NULL;
    if(size) *size=0;
    if(!raw || !size || !filename || !filename[0])
        return false;

    char *path=savedir_path(filename);
    if(!path)
        return false;
    FILE *file=file_open_utf8(path,"rb");
    free(path);
    if(!file)
        return false;

    uint8_t *buffer=NULL;
    bool success=false;
    size_t length=0;

    if(fseek(file,0,SEEK_END)!=0)
        goto out;
    long end=ftell(file);
    if(end<0 || (unsigned long)end > (unsigned long)G12_MAX_PACKED)
        goto out;
    if(fseek(file,0,SEEK_SET)!=0)
        goto out;
    length=(size_t)end;
    buffer=malloc(length ? length : 1);
    if(!buffer)
        goto out;
    if(length && fread(buffer,1,length,file)!=length)
        goto out;
    if(fclose(file)!=0) {
        file=NULL;
        goto out;
    }
    file=NULL;
    if(!g12_unpack(buffer,length,raw,size))
        goto out;
    success=true;

out:
    if(!success) {
        if(raw) *raw=NULL;
        if(size) *size=0;
    }
    if(file)
        fclose(file);
    free(buffer);
    return success;
}

#endif
