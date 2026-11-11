/* FLAT preparation owns every input. Only RGBA staging runs on the worker.
 * Archive access, parser/codec error paths, GL and publication stay on main. */
#include "async_jobs.h"
#include "flat_pixels.inc"
#include "cg_prepare.inc"

#ifndef PARTS_CG_PREPARE_MAIN
#define PARTS_CG_PREPARE_MAIN parts_cg_prepare_main
#endif

#define FLAT_PREPARE_BYTES (128u * 1024u * 1024u)
struct flat_cpu_image {
	struct cg *source;
	bool source_borrowed; // Another image in this request owns the same immutable decode.
	bool atlas;
	uint32_t x,y,w,h;
	struct parts_flat_pixels pixels;
};
struct flat_prepared {
	struct flat *flat;
	struct string *name;
	size_t count,bytes;
	struct flat_cpu_image *images;
};
struct flat_request {
	int number,state;
	bool is_cg;
	bool is_construction;
	size_t bytes;
	struct flat_prepared *prepared;
	struct parts_cg_prepared *cg_prepared;
};
#ifndef PARTS_FLAT_PREPARE_MAIN
#define PARTS_FLAT_PREPARE_MAIN flat_prepare_main
#endif
static struct parts_async_executor *flat_executor;
static size_t flat_pending_bytes;
static bool flat_exit_registered;

static void flat_prepared_free(struct flat_prepared *p)
{
	if (!p) return;
	for (size_t i=0;i<p->count;i++) {
		if (!p->images[i].source_borrowed) cg_free(p->images[i].source);
		parts_flat_pixels_free(&p->images[i].pixels);
	}
	free(p->images);
	if (p->flat) flat_free(p->flat);
	if (p->name) free_string(p->name);
	free(p);
}

static struct flat_prepared *flat_prepare_main(struct string *name)
{
	if (!name || !name->size) return NULL;
	struct archive_data *entry=asset_get_by_name(ASSET_FLAT,name->text,NULL);
	if (!entry) return NULL;
	size_t remaining=FLAT_PREPARE_BYTES-flat_pending_bytes;
	if (!entry->data || entry->size<16 || entry->size>32u*1024u*1024u || entry->size>remaining) {
		archive_free_data(entry);return NULL;
	}
	uint8_t *data=xmalloc(entry->size);
	size_t size=entry->size;
	memcpy(data,entry->data,size);
	archive_free_data(entry);
	int error;
	struct flat *flat=flat_open(data,size,&error);
	if (!flat) {free(data);return NULL;}
	flat->needs_free=true;
	if (flat->nr_libraries>4096) {flat_free(flat);return NULL;}
	struct flat_prepared *p=xcalloc(1,sizeof(*p));
	p->flat=flat;p->name=string_dup(name);p->bytes=size;
	p->count=flat->nr_libraries;
	p->images=xcalloc(p->count,sizeof(*p->images));
	for (size_t i=0;i<p->count;i++) {
		struct flat_library *lib=&flat->libraries[i];
		if (lib->type!=FLAT_LIB_CG) continue;
		if (!lib->cg.data || lib->cg.size<4) {flat_prepared_free(p);return NULL;}
		struct flat_cpu_image *image=&p->images[i];
		image->atlas=lib->atlas_region;
		image->x=lib->atlas_x;image->y=lib->atlas_y;
		image->w=lib->atlas_w;image->h=lib->atlas_h;
		for (size_t j=0;j<i;j++) {
			struct flat_library *prior=&flat->libraries[j];
			if (p->images[j].source && prior->type==FLAT_LIB_CG &&
				prior->cg.data==lib->cg.data && prior->cg.size==lib->cg.size) {
				image->source=p->images[j].source;image->source_borrowed=true;break;
			}
		}
		struct cg_metrics metrics={0};
		struct archive_data bitmap={.data=(uint8_t*)lib->cg.data,.size=lib->cg.size};
		if (image->source) metrics=image->source->metrics;
		else if (!cg_get_metrics_data(&bitmap,&metrics)) {flat_prepared_free(p);return NULL;}
		if (metrics.w<1 || metrics.w>8192 || metrics.h<1 || metrics.h>8192 ||
			(size_t)metrics.w*metrics.h*4>64u*1024u*1024u) {
			flat_prepared_free(p);return NULL;
		}
		if (image->atlas && (!image->w || !image->h ||
			image->x>(unsigned)metrics.w || image->y>(unsigned)metrics.h ||
			image->w>(unsigned)metrics.w-image->x || image->h>(unsigned)metrics.h-image->y)) {
			flat_prepared_free(p);return NULL;
		}
		size_t decoded=(size_t)metrics.w*metrics.h*4;
		size_t staged=image->atlas ? (size_t)image->w*image->h*4 : decoded;
		size_t needed=staged+(image->source_borrowed ? 0 : decoded);
		if (needed>remaining-p->bytes) {flat_prepared_free(p);return NULL;}
		if (!image->source) image->source=cg_load_buffer((uint8_t*)lib->cg.data,lib->cg.size);
		struct cg *cg=image->source;
		if (!cg || !cg->pixels || cg->metrics.w!=metrics.w || cg->metrics.h!=metrics.h) {
			flat_prepared_free(p);return NULL;
		}
		p->bytes+=needed; // Each unique decode once, plus each actual staged crop.
	}
	return p;
}

