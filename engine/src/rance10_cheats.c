/* SPDX-License-Identifier: GPL-2.0-or-later
 * LOCAL GAME PROFILE: function identities/member positions were checked against
 * the user's AIN. Do not send this file to DSH. No script bytes are embedded.
 * UI exchanges atomics only; all page access remains on the VM thread. */
#define VM_PRIVATE
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "system4/ain.h"
#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "rance10_cheats.h"

enum role { NONE, HP_SET, HP_INITIALIZE, HP_FORCE_RESULT, AP_CALC, AP_COST, TREASURE_PER, RESULT_CALC,
    FOOD_SET, FOOD_USE, ZIPPO_USED, ZIPPO_GET, ZIPPO_SET, WAIT_INPUT, JOIN_SCENE,
    FOOD_SCENE, MAP_FOOD_SCENE, SELECTOR_FADE, SELECTOR_SHOW, SELECTOR_ENABLE,
    BATTLE_INFO, BATTLE_REPICK, BUTTON_ENABLE, TREASURE_CARDS };
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
static atomic_uint requested_flags, capabilities;
enum { FOOD_UNAVAILABLE, FOOD_READY, FOOD_REQUESTED };
static atomic_uint food_request;
static unsigned char *roles;
static int nr_roles;
static int player_type, party_type, cost_type, common_type, context_type, switch_type, bonus_type;
static int party_global, bonus_global;
static int view_update, food_type, map_food_type, selector_type, assistant_global, assistant_type, message_type;
static int find_food, find_map_food, set_cards, show_cards, enable_selector, get_message_flags, set_message_cards;
static int button_type, enable_button;
static int get_card_scene_type, get_card_info_type;
static const unsigned toggle_mask = R10MOD_HP | R10MOD_AP | R10MOD_TREASURE | R10MOD_ITEMS |
    R10MOD_SIMPLE_REPICK | R10MOD_TREASURE_MULTI;
struct object_id { int slot; uint32_t seq; };
static struct object_id food_selector, repick_button;
static uint32_t food_shown_at;
static bool food_enabled, repick_enabled;

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

static int find_function(const char *name, int crc, int args)
{
    for (int i=0; i<nr_roles; i++) {
        const struct ain_function *fn=&ain->functions[i];
        if (!strcmp(fn->name,name) && fn->crc==crc && fn->nr_args==args)
            return i;
    }
    return -1;
}

static unsigned bind_treasure_multiselect(void)
{
    int run = find_function("SceneGetCard@Run", 1996792592, 0);
    get_card_scene_type = ain_get_struct(ain, "SceneGetCard");
    get_card_info_type = ain_get_struct(ain, "GetCardInformation");
    if (run < 0 ||
        !member_is(get_card_scene_type, 4, "m_cardInfo", AIN_ARRAY) ||
        !member_is(get_card_scene_type, 6, "m_count", AIN_INT) ||
        !member_is(get_card_info_type, 0, "<CardId>", AIN_STRING))
        return 0;
    roles[run] = TREASURE_CARDS;
    return R10MOD_TREASURE_MULTI;
}

static void allow_all_treasure_cards(int slot)
{
    struct page *scene = page_is(slot, STRUCT_PAGE, get_card_scene_type, 7);
    struct page *cards = scene ? page_is(scene->values[4].i, ARRAY_PAGE, AIN_ARRAY, 1) : NULL;
    if (!cards || cards->array.rank != 1 || cards->array.struct_type != get_card_info_type ||
        cards->nr_vars > 3)
        return;

    int selectable = 0;
    for (int i = 0; i < cards->nr_vars; i++) {
        struct page *card = page_is(cards->values[i].i, STRUCT_PAGE, get_card_info_type, 1);
        if (card && string_index_valid(card->values[0].i) && heap_get_string(card->values[0].i)->size)
            selectable++;
    }
    if (selectable > scene->values[6].i)
        scene->values[6].i = selectable;
}

