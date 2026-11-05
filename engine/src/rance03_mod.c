/* Optional offline Rance03 MOD. Never linked into normal builds.
 * UI threads only exchange atomics; all VM access stays on the VM thread. */
#define VM_PRIVATE
#include <limits.h>
#include <stdatomic.h>
#include <string.h>
#include "system4/ain.h"
#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "rance03_mod.h"
#ifdef __ANDROID__
#include <android/log.h>
#define MOD_LOG(...) __android_log_print(ANDROID_LOG_INFO,"Rance03MOD",__VA_ARGS__)
#else
#define MOD_LOG(...) ((void)0)
#endif

static atomic_uint flags, state, gold_requests, exp_requests;
static bool compatible, applying;
static uint32_t last_poll;
static int player_type, common_type, result_type, item_type, count_type;
static int players_global, common_global, items_global;
enum { HP, SET_CD, OVER_CD, GET_CD, PARTY_CD, GET_PARTY_CD,
       SET_EXP, ADD_EXP, SET_GOLD, ADD_GOLD, USE_ITEM, DEC_ITEM, CAMP_LION, DROP, HOOK_COUNT };
static int functions[HOOK_COUNT];
static const struct { const char *name; int32_t crc; int args; } signatures[] = {
    {"Player@setHp", -1617599902, 1},
    {"Player@setCoolTime", -729587211, 1},
    {"Player@setOverCoolTime", -1464921706, 2},
    {"Player@getCoolTime", -1471100255, 1},
    {"PlayerCommonParam@setPartyCoolTime", 1629503482, 1},
    {"PlayerCommonParam@getPartyCoolTime", 1212459943, 0},
    {"Player@setExp", -534610734, 1},
    {"Player@addExp", -2065683727, 1},
    {"PlayerCommonParam@setGold", 1119748456, 1},
    {"PlayerCommonParam@addGold", 549543388, 1},
    {"PlayerCommonParam@useItem", -310161395, 1},
    {"PlayerCommonParam@decItem", -1220549445, 1},
    {"SceneCamp@onClickSaveLion", -36293420, 1},
    {"BattleResult@isGetItemByLot", -181551202, 0},
};

static struct page *checked_page(int slot, enum page_type kind, int type, int minimum)
{
    if (!page_index_valid(slot)) return NULL;
    struct page *p = heap[slot].page;
    if (!p || p->type != kind || p->nr_vars < minimum || (type >= 0 && p->index != type)) return NULL;
    return p;
}

static struct page *struct_page(int slot, int type, int minimum)
{ return checked_page(slot, STRUCT_PAGE, type, minimum); }

static struct page *array_page(int slot, enum ain_data_type type, int structure)
{
    struct page *p = checked_page(slot, ARRAY_PAGE, type, 0);
    return p && p->array.rank == 1 && p->nr_vars <= 4096 &&
        (structure < 0 || p->array.struct_type == structure) ? p : NULL;
}

static struct page *global_page(int index)
{
    struct page *p = checked_page(0, GLOBAL_PAGE, -1, index + 1);
    return p && index >= 0 ? p : NULL;
}

static int global_slot(int index)
{
    struct page *g = global_page(index);
    return g ? g->values[index].i : -1;
}

static bool member(int type, int index, const char *name, enum ain_data_type data)
{
    if (type < 0 || type >= ain->nr_structures) return false;
    struct ain_struct *s = &ain->structures[type];
    return index < s->nr_members && !strcmp(s->members[index].name, name) && s->members[index].type.data == data;
}

void r03mod_init(void)
{
    atomic_store(&flags, 0); atomic_store(&state, 0);
    atomic_store(&gold_requests, 0); atomic_store(&exp_requests, 0);
    compatible = false; applying = false; last_poll = 0;
    if (!ain) return;
    player_type=ain_get_struct(ain,"Player"); common_type=ain_get_struct(ain,"PlayerCommonParam");
    result_type=ain_get_struct(ain,"BattleResult"); item_type=ain_get_struct(ain,"Item");
    count_type=ain_get_struct(ain,"ItemCount");
    players_global=ain_get_global(ain,"g_player"); common_global=ain_get_global(ain,"g_playerCommonParam");
    items_global=ain_get_global(ain,"g_item");
    if (players_global<0 || common_global<0 || items_global<0 ||
        !member(player_type,10,"hp_",AIN_INT) || !member(player_type,11,"maxHp_",AIN_INT) ||
        !member(player_type,14,"exp_",AIN_INT) || !member(player_type,24,"isJoin_",AIN_BOOL) ||
        !member(player_type,27,"coolTime_",AIN_ARRAY_INT) ||
        !member(common_type,0,"gold_",AIN_INT) || !member(common_type,2,"itemCount_",AIN_ARRAY_STRUCT) ||
        !member(common_type,6,"partyCoolTime_",AIN_INT) || !member(item_type,0,"id_",AIN_STRING) ||
        !member(count_type,1,"count",AIN_INT) || !member(result_type,5,"itemId_",AIN_STRING)) return;
    for (unsigned i=0;i<HOOK_COUNT;i++) {
        functions[i]=-1;
        for(int f=0;f<ain->nr_functions;f++) if(!strcmp(ain->functions[f].name,signatures[i].name)) {
            if(ain->functions[f].nr_args != signatures[i].args || ain->functions[f].crc != signatures[i].crc) return;
            functions[i]=f; break;
        }
        if(functions[i]<0) return;
    }
    compatible=true; atomic_store(&state, 0x20000); MOD_LOG("schema and function CRCs accepted; all toggles off");
}