#ifndef PARTS_FLAT_PIXEL_COPY
#define PARTS_FLAT_PIXEL_COPY parts_flat_pixels_copy
#endif
static bool flat_prepare_pixels(struct flat_prepared *p)
{
	// No GL, archives, Parts, VM, strings or fatal parser/codec calls here.
	for (size_t i=0;i<p->count;i++) {
		struct flat_cpu_image *image=&p->images[i];
		struct cg *cg=image->source;
		if (!cg) continue;
		if (!PARTS_FLAT_PIXEL_COPY(cg->pixels,(size_t)cg->metrics.w*cg->metrics.h*4,
			cg->metrics.w,cg->metrics.h,image->atlas,image->x,image->y,image->w,image->h,
			&image->pixels)) return false;
	}
	return true;
}

#ifndef PARTS_FLAT_TEXTURE_UPLOAD
#define PARTS_FLAT_TEXTURE_UPLOAD gfx_init_texture_with_pixels
#endif
static bool flat_materialize(struct flat_prepared *p,struct parts_flat *next)
{
	memset(next,0,sizeof(*next));
	next->flat=p->flat;p->flat=NULL;
	next->name=p->name;p->name=NULL;
	next->fps_rate=1.f;
	next->end_frame=flat_compute_max_frame(next->flat->timelines,next->flat->nr_timelines)-1;
	next->root_state=flat_layer_state_new(next->flat->nr_timelines);
	next->nr_libraries=p->count;
	next->textures=xcalloc(p->count,sizeof(Texture));
	next->stop_motion_frames=xcalloc(p->count,sizeof(struct flat_stop_motion_frames));
	int w=next->flat->hdr.width,h=next->flat->hdr.height;
	for (size_t i=0;i<p->count;i++) {
		struct parts_flat_pixels *pixels=&p->images[i].pixels;
		if (pixels->rgba) {
			GLenum prior_error=glGetError();
			if (prior_error!=GL_NO_ERROR) {
				WARNING("flat: existing GL error before texture upload: %u",(unsigned)prior_error);
				for (int j=0;j<16 && glGetError()!=GL_NO_ERROR;j++) {}
			}
			PARTS_FLAT_TEXTURE_UPLOAD(&next->textures[i],pixels->w,pixels->h,pixels->rgba);
			GLenum upload_error=glGetError();
			if (!next->textures[i].handle || upload_error!=GL_NO_ERROR) {
				WARNING("flat: texture upload failed: handle=%u error=%u",(unsigned)next->textures[i].handle,(unsigned)upload_error);
				parts_flat_free(next);memset(next,0,sizeof(*next));return false;
			}
		}
		struct flat_library *lib=&next->flat->libraries[i];
		if (lib->type==FLAT_LIB_STOP_MOTION) build_stop_motion_frames(next,(int)i);
		if (lib->type==FLAT_LIB_EMITTER) {
			if (lib->emitter.end_pos_type!=0 && lib->emitter.end_pos_type!=1)
				WARNING("flat: unsupported emitter end_pos_type %d",lib->emitter.end_pos_type);
			if (lib->emitter.pos_track_mode==0)
				WARNING("flat: unsupported emitter pos_track_mode %d",lib->emitter.pos_track_mode);
		}
	}
	if (w<=0 || h<=0) {
		w=h=0;
		for (size_t i=0;i<p->count;i++) {w=max(w,next->textures[i].w);h=max(h,next->textures[i].h);}
	}
	next->common.w=w;next->common.h=h;
	return true;
}