static unsigned bind_extra_features(void)
{
    int update=find_function("AFL_View_Update",105458298,0);
    int wait=find_function("parts::detail::WaitForClick",1857412835,2);
    int join=find_function("SceneStack@Join",1997757891,0);
    if (update<0 || wait<0 || join<0)
        return 0;
    view_update=update;
    roles[wait]=WAIT_INPUT;
    roles[join]=JOIN_SCENE;

    unsigned extra=0;
    int info=find_function("SceneSimpleBattleInfo@Run",783031157,0);
    int repick=find_function("SceneSimpleBattleInfo@<lambda : SceneSimpleBattleInfo@0(ref SceneStack, DG_Event)(21, 57)>",
                             1257673066,0);
    enable_button=find_function("ActivityButton@SetEnable",-47257037,1);
    button_type=ain_get_struct(ain,"ActivityButton");
    if (info>=0 && repick>=0 && enable_button>=0 && member_is(button_type,4,"m_partsNumber",AIN_INT)) {
        roles[info]=BATTLE_INFO;
        roles[repick]=BATTLE_REPICK;
        roles[enable_button]=BUTTON_ENABLE;
        extra |= R10MOD_SIMPLE_REPICK;
    }

    int food_run=find_function("SceneFoodTicket@Run",858228685,0);
    int map_run=find_function("SceneFoodTicketInMap@Run",1315663759,0);
    int fade=find_function("FoodTicketTargetGroup@FadeIn",-1037638492,2);
    set_cards=find_function("FoodTicketTargetGroup@SetCard",-1735065795,1);
    show_cards=find_function("FoodTicketTargetGroup@FadeInSelector",512780337,0);
    enable_selector=find_function("FoodTicketTargetGroup@SetEnable",-1466074054,1);
    find_food=find_function("FoodTicketTargetFinder::Find",-2052874626,0);
    find_map_food=find_function("FoodTicketTargetFinder::FindAfter",2138775470,0);
    get_message_flags=find_function("AssistantSelector@MessageFlags::get",94029839,0);
    set_message_cards=find_function("AssistantMessageFlags@SetCharacterIdsFromCardIds",-1124274526,1);
    food_type=ain_get_struct(ain,"SceneFoodTicket");
    map_food_type=ain_get_struct(ain,"SceneFoodTicketInMap");
    selector_type=ain_get_struct(ain,"FoodTicketTargetGroup");
    assistant_type=ain_get_struct(ain,"AssistantSelector");
    message_type=ain_get_struct(ain,"AssistantMessageFlags");
    assistant_global=ain_get_global(ain,"g_assistant");
    if (food_run>=0 && map_run>=0 && fade>=0 && set_cards>=0 && show_cards>=0 &&
        enable_selector>=0 && find_food>=0 && find_map_food>=0 && get_message_flags>=0 &&
        set_message_cards>=0 && selector_type>=0 && assistant_global>=0 && assistant_type>=0 && message_type>=0 &&
        member_is(food_type,3,"m_cardIds",AIN_ARRAY) && member_is(food_type,10,"m_selector",AIN_STRUCT) &&
        member_is(map_food_type,7,"m_cardIds",AIN_ARRAY) && member_is(map_food_type,3,"m_selector",AIN_STRUCT)) {
        roles[food_run]=FOOD_SCENE;
        roles[map_run]=MAP_FOOD_SCENE;
        roles[fade]=SELECTOR_FADE;
        roles[show_cards]=SELECTOR_SHOW;
        roles[enable_selector]=SELECTOR_ENABLE;
        extra |= R10MOD_FOOD_REROLL;
    }
    return extra;
}

static struct object_id identify(int slot)
{
    return (struct object_id){slot,heap[slot].seq};
}

static bool matches_object(struct object_id id, int slot)
{
    return id.slot==slot && page_index_valid(slot) && id.seq==heap[slot].seq;
}

static void clear_food_request(void)
{
    atomic_store(&food_request,FOOD_UNAVAILABLE);
}

bool r10mod_food_reroll_ready(void)
{
    return atomic_load(&food_request)==FOOD_READY;
}

