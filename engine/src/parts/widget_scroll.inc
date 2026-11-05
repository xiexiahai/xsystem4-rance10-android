/* Owned, independently animated scrollbar roles. Included by widget.c. */
static const int scroll_bases[4]={IMAGE_THUMB,IMAGE_PREVIOUS,IMAGE_NEXT,IMAGE_BACKGROUND};

void parts_widget_scroll_art_free(struct parts_widget *w)
{
    for(int i=7;i<20;i++) {
        if(w->scroll_flats[i]) {
            gfx_delete_texture(&w->scroll_flats[i]->common.texture);
            parts_flat_free(w->scroll_flats[i]);free(w->scroll_flats[i]);w->scroll_flats[i]=NULL;
        }
        if(w->scroll_cg_names[i])free_string(w->scroll_cg_names[i]);
        w->scroll_cg_names[i]=NULL;
    }
    w->scroll_art=false;
}

static struct string *scroll_name(struct string *prefix,int type,int role,int state,
        bool override,bool localized,bool flat)
{
    const char *role_jp[]={"／バー",type==2?"／上ボタン":"／左ボタン",
        type==2?"／下ボタン":"／右ボタン","／背景"};
    const char *role_local[]={"／栏",type==2?"／上按钮":"／左按钮",
        type==2?"／下按钮":"／右按钮","／背景"};
    const char *local_states[]={"／一般","／オン","／DOWN","／无效"};
    const char *jp_states[]={"／通常","／オン","／ダウン","／無効"};
    struct string *name=string_dup(prefix);
    if(!override || role==3) {
        /* A role override already denotes the role prefix, including background. */
        if(!override) {
            char *s=utf2sjis(localized?role_local[role]:role_jp[role],0);
            string_append_cstr(&name,s,strlen(s));free(s);
        }
    }
    if(role!=3) {
        /* Legacy CG skins use the established Japanese suffix spelling. */
        char *s=utf2sjis(flat && localized?local_states[state]:jp_states[state],0);
        string_append_cstr(&name,s,strlen(s));free(s);
    }
    return name;
}

bool parts_widget_set_scroll_skin(struct parts *p,struct string *flat,struct string *cg,
        struct string *flat_override[4],struct string *cg_override[4])
{
    struct parts_widget *w=p?p->widget:NULL;
    if(!w || !flat || !cg || (w->type!=2 && w->type!=3) || (flat->size && cg->size))return false;
    struct parts_widget next={.type=w->type};
    /* Callers may pass w's own strings (including its override entries).
     * Retain every proposed prefix before any old resources are released. */
    next.flat_name=string_dup(flat);next.cg_name=string_dup(cg);
    for(int role=0;role<4;role++) {
        struct string *fo=flat_override?flat_override[role]:w->scroll_flat_override[role];
        struct string *co=cg_override?cg_override[role]:w->scroll_cg_override[role];
        next.scroll_flat_override[role]=string_dup(fo?fo:&EMPTY_STRING);
        next.scroll_cg_override[role]=string_dup(co?co:&EMPTY_STRING);
        if(next.scroll_flat_override[role]->size && next.scroll_cg_override[role]->size)goto fail;
    }
    parts_textures_require_resident(p);
    for(int role=0;role<4;role++) {
        struct string *fo=next.scroll_flat_override[role],*co=next.scroll_cg_override[role];
        bool over=fo->size || co->size;
        /* A zero-size end button has no drawable role or inherited skin. */
        if(!over && ((role==1 && !w->previous_size) || (role==2 && !w->next_size)))continue;
        bool is_flat=over?fo->size!=0:next.flat_name->size!=0;
        struct string *prefix=over?(is_flat?fo:co):(is_flat?next.flat_name:next.cg_name);
        if(!prefix->size)continue;
        for(int state=0;state<(role==3?1:4);state++) {
            int slot=scroll_bases[role]+state;
            struct string *name=scroll_name(prefix,w->type,role,state,over,true,is_flat);
            struct cg *image=NULL;
            bool exists=is_flat?PE_ExistsFlatFile(name):(image=asset_cg_load_by_name(name->text,NULL))!=NULL;
            if(!exists) {
                free_string(name);name=scroll_name(prefix,w->type,role,state,over,false,is_flat);
                exists=is_flat?PE_ExistsFlatFile(name):(image=asset_cg_load_by_name(name->text,NULL))!=NULL;
            }
            if(!exists) {
                free_string(name);
                if(state==0)goto fail;
                continue; /* Optional visual state falls back to normal. */
            }
            if(is_flat) {
                struct parts_flat *f=next.scroll_flats[slot]=xcalloc(1,sizeof(*f));f->fps_rate=1.f;
                if(!parts_flat_load(p,f,name)){free_string(name);goto fail;}
                parts_flat_update(f,0);
            } else {
                gfx_init_texture_with_cg(&next.images[slot],image);cg_free(image);
                if(!next.images[slot].handle){free_string(name);goto fail;}
                next.scroll_cg_names[slot]=string_dup(name);
            }
            free_string(name);
        }
    }
    parts_widget_scroll_art_free(w);
    for(int i=0;i<20;i++) {
        gfx_delete_texture(&w->images[i]);w->images[i]=next.images[i];
        w->scroll_flats[i]=next.scroll_flats[i];w->scroll_cg_names[i]=next.scroll_cg_names[i];
    }
    free_string(w->flat_name);w->flat_name=next.flat_name;
    free_string(w->cg_name);w->cg_name=next.cg_name;
    for(int role=0;role<4;role++) {
        if(w->scroll_flat_override[role])free_string(w->scroll_flat_override[role]);
        if(w->scroll_cg_override[role])free_string(w->scroll_cg_override[role]);
        w->scroll_flat_override[role]=next.scroll_flat_override[role];
        w->scroll_cg_override[role]=next.scroll_cg_override[role];
    }
    w->flat_backed=false;w->scroll_art=w->flat_name->size || w->cg_name->size;
    for(int i=7;i<20;i++) if(w->scroll_flats[i] || w->images[i].handle)w->scroll_art=true;
    for(int role=0;role<4;role++)w->scroll_active[role]=-1;
    w->dirty=true;parts_component_dirty(p);parts_dirty(p);return true;
fail:
    parts_widget_scroll_art_free(&next);
    for(int i=0;i<20;i++)gfx_delete_texture(&next.images[i]);
    free_string(next.flat_name);free_string(next.cg_name);
    for(int role=0;role<4;role++) {
        if(next.scroll_flat_override[role])free_string(next.scroll_flat_override[role]);
        if(next.scroll_cg_override[role])free_string(next.scroll_cg_override[role]);
    }
    return false;
}