static void flat_publish(struct parts *parts,struct parts_flat *target,struct parts_flat *next)
{
	int w=next->common.w,h=next->common.h;
	next->common=target->common; // Keep surface/origin data restored by the caller.
	next->common.texture=(Texture){0};
	parts_flat_free(target);
	*target=*next;memset(next,0,sizeof(*next));
	if (w>0 && h>0) parts_set_dims(parts,&target->common,w,h);
}

bool parts_flat_load(struct parts *parts,struct parts_flat *target,struct string *name)
{
	struct flat_prepared *p=PARTS_FLAT_PREPARE_MAIN(name);
	if (!p) return false;
	struct parts_flat next;
	bool ok=flat_prepare_pixels(p) && flat_materialize(p,&next);
	if (ok) flat_publish(parts,target,&next);
	flat_prepared_free(p);
	return ok;
}

static bool flat_request_work(void *payload)
{
	struct flat_request *r=payload;
	return r->is_cg ? parts_cg_prepare_pixels(r->cg_prepared) : flat_prepare_pixels(r->prepared);
}

static void flat_request_free(void *payload)
{
	struct flat_request *r=payload;
	flat_pending_bytes-=r->bytes;
	if (r->is_cg) parts_cg_prepared_free(r->cg_prepared);
	else flat_prepared_free(r->prepared);
	free(r);
}

void parts_flat_async_reset(void)
{
	parts_async_executor_free(flat_executor,flat_request_free);
	flat_executor=NULL;
	flat_pending_bytes=0;
}

void parts_flat_async_cancel_state(struct parts_state *state)
{
	if (state->async_ticket && flat_executor)
		parts_async_cancel(flat_executor,state->async_ticket);
	state->async_ticket=0;
}

/* The original caller may submit an entire mini-map before its next frame.
 * Retain the 64-job/128MiB limits: publish completed requests and apply short
 * bounded backpressure only to queue saturation. Only the classified CG credit refusal
 * below may retry preparation once; other failures keep their refusal path. No
 * Part/slot/texture pointer is held across this main-thread pump. */
static bool parts_asset_wait_queue_room(void)
{
    Uint32 started = SDL_GetTicks();
    while (parts_async_queue_full(flat_executor)) {
        parts_flat_async_pump();
        if (!parts_async_queue_full(flat_executor))
            return true;
        if ((Uint32)(SDL_GetTicks() - started) >= 250)
            return false;
        SDL_Delay(1);
    }
    return true;
}

/* Only a precise CG decode/staging-credit refusal reaches this pump. Never
 * publish on the ordinary preparation path; keep intrinsic limits immediate. */
static bool parts_asset_wait_prepare_room(size_t required)
{
	if (!flat_pending_bytes || !required || required>FLAT_PREPARE_BYTES) return false;
	Uint32 started=SDL_GetTicks();
	while (required>FLAT_PREPARE_BYTES-flat_pending_bytes) {
		parts_flat_async_pump();
		if (required<=FLAT_PREPARE_BYTES-flat_pending_bytes) return true;
		if ((Uint32)(SDL_GetTicks()-started)>=250) return false;
		SDL_Delay(1);
	}
	return true;
}

static bool parts_asset_async_owner_allowed(int number)
{
	struct parts *owner=parts_try_get(number);
	return !owner || !owner->widget;
}

