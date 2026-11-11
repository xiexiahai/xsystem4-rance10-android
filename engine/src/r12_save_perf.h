/* Explicit, bounded diagnostics. All times are per-save aggregates. */
#ifndef R12_SAVE_PERF_H
#define R12_SAVE_PERF_H

#include "rance10_diagnostics.h"

enum r12_save_phase {
    R12_PERF_OLD_COMMENTS, R12_PERF_BUILD, R12_PERF_PREPARE,
    R12_PERF_FREE_CANDIDATE, R12_PERF_PRINT, R12_PERF_PERSIST,
    R12_PERF_CLEANUP, R12_PERF_PHASES
};
struct r12_save_perf {
    bool enabled, failed;
    unsigned sample, program_calls, program_calculations;
    enum r12_save_phase phase, failed_phase;
    Uint64 started, mark, ticks[R12_PERF_PHASES], program_ticks;
};
static struct r12_save_perf *r12_save_perf_active;

static void r12_save_perf_begin(struct r12_save_perf *sample)
{
    static int enabled = -1;
    static unsigned issued;
    memset(sample, 0, sizeof(*sample));
    if (enabled < 0) {
        const char *value = getenv("XSYSTEM4_PERF_WINDOW");
        enabled = value && !strcmp(value, "1");
    }
    /* A battle-load regression can appear after many saves. The selected
     * save diagnostic therefore keeps per-save aggregates for the session;
     * the legacy short PERF_WINDOW probe remains capped at eight samples. */
    bool continuous = r10_diagnostic_enabled("XSYSTEM4_DIAG_SAVE");
    if ((!enabled && !continuous) || (!continuous && issued >= 8)) return;
    sample->enabled = true;
    sample->sample = ++issued;
    sample->phase = R12_PERF_OLD_COMMENTS;
    sample->started = sample->mark = SDL_GetPerformanceCounter();
    r12_save_perf_active = sample;
}

static void r12_save_perf_phase(struct r12_save_perf *sample, enum r12_save_phase next)
{
    if (!sample || !sample->enabled) return;
    Uint64 now = SDL_GetPerformanceCounter();
    sample->ticks[sample->phase] += now - sample->mark;
    sample->mark = now;
    sample->phase = next;
}

static double r12_save_perf_ms(Uint64 ticks, Uint64 frequency)
{ return frequency ? ticks * 1000.0 / frequency : 0.0; }

static void r12_save_perf_failure(struct r12_save_perf *sample)
{
    if (sample && sample->enabled && !sample->failed) {
        sample->failed = true;
        sample->failed_phase = sample->phase;
    }
}

static void r12_save_perf_end(struct r12_save_perf *sample, bool ok)
{
    if (!sample->enabled) return;
    Uint64 now = SDL_GetPerformanceCounter();
    sample->ticks[sample->phase] += now - sample->mark;
    Uint64 frequency = SDL_GetPerformanceFrequency();
    const char *phases[] = {"old_read_comments", "build", "prepare", "free_candidate", "print", "persist", "cleanup"};
    if (r12_save_perf_active == sample) r12_save_perf_active = NULL;
    NOTICE("[resume-perf] sample=%u ok=%d stage=%s total_ms=%.3f "
        "old_comments_ms=%.3f build_ms=%.3f prepare_ms=%.3f free_candidate_ms=%.3f "
        "print_ms=%.3f persist_ms=%.3f cleanup_ms=%.3f "
        "program_id_child_ms=%.3f program_calls=%u program_calculations=%u",
        sample->sample, ok, phases[!ok && sample->failed ? sample->failed_phase : sample->phase], r12_save_perf_ms(now-sample->started,frequency),
        r12_save_perf_ms(sample->ticks[R12_PERF_OLD_COMMENTS],frequency),
        r12_save_perf_ms(sample->ticks[R12_PERF_BUILD],frequency),
        r12_save_perf_ms(sample->ticks[R12_PERF_PREPARE],frequency),
        r12_save_perf_ms(sample->ticks[R12_PERF_FREE_CANDIDATE],frequency),
        r12_save_perf_ms(sample->ticks[R12_PERF_PRINT],frequency),
        r12_save_perf_ms(sample->ticks[R12_PERF_PERSIST],frequency),
        r12_save_perf_ms(sample->ticks[R12_PERF_CLEANUP],frequency),
        r12_save_perf_ms(sample->program_ticks,frequency), sample->program_calls, sample->program_calculations);
}
#endif
