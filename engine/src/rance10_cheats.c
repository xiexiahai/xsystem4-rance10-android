/* SPDX-License-Identifier: GPL-2.0-or-later
 * LOCAL GAME PROFILE: function identities/member positions were checked against
 * the user's AIN. Do not send this file to DSH. No script bytes are embedded.
 * UI exchanges atomics only; all page access remains on the VM thread. */
#define VM_PRIVATE
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "system4/ain.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "rance10_cheats.h"

enum role { NONE, HP_SET, HP_INITIALIZE, HP_FORCE_RESULT, AP_CALC, AP_COST, TREASURE_PER, RESULT_CALC,
    FOOD_SET, FOOD_USE, ZIPPO_USED, ZIPPO_GET, ZIPPO_SET, ROLE_COUNT };
static const struct signature { const char *name; int crc, args; enum role role; } signatures[] = {
    {"BattlePlayer@SetHp",1263031384,1,HP_SET},
    {"BattleContext@InitHpPer",-1986335791,0,HP_INITIALIZE},
    {"SceneSimpleBattleResult@Run",-25399812,0,HP_FORCE_RESULT},
    {"SkillApCostCalculator::Calc",-1165157875,2,AP_CALC},
    {"SkillApCostCalculator::Calc",1371901306,2,AP_CALC},
    {"SkillApCost@Cost::get",1212459943,0,AP_COST},
    {"BattleResult@GetTotalPerTreasure",-775023408,0,TREASURE_PER},
    {"SceneBattleResult@CalcResult",-2117286863,0,RESULT_CALC},
    {"PlayerCommonParam@FoodTicket::set",1011278225,1,FOOD_SET},
    {"SceneFoodTicket@OnClickCard",2144810098,3,FOOD_USE},
    {"PlayerCommonParam@IsUsedHannyZippo::get",1094770795,0,ZIPPO_USED},
    {"BattleContext@HannyZippoCount::get",1212459943,0,ZIPPO_GET},
    {"BattleContext@HannyZippoCount::set",55320089,1,ZIPPO_SET},
};
static atomic_uint flags, capabilities;
static unsigned char *roles;
static int nr_roles;
static int player_type, party_type, cost_type, common_type, context_type, switch_type, bonus_type;
static int party_global, bonus_global;

static bool member_is(int type,int index,const char *name,int data)
{
    return type>=0 && type<ain->nr_structures && index>=0 &&
        index<ain->structures[type].nr_members &&
        !strcmp(ain->structures[type].members[index].name,name) &&
        (int)ain->structures[type].members[index].type.data==data;
}
static struct page *page_is(int slot,enum page_type kind,int type,int minimum)
{
    if(!page_index_valid(slot))return NULL;
    struct page *p=heap[slot].page;
    return p && p->type==kind && (type<0 || p->index==type) && p->nr_vars>=minimum ? p:NULL;
}
static struct page *global_struct(int global,int type,int minimum)
{
    struct page *g=global>=0?page_is(0,GLOBAL_PAGE,-1,global+1):NULL;
    return g?page_is(g->values[global].i,STRUCT_PAGE,type,minimum):NULL;
}
static enum role role_at(int frame)
{
    if(frame<0 || frame>=call_stack_ptr || !roles)return NONE;
    int f=call_stack[frame].fno;
    return f>=0 && f<nr_roles ? roles[f]:NONE;
}
static bool hp_is_scripted_reset(void)
{
    for(int i=call_stack_ptr-2;i>=0;i--) {
        enum role r=role_at(i);
        if(r==HP_INITIALIZE || r==HP_FORCE_RESULT)return true;
    }
    return false;
}
static bool owns_zippo(void)
{
    struct page *s=global_struct(bonus_global,switch_type,2);
    struct page *a=s?page_is(s->values[1].i,ARRAY_PAGE,-1,0):NULL;
    if(!a || a->array.rank!=1 || a->array.struct_type!=bonus_type || a->nr_vars>1024)return false;
    for(int i=0;i<a->nr_vars;i++) {
        struct page *b=page_is(a->values[i].i,STRUCT_PAGE,bonus_type,4);
        /* The game's IsEnable predicate is Type==7 && IsUse. */
        if(b && b->values[1].i==7 && b->values[3].i)return true;
    }
    return false;
}
void r10mod_init(void)
{
    atomic_store(&flags,0);atomic_store(&capabilities,0);
    free(roles);roles=NULL;nr_roles=0;
    if(!ain || ain->version!=12 || ain->nr_functions<=0 || ain->nr_functions>100000)return;
    player_type=ain_get_struct(ain,"BattlePlayer");party_type=ain_get_struct(ain,"Party");
    cost_type=ain_get_struct(ain,"SkillApCost");common_type=ain_get_struct(ain,"PlayerCommonParam");
    context_type=ain_get_struct(ain,"BattleContext");switch_type=ain_get_struct(ain,"PartyBonusSwitcher");
    bonus_type=ain_get_struct(ain,"PartyBonus");
    party_global=ain_get_global(ain,"g_party");bonus_global=ain_get_global(ain,"g_partyBonus");
    if(party_global<0 || bonus_global<0 ||
       !member_is(player_type,0,"m_side",AIN_INT) ||
       !member_is(party_type,4,"m_hp",AIN_INT) ||
       !member_is(cost_type,0,"m_cost",AIN_INT) ||
       !member_is(common_type,0,"m_foodTicket",AIN_INT) ||
       !member_is(context_type,1,"m_hannyZippoCount",AIN_INT) ||
       !member_is(switch_type,1,"m_bonus",AIN_ARRAY) ||
       !member_is(bonus_type,1,"<Type>",AIN_ENUM) ||
       !member_is(bonus_type,3,"<IsUse>",AIN_BOOL))return;
    roles=calloc((size_t)ain->nr_functions,1);if(!roles)return;
    nr_roles=ain->nr_functions;
    for(unsigned s=0;s<sizeof(signatures)/sizeof(*signatures);s++) {
        bool found=false;
        for(int f=0;f<nr_roles;f++) {
            const struct ain_function *fn=&ain->functions[f];
            if(!strcmp(fn->name,signatures[s].name) && fn->crc==signatures[s].crc && fn->nr_args==signatures[s].args) {
                roles[f]=signatures[s].role;found=true;
            }
        }
        if(!found){free(roles);roles=NULL;nr_roles=0;return;}
    }
    atomic_store(&capabilities,15);
}
void r10mod_set_flags(unsigned value) {atomic_store(&flags,value&atomic_load(&capabilities)&15);}
unsigned r10mod_get_flags(void) {return atomic_load(&flags);}
unsigned r10mod_get_capabilities(void) {return atomic_load(&capabilities);}

