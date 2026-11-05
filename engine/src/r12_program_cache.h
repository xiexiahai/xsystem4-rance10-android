/* R12's existing algorithm is unchanged; only registered stable AIN epochs
 * may reuse its result. No file, JSON, heap image or validation is cached. */
#ifndef R12_PROGRAM_CACHE_H
#define R12_PROGRAM_CACHE_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

struct ain;
bool savedata_program_id_epoch(const struct ain *program, uint64_t *out);

static struct {
    const struct ain *owner;
    uint64_t epoch;
    bool valid;
    char id[64];
} r12_program_cache;

static void r12_cached_program_id(const struct ain *program, char out[64],
        void (*compute)(char out[64]))
{
    uint64_t epoch;
    if (!savedata_program_id_epoch(program, &epoch)) {
        r12_program_cache.valid = false;
        r12_program_cache.owner = NULL;
        compute(out);
        return;
    }
    if (r12_program_cache.valid && r12_program_cache.owner == program &&
            r12_program_cache.epoch == epoch) {
        strcpy(out, r12_program_cache.id);
        return;
    }
    r12_program_cache.valid = false;
    r12_program_cache.owner = NULL;
    compute(out);
    uint64_t after;
    if (!savedata_program_id_epoch(program, &after) || after != epoch)
        return; /* Never retain a computation across a lifetime change. */
    strcpy(r12_program_cache.id, out);
    r12_program_cache.owner = program;
    r12_program_cache.epoch = epoch;
    r12_program_cache.valid = true;
}
#endif
