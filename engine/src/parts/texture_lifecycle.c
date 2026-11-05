/* Main-thread residency lifecycle. CPU pixels replace owned GPU storage while
 * parked; part models, aliases and current transforms remain live. */
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include "system4.h"
#include "system4/string.h"
#include "system4/flat.h"
#include "parts.h"
#include "parts_internal.h"
#include "texture_slots.inc"
#include "texture_parking.inc"

_Static_assert(offsetof(struct parts,sp)==0,"Parts sprite must remain the first field");
#define PARTS_PARK_BYTES ((size_t)256u*1024u*1024u)
static size_t parked_bytes;
static unsigned parked_parts;
#include "texture_probe.inc"

bool parts_textures_any_parked(void) {return parked_parts!=0;}

void parts_textures_require_resident(struct parts *parts)
{
    if (!parts || !parts->parked_textures) return;
    size_t bytes=parts_texture_park_bytes(parts->parked_textures);
    if (!parts_texture_resume(&parts->parked_textures))
        ERROR("Parts texture resume failed for part %d before publication",parts->no);
    parked_bytes-=bytes;parked_parts--;
    if (getenv("XSYSTEM4_TRACE_TEXTURES"))
        NOTICE("[texture] resume part=%d bytes=%zu parked_parts=%u parked_bytes=%zu",parts->no,bytes,parked_parts,parked_bytes);
    parts_dirty(parts);
}

void parts_textures_require_state(struct parts_state *state)
{
    if (!parked_parts) return;
    struct parts *p;
    PARTS_LIST_FOREACH(p) {
        if (!p->parked_textures) continue;
        for (int i=0;i<PARTS_NR_STATES;i++) if (&p->states[i]==state) {
            parts_textures_require_resident(p);return;
        }
    }
}

void parts_textures_require_common(struct parts *p,struct parts_common *common)
{
    if (!p || !p->parked_textures) return;
    for (int i=0;i<PARTS_NR_STATES;i++) if (&p->states[i].common==common) {
        parts_textures_require_resident(p);return;
    }
}

void parts_textures_discard(struct parts *p)
{
    if (!p || !p->parked_textures) return;
    parked_bytes-=parts_texture_park_bytes(p->parked_textures);parked_parts--;
    parts_texture_discard(&p->parked_textures);
    if (getenv("XSYSTEM4_TRACE_TEXTURES"))
        NOTICE("[texture] discard part=%d parked_parts=%u parked_bytes=%zu",p->no,parked_parts,parked_bytes);
}

void PE_SuspendTexture(int number)
{
    struct parts *p=parts_try_get(number);
    if (!p || p->parked_textures) return;
    if (!parts_assets_async_flush(5000)) ERROR("Texture suspend timed out waiting for owned async pixels");
    if(p->widget && p->widget->textbox)parts_textbox_open(p,false);
    if (!parts_widget_build(p)) ERROR("Texture suspend could not finish part %d widget",number);
    Texture **slots=NULL;size_t count=0;
    if (!parts_texture_slots_collect(p,&slots,&count))
        ERROR("Texture suspend does not support part %d state ownership",number);
    bool ok=parts_texture_suspend(slots,count,PARTS_PARK_BYTES-parked_bytes,&p->parked_textures);
    free(slots);
    if (!ok) ERROR("Texture suspend failed before deleting live textures for part %d",number);
    parked_bytes+=parts_texture_park_bytes(p->parked_textures);parked_parts++;
    if (getenv("XSYSTEM4_TRACE_TEXTURES"))
        NOTICE("[texture] suspend part=%d images=%zu bytes=%zu parked_parts=%u parked_bytes=%zu",number,p->parked_textures->image_count,parts_texture_park_bytes(p->parked_textures),parked_parts,parked_bytes);
    parts_input_reset_drag(p);
    parts_dirty(p);
    parts_textures_roundtrip_probe(p);
}

void PE_ResumeTexture(int number)
{
    parts_textures_require_resident(parts_try_get(number));
}
