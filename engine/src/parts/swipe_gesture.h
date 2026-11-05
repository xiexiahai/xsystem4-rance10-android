/* SPDX-License-Identifier: GPL-2.0-or-later
 * Cursor-independent swipe events. Script listeners own content movement. */
#ifndef PARTS_SWIPE_GESTURE_H
#define PARTS_SWIPE_GESTURE_H
#include <stdbool.h>
#include <math.h>
#include <string.h>
enum parts_swipe_event {PARTS_SWIPE_BEGIN,PARTS_SWIPE_UPDATE,PARTS_SWIPE_END};
typedef void (*parts_swipe_emit)(void*,enum parts_swipe_event,int,int,int,int,bool);
struct parts_swipe_gesture {
    int type;bool down,active,inertia;
    double start_x,start_y,last_x,last_y,x,y,vx,vy;
    unsigned elapsed;
};
static void parts_swipe_reset(struct parts_swipe_gesture *g){memset(g,0,sizeof(*g));}
static void parts_swipe_begin(struct parts_swipe_gesture *g,int type,int x,int y)
{
    parts_swipe_reset(g);if(type<1 || type>3)return;
    g->type=type;g->down=true;g->start_x=g->last_x=g->x=x;g->start_y=g->last_y=g->y=y;
}
static void parts_swipe_send(struct parts_swipe_gesture *g,enum parts_swipe_event event,bool inertia,parts_swipe_emit emit,void *data)
{if(emit)emit(data,event,(int)g->start_x,(int)g->start_y,(int)lround(g->x),(int)lround(g->y),inertia);}
static void parts_swipe_stop(struct parts_swipe_gesture *g,parts_swipe_emit emit,void *data)
{if(g->active)parts_swipe_send(g,PARTS_SWIPE_END,false,emit,data);parts_swipe_reset(g);}
static double parts_swipe_limit(double value)
{return fmax(-1000000.,fmin(1000000.,value));}
static void parts_swipe_update(struct parts_swipe_gesture *g,int x,int y,bool down,int passed,parts_swipe_emit emit,void *data)
{
    if(!g->type)return;
    if(passed>250){parts_swipe_stop(g,emit,data);return;}
    double dt=passed>0?passed:1;
    if(g->inertia) {
        g->elapsed+=(unsigned)dt;g->x=parts_swipe_limit(g->x+g->vx*dt);g->y=parts_swipe_limit(g->y+g->vy*dt);
        double decay=pow(.90,dt/16.);g->vx*=decay;g->vy*=decay;
        parts_swipe_send(g,PARTS_SWIPE_UPDATE,true,emit,data);
        if(g->elapsed>=700 || fabs(g->vx)+fabs(g->vy)<.02)parts_swipe_stop(g,emit,data);
        return;
    }
    if(!g->down)return;
    g->x=g->type==2?g->start_x:parts_swipe_limit(x);
    g->y=g->type==3?g->start_y:parts_swipe_limit(y);
    double dx=g->x-g->start_x,dy=g->y-g->start_y;
    if(!g->active && dx*dx+dy*dy>=64) {
        g->active=true;parts_swipe_send(g,PARTS_SWIPE_BEGIN,false,emit,data);
    }
    g->vx=fmax(-8.,fmin(8.,.5*g->vx+.5*(g->x-g->last_x)/dt));
    g->vy=fmax(-8.,fmin(8.,.5*g->vy+.5*(g->y-g->last_y)/dt));
    if(g->active && (g->x!=g->last_x || g->y!=g->last_y))parts_swipe_send(g,PARTS_SWIPE_UPDATE,false,emit,data);
    g->last_x=g->x;g->last_y=g->y;
    if(down)return;
    g->down=false;
    if(!g->active){parts_swipe_reset(g);return;}
    if(fabs(g->vx)+fabs(g->vy)>=.02){g->inertia=true;g->elapsed=0;}
    else parts_swipe_stop(g,emit,data);
}
#endif