bool r10mod_request_food_reroll(void)
{
    unsigned ready=FOOD_READY;
    return atomic_compare_exchange_strong(&food_request,&ready,FOOD_REQUESTED);
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
    atomic_store(&capabilities, 0);
    clear_food_request();
    view_update=-1;
    food_selector.slot=-1;
    repick_button.slot=-1;
    food_enabled=false;
    free(roles);roles=NULL;nr_roles=0;
    const char *requested_mode = getenv("XSYSTEM4_RANCE10_CHEATS");
    if (requested_mode && !strcmp(requested_mode, "0"))
        return;
    /* The original and Sunset 0.49 bind against the same checked signatures.
     * Match the loaded AIN instead of disabling the mod's launch mode. */
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
    atomic_store(&capabilities, 15 | bind_extra_features() | bind_treasure_multiselect());
}
void r10mod_set_flags(unsigned value) {atomic_store(&requested_flags, value & toggle_mask);}
unsigned r10mod_get_flags(void) {return atomic_load(&requested_flags) & atomic_load(&capabilities);}
unsigned r10mod_get_capabilities(void) {return atomic_load(&capabilities);}

void r10mod_before_method(void)
{
    unsigned enabled=r10mod_get_flags();
    if(call_stack_ptr<=0)return;
    struct function_call *f=&call_stack[call_stack_ptr-1];
    enum role role=role_at(call_stack_ptr-1);
    if (role == TREASURE_CARDS) {
        if (enabled & R10MOD_TREASURE_MULTI)
            allow_all_treasure_cards(f->struct_page);
        return;
    }
    if (role==BATTLE_INFO) {
        repick_button.slot=-1;
        return;
    }
    if (!enabled && role!=SELECTOR_ENABLE && role!=SELECTOR_FADE && role!=BUTTON_ENABLE)
        return;
    struct page *local=page_is(f->page_slot,LOCAL_PAGE,f->fno,1);
    if(!local)return;
    if ((role==SELECTOR_ENABLE || role==SELECTOR_FADE) &&
        page_is(f->struct_page,STRUCT_PAGE,selector_type,1)) {
        if (role==SELECTOR_FADE || !local->values[0].i) {
            food_enabled=false;
            clear_food_request();
        } else if (matches_object(food_selector,f->struct_page)) {
            food_enabled=true;
        }
    }
    if (role==BUTTON_ENABLE && role_at(call_stack_ptr-2)==BATTLE_REPICK &&
        !local->values[0].i && page_is(f->struct_page,STRUCT_PAGE,button_type,5)) {
        repick_button=identify(f->struct_page);
        repick_enabled=(enabled&R10MOD_SIMPLE_REPICK)!=0;
        local->values[0].i=repick_enabled;
    }
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

static struct page *card_ids(int slot)
{
    struct page *cards=page_is(slot,ARRAY_PAGE,AIN_ARRAY,1);
    return cards && cards->array.rank==1 && cards->nr_vars<=6 && cards->element_type &&
        cards->element_type->data==AIN_STRING ? cards:NULL;
}

static bool same_cards(const struct page *a, const struct page *b)
{
    if (a->nr_vars!=b->nr_vars)
        return false;
    for (int i=0; i<a->nr_vars; i++) {
        const struct string *id=heap_get_string(a->values[i].i);
        bool found=false;
        for (int j=0; j<b->nr_vars; j++) {
            const struct string *other=heap_get_string(b->values[j].i);
            if (id->size==other->size && !memcmp(id->text,other->text,id->size)) {
                found=true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

static void reroll_food(struct page *scene, int selector, int ids_index, int finder)
{
    struct page *globals=page_is(0,GLOBAL_PAGE,-1,assistant_global+1);
    int assistant=globals ? globals->values[assistant_global].i:-1;
    if (!page_is(assistant,STRUCT_PAGE,assistant_type,1))
        return;

    int previous=scene->values[ids_index].i;
    int replacement=-1;
    /* Retry a few times when the original picker draws the same people. A
     * small pool may only contain one possible group. */
    for (int draw=0; draw<4; draw++) {
        vm_call(finder,-1);
        int ids=stack_pop().i;
        struct page *next=card_ids(ids);
        if (!next) {
            if (page_index_valid(ids)) heap_unref(ids);
            break;
        }
        if (replacement>=0) heap_unref(replacement);
        replacement=ids;
        if (!same_cards(card_ids(previous),next))
            break;
    }
    if (replacement<0)
        return;

    scene->values[ids_index].i=replacement;
    heap_unref(previous);
    vm_call(get_message_flags,assistant);
    int message=stack_pop().i;
    if (page_is(message,STRUCT_PAGE,message_type,1))
        r10mod_call_method(set_message_cards,message,vm_copy(vm_int(replacement),AIN_ARRAY));
    if (page_index_valid(message)) heap_unref(message);
    r10mod_call_method(set_cards,selector,vm_copy(vm_int(replacement),AIN_ARRAY));
    vm_call(show_cards,selector);
    r10mod_call_method(enable_selector,selector,vm_bool(true));
}

void r10mod_before_call(int function)
{
    if (function!=view_update)
        return;
    /* Only the normal scene wait may replace cards. Nested dialogs, help,
     * effects and event processing have a different input stack. */
    int top=call_stack_ptr-1;
    if (role_at(top)!=WAIT_INPUT || role_at(top-2)!=JOIN_SCENE) {
        clear_food_request();
        return;
    }
    enum role scene_role=role_at(top-3);
    if (scene_role==BATTLE_INFO && matches_object(repick_button,repick_button.slot)) {
        bool enabled=(r10mod_get_flags()&R10MOD_SIMPLE_REPICK)!=0;
        if (enabled!=repick_enabled) {
            repick_enabled=enabled;
            r10mod_call_method(enable_button,repick_button.slot,vm_bool(enabled));
        }
    }
    if (scene_role!=FOOD_SCENE && scene_role!=MAP_FOOD_SCENE) {
        clear_food_request();
        return;
    }
    bool in_map=scene_role==MAP_FOOD_SCENE;
    int ids_index=in_map ? 7:3;
    int selector_index=in_map ? 3:10;
    struct page *scene=page_is(call_stack[top-3].struct_page,STRUCT_PAGE,in_map ? map_food_type:food_type,
                               in_map ? 8:11);
    int selector=scene ? scene->values[selector_index].i:-1;
    /* The stock card entrance runs for up to 350 + 5*30 ms. */
    if (!food_enabled || !matches_object(food_selector,selector) ||
        (uint32_t)vm_time()-food_shown_at<500 || !card_ids(scene->values[ids_index].i)) {
        clear_food_request();
        return;
    }
    unsigned state=atomic_load(&food_request);
    if (state==FOOD_REQUESTED) {
        clear_food_request();
        reroll_food(scene,selector,ids_index,in_map ? find_map_food:find_food);
    } else if (state==FOOD_UNAVAILABLE) {
        atomic_store(&food_request,FOOD_READY);
    }
}

void r10mod_return(void)
{
    unsigned enabled=r10mod_get_flags();
    if(call_stack_ptr<=0)return;
    struct function_call *f=&call_stack[call_stack_ptr-1];
    enum role role=role_at(call_stack_ptr-1);
    if (role==SELECTOR_SHOW && page_is(f->struct_page,STRUCT_PAGE,selector_type,1)) {
        food_selector=identify(f->struct_page);
        food_shown_at=(uint32_t)vm_time();
        food_enabled=true;
        clear_food_request();
    }
    if (role==FOOD_SCENE || role==MAP_FOOD_SCENE) {
        food_enabled=false;
        food_selector.slot=-1;
        clear_food_request();
    }
    if (role==BATTLE_INFO)
        repick_button.slot=-1;
    if(!enabled || stack_ptr<=0)return;
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
JNIEXPORT jboolean JNICALL Java_io_github_kichikuou_xsystem4_Rance10CheatBridge_nativeFoodRerollReady(JNIEnv *e,jobject o)
{(void)e;(void)o;return r10mod_food_reroll_ready();}
JNIEXPORT jboolean JNICALL Java_io_github_kichikuou_xsystem4_Rance10CheatBridge_nativeRequestFoodReroll(JNIEnv *e,jobject o)
{(void)e;(void)o;return r10mod_request_food_reroll();}
#endif