bool parts_widget_set_scroll_override(struct parts *p,bool flat,int role,struct string *name)
{
    struct parts_widget *w=p?p->widget:NULL;
    if(!w || !name || role<0 || role>=4 || (w->type!=2 && w->type!=3))return false;
    struct string *prefixes[4];
    for(int i=0;i<4;i++)prefixes[i]=flat?w->scroll_flat_override[i]:w->scroll_cg_override[i];
    prefixes[role]=name;
    return parts_widget_set_scroll_skin(p,w->flat_name,w->cg_name,flat?prefixes:NULL,flat?NULL:prefixes);
}

static int scroll_selected(struct parts_widget *w,int role)
{
    int base=scroll_bases[role];if(role==3)return base;
    int region=role==0?3:role;
    int visual=(!w->enabled || !scroll_max(w))?3:
        w->pressed_region==region?2:w->hover_region==region?1:0;
    int slot=base+visual;
    return w->scroll_flats[slot] || w->images[slot].handle?slot:base;
}

bool parts_widget_scroll_update(struct parts *p,int passed_time)
{
    struct parts_widget *w=p->widget;
    if(!w || !w->scroll_art || p->parked_textures)return false;
    bool changed=false;
    for(int role=0;role<4;role++) {
        int slot=scroll_selected(w,role);
        struct parts_flat *f=w->scroll_flats[slot];
        if(w->scroll_active[role]!=slot) {
            if(f){parts_flat_seek(f,0);parts_flat_update(f,0);}
            w->scroll_active[role]=slot;changed=true;
        } else if(f && parts_flat_update(f,passed_time))changed=true;
    }
    return changed;
}

void parts_widget_scroll_items(struct parts *p,struct widget_scroll_item items[4])
{
    struct parts_widget *w=p->widget;
    struct widget_scroll_geometry g=geometry(w);
    const int roles[]={3,1,2,0}; /* background, end buttons, then thumb */
    const int start[]={0,0,g.length-g.next,g.start};
    const int length[]={g.length,g.previous,g.next,g.thumb};
    for(int i=0;i<4;i++) {
        int slot=scroll_selected(w,roles[i]);
        items[i]=(struct widget_scroll_item){.flat=w->scroll_flats[slot],.texture=&w->images[slot],
            .rect=w->type==2?(Rectangle){0,start[i],w->width,length[i]}:
                (Rectangle){start[i],0,length[i],w->height}};
    }
}