static struct function_call *frame(void)
{ return call_stack_ptr>0 ? &call_stack[call_stack_ptr-1] : NULL; }

static int32_t clamp_sum(int32_t old, int32_t delta)
{
    int64_t sum=(int64_t)old+delta;
    return sum<0 ? 0 : sum>INT_MAX ? INT_MAX : (int32_t)sum;
}

void r03mod_before_method(void)
{
    if(!compatible) return;
    struct function_call *f=frame(); if(!f) return;
    struct page *local=checked_page(f->page_slot,LOCAL_PAGE,f->fno,1);
    if(!local) return;
    struct page *p=struct_page(f->struct_page,player_type,31);
    unsigned enabled=atomic_load(&flags);
    if(p && f->fno==functions[HP] && (enabled&R03MOD_HP) && local->values[0].i<p->values[10].i) {
        MOD_LOG("HP guard %d -> %d",local->values[0].i,p->values[10].i);
        local->values[0].i=p->values[10].i;
    }
    // MOD can place EXP/gold at INT32_MAX. Preserve future ordinary rewards
    // without letting the original signed addition wrap negative.
    if(p && f->fno==functions[ADD_EXP] && p->values[14].i>=0)
        local->values[0].i=clamp_sum(p->values[14].i,local->values[0].i)-p->values[14].i;
    struct page *c=struct_page(f->struct_page,common_type,16);
    if(c && f->fno==functions[ADD_GOLD] && c->values[0].i>=0)
        local->values[0].i=clamp_sum(c->values[0].i,local->values[0].i)-c->values[0].i;
}

static void clear_cooldowns(struct page *p)
{
    struct page *a=array_page(p->values[27].i,AIN_ARRAY_INT,-1);
    if(a) for(int i=0;i<a->nr_vars;i++) a->values[i].i=0;
}

static bool valid_drop(struct page *result)
{
    int id=result->values[5].i;
    if(!string_index_valid(id) || !heap[id].s->size) return false;
    struct page *items=array_page(global_slot(items_global),AIN_ARRAY_STRUCT,item_type);
    if(!items) return false;
    for(int i=0;i<items->nr_vars;i++) {
        struct page *p=struct_page(items->values[i].i,item_type,12);
        if(p && string_index_valid(p->values[0].i) && !strcmp(heap[p->values[0].i].s->text,heap[id].s->text)) return true;
    }
    return false;
}

void r03mod_return(void)
{
    if(!compatible) return;
    struct function_call *f=frame(); if(!f) return;
    unsigned enabled=atomic_load(&flags);
    if(enabled&R03MOD_COOLDOWN) {
        struct page *p=struct_page(f->struct_page,player_type,31);
        if(p && (f->fno==functions[SET_CD] || f->fno==functions[OVER_CD])) { clear_cooldowns(p); MOD_LOG("player cooldown cleared"); }
        if(p && f->fno==functions[GET_CD] && stack_ptr>0) stack_set(0,0);
        struct page *c=struct_page(f->struct_page,common_type,16);
        if(c && f->fno==functions[PARTY_CD]) c->values[6].i=0;
        if(c && f->fno==functions[GET_PARTY_CD] && stack_ptr>0) stack_set(0,0);
    }
    if((enabled&R03MOD_DROP) && f->fno==functions[DROP] && stack_ptr>0) {
        struct page *r=struct_page(f->struct_page,result_type,10);
        if(r && valid_drop(r)) { stack_set(0,true); MOD_LOG("valid existing loot guaranteed"); }
    }
}