static bool parts_asset_enqueue(int number,int state,struct flat_prepared *p,struct parts_cg_prepared *cg, bool construction)
{
	size_t bytes=cg ? cg->bytes : p->bytes;
	if (bytes>FLAT_PREPARE_BYTES-flat_pending_bytes) return false;
	if (!flat_executor) {
		// The observed first map burst submits 51 assets before its next frame.
		// Keep a finite queue and the same byte budget, including DONE jobs.
		flat_executor=parts_async_executor_new(64);
		if (!flat_executor) return false;
		if (!flat_exit_registered) {atexit(parts_flat_async_reset);flat_exit_registered=true;}
	}
	if (!parts_asset_wait_queue_room()) return false;
	struct flat_request *r=malloc(sizeof(*r));
	if (!r) return false;
	*r=(struct flat_request){.number=number,.state=state,.is_cg=cg!=NULL,.is_construction=construction,.bytes=bytes,.prepared=p,.cg_prepared=cg};
	uint64_t ticket=parts_async_submit(flat_executor,r,flat_request_work);
	if (!ticket) {free(r);return false;}
	flat_pending_bytes+=bytes;
	struct parts *parts=parts_get(number);
	parts_textures_require_resident(parts);
	struct parts_state *slot=&parts->states[state];
	parts_flat_async_cancel_state(slot);
	slot->async_ticket=ticket;
	return true;
}

static bool flat_enqueue(int number,int state,struct flat_prepared *p)
{
	return parts_asset_enqueue(number,state,p,NULL,false);
}

bool PE_SetPartsCGThread(int number,struct string *name,int state)
{
	if (!parts_state_valid(--state)) return false;
	if (!parts_asset_async_owner_allowed(number)) return false;
	/* Clear detail even when a fixture overrides the real prepare helper. */
	parts_cg_prepare_budget_required=0;
	struct parts_cg_prepared *p=PARTS_CG_PREPARE_MAIN(name,FLAT_PREPARE_BYTES-flat_pending_bytes);
	if (!p && parts_asset_wait_prepare_room(parts_cg_prepare_budget_required)) {
		/* No Part/slot/texture pointer is held across the pump. */
		if (!parts_asset_async_owner_allowed(number)) return false;
		parts_cg_prepare_budget_required=0;
		p=PARTS_CG_PREPARE_MAIN(name,FLAT_PREPARE_BYTES-flat_pending_bytes);
	}
	if (!p) return false;
	if (parts_asset_enqueue(number,state,NULL,p,false)) return true;
	parts_cg_prepared_free(p);return false;
}

bool PE_SetPartsFlatThread(int number,struct string *name,int state)
{
	if (!parts_state_valid(--state)) return false;
	struct parts *owner=parts_try_get(number);
	if (owner && owner->widget) return false;
	struct flat_prepared *p=PARTS_FLAT_PREPARE_MAIN(name);
	if (!p) return false;
	if (flat_enqueue(number,state,p)) return true;
	flat_prepared_free(p);return false;
}


bool PE_BuildPartsConstructionProcessThread(int number, int state)
{
    if (state < 1 || state > PARTS_NR_STATES) return false;
    struct parts *owner = parts_try_get(number);
    if (!owner || owner->widget || owner->states[state-1].type != PARTS_CONSTRUCTION_PROCESS) return false;
    struct parts_cg_prepared *p = calloc(1, sizeof(*p));
    if (!p) return false;
    p->source = parts_construction_snapshot(owner, state-1, FLAT_PREPARE_BYTES-flat_pending_bytes);
    if (!p->source) {free(p);return false;}
    p->bytes = (size_t)p->source->metrics.w * p->source->metrics.h * 8;
    if (parts_asset_enqueue(number, state-1, NULL, p, true)) return true;
    parts_cg_prepared_free(p);return false;
}

bool PE_IsThreadLoading(int number,int state)
{
	if (!parts_state_valid(--state)) return false;
	struct parts *p=parts_try_get(number);
	return p && p->states[state].async_ticket!=0;
}

bool PE_IsExistWorkerThread(void)
{
	return parts_async_busy(flat_executor);
}

