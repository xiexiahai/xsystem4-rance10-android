/* SPDX-License-Identifier: GPL-2.0-or-later */
/* New typed widget payloads only: the pre-existing widget byte stream is unchanged. */
void parts_widget_spin_refresh(struct parts *p)
{
    struct parts_widget *w=p->widget;char text[96];
    snprintf(text,sizeof(text),"%.*f",w->spin.decimals,(double)w->spin.value);
    tb_set(&w->textbox->buffer,text);w->textbox->editing[0]=0;
    free_string(w->text);w->text=cstr_to_string(text);
    parts_textbox_dirty(p);
}
bool parts_widget_spin_value(struct parts *p,float value)
{
    if(!p || !p->widget || p->widget->type!=PARTS_WIDGET_SPINBOX || !isfinite(value))return false;
    struct parts_widget *w=p->widget;
    value=max(w->spin.min,min(w->spin.max,value));bool changed=value!=w->spin.value;
    w->spin.value=value;parts_widget_spin_refresh(p);
    if(changed && !p->is_back_scene)parts_msg_push(p,PARTS_MSG_CHANGED_TEXT,"");
    return true;
}
void parts_widget_spin_select_all(struct parts *p)
{
    tb_select_all(&p->widget->textbox->buffer);parts_textbox_dirty(p);
}
static bool parts_widget_spin_mouse(struct parts *p,Point cursor,bool down,bool pressed)
{
    struct parts_widget *w=p->widget;
    if(w->textbox->readonly)return false;
    float sx=p->global.scale.x,sy=p->global.scale.y;
    if(fabsf(sx)<0.0001f || fabsf(sy)<0.0001f)return false;
    float a=-p->local.rotation.z*0.017453292519943295f,dx=cursor.x-p->global.pos.x,dy=cursor.y-p->global.pos.y;
    Point origin=p->states[0].common.origin_offset;
    int x=(int)lroundf((cosf(a)*dx-sinf(a)*dy)/sx)-origin.x;
    int y=(int)lroundf((sinf(a)*dx+cosf(a)*dy)/sy)-origin.y;
    bool arrow=x>=w->width-15 && x<w->width && y>=0 && y<w->height;
    if(pressed && arrow) {
        w->pressed_region=y<w->height/2?4:5;w->repeat_at=SDL_GetTicks()+350;
        double next=(double)w->spin.value+(w->pressed_region==4?1.:-1.)*w->spin.increment;
        parts_widget_spin_value(p,(float)max(w->spin.min,min(w->spin.max,next)));
        return true;
    }
    if(w->pressed_region==4 || w->pressed_region==5) {
        if(down && SDL_TICKS_PASSED(SDL_GetTicks(),w->repeat_at)) {
            double next=(double)w->spin.value+(w->pressed_region==4?1.:-1.)*w->spin.increment;
            parts_widget_spin_value(p,(float)max(w->spin.min,min(w->spin.max,next)));
            w->repeat_at=SDL_GetTicks()+75;
        }
        if(!down)w->pressed_region=-1;
        return true;
    }
    return false;
}
static void parts_widget_spin_arrows(Texture *canvas,struct parts_widget *w)
{
    if(w->width<18 || w->height<8)return;
    int x=w->width-15,h=w->height/2;
    gfx_fill(canvas,x,1,14,w->height-2,210,210,210);
    gfx_fill(canvas,x,h,14,1,100,100,100);
    for(int n=0;n<3;n++) {
        gfx_fill(canvas,x+6-n,max(2,h/2-1)+n,1+2*n,1,60,60,60);
        gfx_fill(canvas,x+4+n,h+max(1,(w->height-h)/2-1)+n,5-2*n,1,60,60,60);
    }
}
static void parts_widget_spin_form_save(struct iarray_writer *out,struct parts_widget *w)
{
    if(w->type==PARTS_WIDGET_SPINBOX) {
        iarray_write_float(out,w->spin.value);iarray_write_float(out,w->spin.def);
        iarray_write_float(out,w->spin.min);iarray_write_float(out,w->spin.max);
        iarray_write_float(out,w->spin.increment);iarray_write(out,w->spin.decimals);
    } else if(w->type==PARTS_WIDGET_FORM) {
        int v[]={w->form.min_w,w->form.min_h,w->form.max_w,w->form.max_h,w->form.caption_plus,
            w->form.caption.r,w->form.caption.g,w->form.caption.b};
        for(unsigned i=0;i<sizeof(v)/sizeof(*v);i++)iarray_write(out,v[i]);
        iarray_write(out,w->form.close_enabled);iarray_write(out,w->form.close_phase);
    }
}
static bool parts_widget_spin_form_load(struct iarray_reader *in,struct parts_widget *w,int version)
{
    if(w->type==PARTS_WIDGET_SPINBOX) {
        w->spin.value=iarray_read_float(in);w->spin.def=iarray_read_float(in);
        w->spin.min=iarray_read_float(in);w->spin.max=iarray_read_float(in);
        w->spin.increment=iarray_read_float(in);w->spin.decimals=iarray_read(in);
        if(!isfinite(w->spin.value)||!isfinite(w->spin.def)||!isfinite(w->spin.min)||!isfinite(w->spin.max)||
            !isfinite(w->spin.increment)||w->spin.increment<0||w->spin.min>w->spin.max||
            w->spin.value<w->spin.min||w->spin.value>w->spin.max||w->spin.decimals<0||w->spin.decimals>9)in->error=1;
    } else if(w->type==PARTS_WIDGET_FORM) {
        int v[8];for(int i=0;i<8;i++)v[i]=iarray_read(in);
        for(int i=0;i<4;i++)if(v[i]<1||v[i]>4096)in->error=1;
        if(v[0]>v[2]||v[1]>v[3]||v[4]<0||v[4]>512)in->error=1;
        for(int i=5;i<8;i++)if(v[i]<0||v[i]>255)in->error=1;
        w->form.min_w=v[0];w->form.min_h=v[1];w->form.max_w=v[2];w->form.max_h=v[3];
        w->form.caption_plus=v[4];w->form.caption=(SDL_Color){v[5],v[6],v[7],255};
        if(version>=28) {
            int enabled=iarray_read(in),phase=iarray_read(in);
            if(enabled<0 || enabled>1 || phase<0 || phase>2)in->error=1;
            w->form.close_enabled=enabled;w->form.close_phase=phase;
        }
    }
    return !in->error;
}
