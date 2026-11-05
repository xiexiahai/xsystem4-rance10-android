/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GLOBAL_GRAPH_ENVELOPE_H
#define GLOBAL_GRAPH_ENVELOPE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <zlib.h>

#define G12_HEADER_SIZE ((size_t)24)
#define G12_RAW_LIMIT ((uint32_t)(64u * 1024u * 1024u))
#define G12_COMPRESSED_LIMIT ((uint32_t)(G12_RAW_LIMIT + 1024u * 1024u))
#define G12_MAX_PACKED ((size_t)G12_COMPRESSED_LIMIT + G12_HEADER_SIZE)

static const uint8_t g12_magic[8] = {'R','1','0','G','S','1',0,0};

static inline uint32_t g12_read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void g12_write32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static inline void g12_clear_output(uint8_t **out,size_t *out_len)
{
    if(out) *out=NULL;
    if(out_len) *out_len=0;
}

/* src == NULL is legal only when len == 0. On failure *out is NULL and
 * *out_len is 0. Success returns a malloc'd buffer owned by the caller. */
static bool g12_pack(const uint8_t *src,size_t len,uint8_t **out,size_t *out_len)
{
    g12_clear_output(out,out_len);
    if(!out || !out_len)
        return false;
    if(len > (size_t)G12_RAW_LIMIT)
        return false;
    if(!src && len != 0)
        return false;

    z_stream zs;
    memset(&zs,0,sizeof(zs));
    if(deflateInit(&zs,6) != Z_OK)
        return false;

    uLong bound=deflateBound(&zs,(uLong)len);
    if(bound > (uLong)G12_COMPRESSED_LIMIT) {
        deflateEnd(&zs);
        return false;
    }
    size_t cap=bound ? (size_t)bound : 1;
    uint8_t *compressed=malloc(cap);
    if(!compressed) {
        deflateEnd(&zs);
        return false;
    }

    zs.next_in=(Bytef *)(uintptr_t)(len ? src : (const uint8_t *)"");
    zs.avail_in=(uInt)len;
    zs.next_out=compressed;
    zs.avail_out=(uInt)cap;
    int zr=deflate(&zs,Z_FINISH);
    if(zr != Z_STREAM_END || zs.total_in != (uLong)len ||
            zs.total_out > (uLong)G12_COMPRESSED_LIMIT) {
        deflateEnd(&zs);
        free(compressed);
        return false;
    }
    size_t compressed_len=(size_t)zs.total_out;
    deflateEnd(&zs);

    if(compressed_len > (size_t)G12_COMPRESSED_LIMIT ||
            compressed_len > SIZE_MAX - G12_HEADER_SIZE) {
        free(compressed);
        return false;
    }
    size_t file_len=G12_HEADER_SIZE + compressed_len;
    if(file_len > G12_MAX_PACKED) {
        free(compressed);
        return false;
    }

    uint8_t *envelope=malloc(file_len);
    if(!envelope) {
        free(compressed);
        return false;
    }
    memcpy(envelope,g12_magic,sizeof(g12_magic));
    g12_write32(envelope+8,(uint32_t)len);
    g12_write32(envelope+12,(uint32_t)compressed_len);
    uLong crc=crc32(0L,Z_NULL,0);
    if(len)
        crc=crc32(crc,(const Bytef *)src,(uInt)len);
    g12_write32(envelope+16,(uint32_t)crc);
    g12_write32(envelope+20,1u);
    memcpy(envelope+G12_HEADER_SIZE,compressed,compressed_len);
    free(compressed);
    *out=envelope;
    *out_len=file_len;
    return true;
}

/* Exact-length, CRC-checked zlib container. A successful empty raw stream still
 * returns a one-byte allocation. */
static bool g12_unpack(const uint8_t *src,size_t len,uint8_t **out,size_t *out_len)
{
    g12_clear_output(out,out_len);
    if(!out || !out_len || !src)
        return false;
    if(len < G12_HEADER_SIZE || len > G12_MAX_PACKED)
        return false;
    if(memcmp(src,g12_magic,sizeof(g12_magic)) != 0)
        return false;
    if(g12_read32(src+20) != 1u)
        return false;

    uint32_t raw_len=g12_read32(src+8);
    uint32_t compressed_len=g12_read32(src+12);
    if(raw_len > G12_RAW_LIMIT || compressed_len > G12_COMPRESSED_LIMIT)
        return false;
    if((size_t)compressed_len + G12_HEADER_SIZE != len)
        return false;

    size_t raw_cap=(size_t)raw_len + 1; /* room for zlib's end check; empty still >=1 */
    uint8_t *raw=malloc(raw_cap);
    if(!raw)
        return false;

    z_stream zs;
    memset(&zs,0,sizeof(zs));
    if(inflateInit(&zs) != Z_OK) {
        free(raw);
        return false;
    }
    zs.next_in=(Bytef *)(uintptr_t)(src + G12_HEADER_SIZE);
    zs.avail_in=(uInt)compressed_len;
    zs.next_out=raw;
    zs.avail_out=(uInt)raw_cap;
    int zr=inflate(&zs,Z_FINISH);
    if(zr != Z_STREAM_END || zs.total_in != (uLong)compressed_len ||
            zs.total_out != (uLong)raw_len) {
        inflateEnd(&zs);
        free(raw);
        return false;
    }
    inflateEnd(&zs);

    uLong crc=crc32(0L,Z_NULL,0);
    if(raw_len)
        crc=crc32(crc,(const Bytef *)raw,(uInt)raw_len);
    if((uint32_t)crc != g12_read32(src+16)) {
        free(raw);
        return false;
    }

    *out=raw;
    *out_len=(size_t)raw_len;
    return true;
}

#endif
