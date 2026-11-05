/* SPDX-License-Identifier: GPL-2.0-or-later
 * Included once by savedata.c, after global_graph_program.h.
 * Runtime owns registration. Other tools/tests remain uncached by default.
 * Any mutation of hashed AIN bytes/descriptors must invalidate first. */
#ifndef G12_COMMENT_PROGRAM_CACHE_H
#define G12_COMMENT_PROGRAM_CACHE_H
static struct {
    const struct ain *owner;
    uint64_t epoch, computed_epoch;
    bool active, valid;
    char id[64];
} g12_comment_program_cache;

void savedata_program_id_begin(const struct ain *program)
{
    /* Explicit new lifetime, even if malloc reused the identical address. */
    g12_comment_program_cache.epoch++;
    g12_comment_program_cache.owner=program;
    g12_comment_program_cache.active=program!=NULL;
    g12_comment_program_cache.valid=false;
    g12_comment_program_cache.id[0]=0;
}
void savedata_program_id_end(void)
{
    g12_comment_program_cache.epoch++;
    g12_comment_program_cache.owner=NULL;
    g12_comment_program_cache.active=false;
    g12_comment_program_cache.valid=false;
    g12_comment_program_cache.id[0]=0;
}
void savedata_program_id_invalidate(const struct ain *program)
{
    if(g12_comment_program_cache.active && g12_comment_program_cache.owner==program) {
        g12_comment_program_cache.epoch++;
        g12_comment_program_cache.valid=false;
        g12_comment_program_cache.id[0]=0;
    }
}
/* Shared registration token, independent of either fingerprint algorithm. */
bool savedata_program_id_epoch(const struct ain *program, uint64_t *out)
{
    if (!program || !out || !g12_comment_program_cache.active ||
            program != g12_comment_program_cache.owner)
        return false;
    *out = g12_comment_program_cache.epoch;
    return true;
}
static bool g12_comment_program_id(const struct ain *program,char out[64])
{
    if(!program || !out || !g12_comment_program_cache.active ||
            program!=g12_comment_program_cache.owner)
        return g12_program_id(program,out);
    if(g12_comment_program_cache.valid &&
            g12_comment_program_cache.computed_epoch==g12_comment_program_cache.epoch) {
        strcpy(out,g12_comment_program_cache.id);
        return true;
    }
    if(!g12_program_id(program,out))
        return false; /* Do not memoize failures. */
    strcpy(g12_comment_program_cache.id,out);
    g12_comment_program_cache.computed_epoch=g12_comment_program_cache.epoch;
    g12_comment_program_cache.valid=true;
    return true;
}
#endif