static bool flat_pending_write(int number,int state)
{
	if (!PE_IsThreadLoading(number,state)) return false;
	WARNING("FLAT state %d/%d is pending; state-specific mutation rejected",number,state);
	return true;
}

#ifndef PARTS_CG_TEXTURE_UPLOAD
#define PARTS_CG_TEXTURE_UPLOAD gfx_init_texture_with_pixels
#endif
static bool cg_materialize(struct parts_cg_prepared *p,struct parts_cg *next)
{
	memset(next,0,sizeof(*next));
	if (!p->pixels.rgba || p->pixels.w<=0 || p->pixels.h<=0) return false;
	GLenum prior=glGetError();
	if (prior!=GL_NO_ERROR) {
		WARNING("CG: existing GL error before texture upload: %u",(unsigned)prior);
		for (int i=0;i<16 && glGetError()!=GL_NO_ERROR;i++) {}
	}
	PARTS_CG_TEXTURE_UPLOAD(&next->common.texture,p->pixels.w,p->pixels.h,p->pixels.rgba);
	GLenum error=glGetError();
	if (!next->common.texture.handle || error!=GL_NO_ERROR) {
		WARNING("CG: texture upload failed: handle=%u error=%u",(unsigned)next->common.texture.handle,(unsigned)error);
		gfx_delete_texture(&next->common.texture);return false;
	}
	next->common.w=p->pixels.w;next->common.h=p->pixels.h;
	next->no=p->no;next->name=p->name;p->name=NULL;
	return true;
}

void parts_flat_async_pump(void)
{
	struct parts_async_result result;
	while (parts_async_take(flat_executor,&result)) {
		struct flat_request *r=result.payload;
		struct parts *p=parts_try_get(r->number);
		struct parts_state *slot=p ? &p->states[r->state] : NULL;
		if (!slot || slot->async_ticket!=result.id || result.cancelled) {
			flat_request_free(r);continue;
		}

        if (r->is_construction) {
            struct parts_cg next;
            if (!result.ok || slot->type != PARTS_CONSTRUCTION_PROCESS ||
                    !cg_materialize(r->cg_prepared, &next)) {
                flat_request_free(r);
                ERROR("Asynchronous construction failed before publication");return;
            }
            slot->async_ticket = 0;
            gfx_delete_texture(&slot->cproc.common.texture);
            slot->cproc.common.texture = next.common.texture;
            parts_set_dims(p, &slot->cproc.common, next.common.w, next.common.h);
            parts_dirty(p);
            flat_request_free(r);continue;
        }
		if (r->is_cg) {
			struct parts_cg next;
			if (!result.ok || !cg_materialize(r->cg_prepared,&next)) {
				WARNING("Asynchronous CG preparation/commit failed for part %d",r->number);
				flat_request_free(r);ERROR("Asynchronous CG failed before publication");return;
			}
			slot->async_ticket=0;
			parts_cg_publish_state(p,r->state,&next);
			flat_request_free(r);continue;
		}
		struct parts_flat next;
		if (!result.ok || !flat_materialize(r->prepared,&next)) {
			WARNING("Asynchronous FLAT preparation/commit failed for part %d",r->number);
			flat_request_free(r);
			ERROR("Asynchronous FLAT failed before publication");
			return; // Fatal on main, before an observer can mistake failure for completion.
		}
		slot->async_ticket=0;
		struct parts_common common=slot->type==PARTS_FLAT ? slot->common : (struct parts_common){0};
		parts_state_reset(slot,PARTS_FLAT);
		slot->flat.common=common;
		flat_publish(p,&slot->flat,&next);
		slot->flat.needs_advance=true;
		parts_dirty(p);
		flat_request_free(r);
	}
}

bool parts_assets_async_flush(unsigned timeout_ms)
{
	Uint32 start=SDL_GetTicks();
	for (;;) {
		parts_flat_async_pump();
		if (!PE_IsExistWorkerThread()) return true;
		if ((Uint32)(SDL_GetTicks()-start)>=timeout_ms) return false;
		SDL_Delay(1);
	}
}
