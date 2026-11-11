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
#include "rance10_diagnostics.h"
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
    bool trace_save=r10_diagnostic_enabled("XSYSTEM4_DIAG_SAVE");
    Uint32 start_ms=trace_save ? SDL_GetTicks() : 0;
    if (!parts_assets_async_flush(5000)) ERROR("Texture suspend timed out waiting for owned async pixels");
    Uint32 flushed_ms=trace_save ? SDL_GetTicks() : 0;
    if(p->widget && p->widget->textbox)parts_textbox_open(p,false);
    if (!parts_widget_build(p)) ERROR("Texture suspend could not finish part %d widget",number);
    Texture **slots=NULL;size_t count=0;
    if (!parts_texture_slots_collect(p,&slots,&count))
        ERROR("Texture suspend does not support part %d state ownership",number);
    Uint32 collected_ms=trace_save ? SDL_GetTicks() : 0;
    bool ok=parts_texture_suspend(slots,count,PARTS_PARK_BYTES-parked_bytes,&p->parked_textures);
    free(slots);
    if (!ok) ERROR("Texture suspend failed before deleting live textures for part %d",number);
    parked_bytes+=parts_texture_park_bytes(p->parked_textures);parked_parts++;
    if (getenv("XSYSTEM4_TRACE_TEXTURES"))
        NOTICE("[texture] suspend part=%d images=%zu bytes=%zu parked_parts=%u parked_bytes=%zu",number,p->parked_textures->image_count,parts_texture_park_bytes(p->parked_textures),parked_parts,parked_bytes);
    parts_input_reset_drag(p);
    parts_dirty(p);
    parts_textures_roundtrip_probe(p);
    if(trace_save) {
        Uint32 end_ms=SDL_GetTicks();
        NOTICE("[parts-park-perf] part=%d slots=%zu bytes=%zu flush_ms=%u collect_ms=%u park_ms=%u total_ms=%u",
            number,count,parts_texture_park_bytes(p->parked_textures),(unsigned)(flushed_ms-start_ms),
            (unsigned)(collected_ms-flushed_ms),(unsigned)(end_ms-collected_ms),(unsigned)(end_ms-start_ms));
    }
}

void PE_ResumeTexture(int number)
{
    parts_textures_require_resident(parts_try_get(number));
}


/* Immutable, owner-local copy used by non-replayable construction snapshots. */
#include "parts_texture_pixels.h"
int parts_copy_texture_pixels(const struct parts *owner,const struct texture *texture,struct parts_texture_pixels *out)
{
 if(!texture||!out)return -1;
 memset(out,0,sizeof(*out));
 if(texture->handle) {
  if(texture->w<1||texture->h<1||texture->w>8192||texture->h>8192||
     (size_t)texture->w*texture->h*4>64u*1024u*1024u)return -1;
  out->w=texture->w;out->h=texture->h;
  GLint binding;glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
  glBindTexture(GL_TEXTURE_2D,texture->handle);
  glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&out->min_filter);
  glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&out->mag_filter);
  glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,&out->wrap_s);
  glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,&out->wrap_t);
  glBindTexture(GL_TEXTURE_2D,(GLuint)binding);
  out->rgba=gfx_get_pixels((Texture*)texture);
  if(!out->rgba||glGetError()!=GL_NO_ERROR){free(out->rgba);out->rgba=NULL;return -1;}
  return 1;
 }
 if(owner && owner->parked_textures) {
  struct parts_texture_park *park=owner->parked_textures;
  for(size_t i=0;i<park->slot_count;i++)if(park->slots[i].tex==texture) {
   int n=park->slots[i].image;if(n<0)return 0;
   if((size_t)n>=park->image_count)return -1;
   const struct parts_texture_park_image *im=&park->images[n];
   if(!im->rgba||im->w<1||im->h<1||im->w>8192||im->h>8192||
      (size_t)im->w*im->h*4>64u*1024u*1024u)return -1;
   size_t bytes=(size_t)im->w*im->h*4;
   out->rgba=malloc(bytes);if(!out->rgba)return -1;
   memcpy(out->rgba,im->rgba,bytes);out->w=im->w;out->h=im->h;
   out->min_filter=im->min_filter;out->mag_filter=im->mag_filter;
   out->wrap_s=im->wrap_s;out->wrap_t=im->wrap_t;
   return 1;
  }
 }
 return 0;
}