bool r03mod_keep_item(union vm_value *value)
{
    if(!compatible || !(atomic_load(&flags)&R03MOD_ITEMS)) return false;
    struct function_call *f=frame();
    if(!f) return false;
    bool normal_use=f->fno==functions[USE_ITEM];
    bool quick_lion=f->fno==functions[DEC_ITEM] && call_stack_ptr>=2 &&
        call_stack[call_stack_ptr-2].fno==functions[CAMP_LION];
    // The camp shortcut consumes a lion directly. Other decItem callers
    // enforce inventory limits or remove story items and must remain normal.
    if(!normal_use && !quick_lion) return false;
    struct page *c=struct_page(f->struct_page,common_type,16);
    struct page *local=checked_page(f->page_slot,LOCAL_PAGE,f->fno,3);
    struct page *counts=c ? array_page(c->values[2].i,AIN_ARRAY_STRUCT,count_type) : NULL;
    int index=local ? local->values[1].i : -1;
    if(!counts || index<0 || index>=counts->nr_vars) return false;
    struct page *item=struct_page(counts->values[index].i,count_type,2);
    bool keep=item && value==&item->values[1] && value->i>0;
    if(keep) MOD_LOG("item use preserved count=%d",value->i);
    return keep;
}

void r03mod_idle(void)
{
    if(!compatible || applying) return;
    struct page *players=array_page(global_slot(players_global),AIN_ARRAY_STRUCT,player_type);
    int common_slot=global_slot(common_global);
    struct page *common=struct_page(common_slot,common_type,16);
    bool joined=false;
    if(players) for(int i=0;i<players->nr_vars;i++) {
        struct page *p=struct_page(players->values[i].i,player_type,31);
        if(p && p->values[24].i) { joined=true; break; }
    }
    unsigned next_state=0x20000 | (joined&&common ? 0x10000 : 0);
    if(atomic_exchange(&state,next_state)!=next_state)
        MOD_LOG("game ready=%d players=%d",joined&&common,players ? players->nr_vars : 0);
    if(!joined || !common) return;
    unsigned enabled=atomic_load(&flags);
    if(enabled&R03MOD_COOLDOWN) {
        for(int i=0;i<players->nr_vars;i++) {
            struct page *p=struct_page(players->values[i].i,player_type,31);
            if(p) clear_cooldowns(p);
        }
        common->values[6].i=0;
    }
    unsigned gold=atomic_exchange(&gold_requests,0), exp=atomic_exchange(&exp_requests,0);
    if(!gold && !exp) return;
    applying=true;
    if(gold) {
        int64_t sum=(int64_t)common->values[0].i+(int64_t)gold*10000;
        r03mod_call_setter(functions[SET_GOLD],common_slot,sum>INT_MAX ? INT_MAX : (int32_t)sum);
        MOD_LOG("gold requests applied count=%u",gold);
    }
    for(int i=0;exp;i++) {
        // A script callback may replace global pages: resolve again after
        // each script invocation rather than keeping borrowed page pointers.
        players=array_page(global_slot(players_global),AIN_ARRAY_STRUCT,player_type);
        if(!players || i>=players->nr_vars) break;
        struct page *p=struct_page(players->values[i].i,player_type,31);
        if(p && p->values[24].i) { r03mod_call_setter(functions[SET_EXP],players->values[i].i,INT_MAX); MOD_LOG("joined player EXP set to INT32_MAX"); }
    }
    applying=false;
}

void r03mod_set_flags(unsigned value) { atomic_store(&flags,value&15); }
void r03mod_poll(void)
{
    if(!compatible || applying) return;
    uint32_t now=(uint32_t)vm_time();
    if(now-last_poll<100 && !atomic_load(&gold_requests) && !atomic_load(&exp_requests)) return;
    last_poll=now;
    r03mod_idle();
}
unsigned r03mod_get_state(void) { return atomic_load(&state)|atomic_load(&flags); }
bool r03mod_action(int action)
{
    if(!(atomic_load(&state)&0x10000)) return false;
    if(action==1) { atomic_store(&exp_requests,1); return true; }
    if(action==2) {
        unsigned count=atomic_load(&gold_requests);
        while(count<10000) if(atomic_compare_exchange_weak(&gold_requests,&count,count+1)) return true;
    }
    return false;
}

#ifdef __ANDROID__
#include <jni.h>
JNIEXPORT void JNICALL Java_io_github_kichikuou_xsystem4_GameVariant_nativeSetFlags(JNIEnv *e,jobject o,jint value)
{ r03mod_set_flags(value); }
JNIEXPORT jint JNICALL Java_io_github_kichikuou_xsystem4_GameVariant_nativeGetState(JNIEnv *e,jobject o)
{ return r03mod_get_state(); }
JNIEXPORT jboolean JNICALL Java_io_github_kichikuou_xsystem4_GameVariant_nativeAction(JNIEnv *e,jobject o,jint action)
{ return r03mod_action(action); }
#endif