void r10mod_before_method(void)
{
    unsigned enabled=atomic_load(&flags);
    if(!enabled || call_stack_ptr<=0)return;
    struct function_call *f=&call_stack[call_stack_ptr-1];
    enum role role=role_at(call_stack_ptr-1);
    struct page *local=page_is(f->page_slot,LOCAL_PAGE,f->fno,1);
    if(!local)return;
    if((enabled&R10MOD_HP) && role==HP_SET && !hp_is_scripted_reset()) {
        struct page *bp=page_is(f->struct_page,STRUCT_PAGE,player_type,1);
        struct page *party=global_struct(party_global,party_type,5);
        if(bp && bp->values[0].i==0 && party && party->values[4].i>=0 && local->values[0].i<party->values[4].i)
            local->values[0].i=party->values[4].i;
    }
    if((enabled&R10MOD_ITEMS) && role==FOOD_SET && role_at(call_stack_ptr-2)==FOOD_USE) {
        struct page *p=page_is(f->struct_page,STRUCT_PAGE,common_type,1);
        if(p && p->values[0].i>0 && local->values[0].i==p->values[0].i-1)
            local->values[0].i=p->values[0].i;
    }
    if((enabled&R10MOD_ITEMS) && role==ZIPPO_SET && owns_zippo()) {
        struct page *p=page_is(f->struct_page,STRUCT_PAGE,context_type,2);
        if(p && p->values[1].i>0 && local->values[0].i<p->values[1].i)
            local->values[0].i=p->values[1].i;
    }
}
void r10mod_return(void)
{
    unsigned enabled=atomic_load(&flags);
    if(!enabled || call_stack_ptr<=0 || stack_ptr<=0)return;
    struct function_call *f=&call_stack[call_stack_ptr-1];
    enum role role=role_at(call_stack_ptr-1);
    if((enabled&R10MOD_AP) && role==AP_COST && page_is(f->struct_page,STRUCT_PAGE,cost_type,2))
        stack_set(0,0); /* Virtual cost: do not overwrite a cached cost object. */
    if((enabled&R10MOD_TREASURE) && role==TREASURE_PER && role_at(call_stack_ptr-2)==RESULT_CALC)
        stack_set(0,100); /* Called after the original forced-no-treasure branch and RNG. */
    if((enabled&R10MOD_ITEMS) && owns_zippo()) {
        if(role==ZIPPO_USED && page_is(f->struct_page,STRUCT_PAGE,common_type,12))stack_set(0,false);
        if(role==ZIPPO_GET && page_is(f->struct_page,STRUCT_PAGE,context_type,2) && stack_peek(0).i==0)
            stack_set(0,1); /* Restore availability when enabling after earlier use. */
    }
}
#ifdef __ANDROID__
#include <jni.h>
JNIEXPORT void JNICALL Java_io_github_kichikuou_xsystem4_Rance10CheatBridge_nativeSetFlags(JNIEnv *e,jobject o,jint v)
{(void)e;(void)o;r10mod_set_flags((unsigned)v);}
JNIEXPORT jint JNICALL Java_io_github_kichikuou_xsystem4_Rance10CheatBridge_nativeGetFlags(JNIEnv *e,jobject o)
{(void)e;(void)o;return (jint)r10mod_get_flags();}
JNIEXPORT jint JNICALL Java_io_github_kichikuou_xsystem4_Rance10CheatBridge_nativeGetCapabilities(JNIEnv *e,jobject o)
{(void)e;(void)o;return (jint)r10mod_get_capabilities();}
#endif
