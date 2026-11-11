/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef G12_SAVE_PERF_H
#define G12_SAVE_PERF_H
#include "rance10_diagnostics.h"

enum g12_perf_phase {
    G12_PERF_SELECT, G12_PERF_COLLECT, G12_PERF_NODES, G12_PERF_VALIDATE,
    G12_PERF_METADATA, G12_PERF_PRINT, G12_PERF_PERSIST, G12_PERF_CLEANUP,
    G12_PERF_COUNT
};
struct g12_save_perf {
    bool enabled;
    enum g12_perf_phase phase;
    Uint64 started, mark, ticks[G12_PERF_COUNT];
};
static void g12_perf_begin(struct g12_save_perf *sample)
{
    memset(sample,0,sizeof(*sample));
    sample->enabled=r10_diagnostic_enabled("XSYSTEM4_DIAG_SAVE");
    if(sample->enabled)sample->started=sample->mark=SDL_GetPerformanceCounter();
}
static void g12_perf_phase(struct g12_save_perf *sample,enum g12_perf_phase phase)
{
    if(!sample->enabled)return;
    Uint64 now=SDL_GetPerformanceCounter();
    sample->ticks[sample->phase]+=now-sample->mark;
    sample->mark=now;sample->phase=phase;
}
static void g12_perf_end(struct g12_save_perf *sample,const char *kind,bool ok,
        size_t nodes,size_t words,size_t string_bytes)
{
    if(!sample->enabled)return;
    Uint64 now=SDL_GetPerformanceCounter(),frequency=SDL_GetPerformanceFrequency();
    sample->ticks[sample->phase]+=now-sample->mark;
    double ms=frequency?1000.0/frequency:0.0;
    NOTICE("[global-save-perf] kind=%s ok=%d total_ms=%.3f select_or_read_ms=%.3f "
        "collect_ms=%.3f nodes_ms=%.3f validate_ms=%.3f metadata_ms=%.3f "
        "print_ms=%.3f persist_ms=%.3f cleanup_ms=%.3f nodes=%zu words=%zu string_bytes=%zu",
        kind,ok,(now-sample->started)*ms,sample->ticks[0]*ms,
        sample->ticks[1]*ms,sample->ticks[2]*ms,sample->ticks[3]*ms,
        sample->ticks[4]*ms,sample->ticks[5]*ms,sample->ticks[6]*ms,
        sample->ticks[7]*ms,nodes,words,string_bytes);
}
#endif
