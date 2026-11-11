/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Explicit, resource-reading diagnostic. Never enters the game VM or writes
 * a progress save. Normal launches do not enter this function. */
#include "system4/utfsjis.h"
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "input.h"
#include "parts.h"
#include "parts_activity.h"
#include "parts/parts_internal.h"
#include "parts/textbox_internal.h"
#include "scene.h"
#include "vm/heap.h"
#include "vm/page.h"

static struct string *textbox_probe_string(const char *utf8)
{
    char *legacy=utf2sjis(utf8,0);
    struct string *s=cstr_to_string(legacy);free(legacy);return s;
}
static void textbox_probe_label(int no,int x,int y,int width,int height,const char *caption,bool clickable)
{
    struct parts *p=parts_get(no);parts_widget_create(p,0);
    struct parts_widget *w=p->widget;w->width=width;w->height=height;
    w->color=(SDL_Color){210,190,130,255};
    free_string(w->text);w->text=textbox_probe_string(caption);
    parts_widget_font_set(&w->font,0,24,20,20,20,0,0,0,0,0);
    PE_SetPos(no,x,y);PE_SetPartsOriginPosMode(no,1);PE_SetZ(no,1000);
    PE_SetClickable(no,clickable);w->dirty=true;
}
static int textbox_probe_run(void)
{
    if(!ain || ain->version!=12)return 1;
    gfx_init();gfx_font_init();heap_init();
    for(int i=1;i<=5;i++) {
        char name[32];if(i==1)snprintf(name,sizeof(name),"Rance10CG");
        else snprintf(name,sizeof(name),"Rance10CG%d",i);
        if(!asset_manager_load_archive(ASSET_CG,name))return 1;
    }
    for(int i=2;i<=3;i++) {
        char name[32];snprintf(name,sizeof(name),"Rance10Flat%d",i);
        if(!asset_manager_load_archive(ASSET_FLAT,name))return 1;
    }
    parts_multi_controller=true;if(!PE_Init())return 1;
    struct string *activity=cstr_to_string("textbox-native-probe");
    struct string *file=cstr_to_string("Game\\SaveLoad\\SceneSave.pactex");
    bool loaded=PE_ReadActivityFile(activity,file);free_string(activity);free_string(file);
    if(!loaded){PE_ReleaseAllParts();return 1;}
    struct parts *p,*actual=NULL;
    PARTS_LIST_FOREACH(p)if(p->widget && p->widget->type==PARTS_WIDGET_MULTITEXTBOX){actual=p;break;}
    if(!actual){PE_ReleaseAllParts();return 1;}
    int actual_no=actual->no;
    PE_SetPos(actual_no,250,500);PE_SetPartsOriginPosMode(actual_no,1);PE_SetZ(actual_no,1000);
    p=parts_get(700);parts_widget_create(p,PARTS_WIDGET_TEXTBOX);
    p->widget->width=700;p->widget->height=60;
    PE_SetPos(700,250,300);PE_SetPartsOriginPosMode(700,1);PE_SetZ(700,1000);
    textbox_probe_label(710,150,60,1550,80,"文本框自检：上方单行，下方为实际存档备注控件",false);
    textbox_probe_label(711,150,160,1550,70,"点击输入区测试输入、删除和换行；本页仅保存内存控件快照",false);
    textbox_probe_label(712,250,410,900,60,"下方沿用备注模板；游戏场景的额外字数限制另行验证",false);
    textbox_probe_label(701,150,900,350,70,"保存控件快照",true);
    textbox_probe_label(702,550,900,350,70,"恢复控件快照",true);
    textbox_probe_label(703,950,900,350,70,"关闭键盘",true);
    textbox_probe_label(704,1350,900,350,70,"退出自检",true);
    PE_UpdateComponent(0);PE_ReleaseMessage();SDL_StopTextInput();PE_BeginInput();
    NOTICE("[textbox-probe] ready single=700 multiline=%d limit=%zu",actual_no,actual->widget->textbox->buffer.limit);
    struct page *snapshot=NULL;unsigned previous=SDL_GetTicks(),started=previous;
    const char *seconds_env=getenv("XSYSTEM4_TEXTBOX_PROBE_SECONDS");
    unsigned seconds=seconds_env?max(1,min(300,atoi(seconds_env))):0;
    bool running=true;
    while(running) {
        handle_events();unsigned now=SDL_GetTicks();int elapsed=min(100,now-previous);previous=now;
        PE_UpdateComponent(elapsed);PE_Update(elapsed,true);
        while(PE_GetMessageType()!=-1) {
            int type=PE_GetMessageType(),no=PE_GetMessagePartsNumber();PE_PopMessage();
            if(type==22) {
                p=parts_try_get(no);
                if(p && p->widget && p->widget->textbox)
                    NOTICE("[textbox-probe] changed part=%d scalars=%zu",no,tb_count(p->widget->textbox->buffer.text,p->widget->textbox->buffer.len));
            }
            if(type!=4)continue;
            if(no==701) {
                bool ok=PE_Save(&snapshot);NOTICE("[textbox-probe] snapshot-save=%d",ok);
            } else if(no==702 && snapshot) {
                bool ok=PE_Load(&snapshot);PE_BeginInput();NOTICE("[textbox-probe] snapshot-load=%d",ok);break;
            } else if(no==703) {
                parts_textbox_open(parts_try_get(700),false);parts_textbox_open(parts_try_get(actual_no),false);
                NOTICE("[textbox-probe] keyboard-closed");
            } else if(no==704)running=false;
        }
        scene_render();gfx_swap();SDL_Delay(8);
        if(seconds && SDL_GetTicks()-started>=seconds*1000)running=false;
    }
    if(snapshot)free_page(snapshot);
    PE_ReleaseMessage();PE_ReleaseAllParts();
    NOTICE("[textbox-probe] finished");return 0;
}
