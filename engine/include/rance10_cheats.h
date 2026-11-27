/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RANCE10_CHEATS_H
#define RANCE10_CHEATS_H
#include <stdbool.h>
enum {
    R10MOD_HP=1, R10MOD_AP=2, R10MOD_TREASURE=4, R10MOD_ITEMS=8,
    R10MOD_SIMPLE_REPICK=16, R10MOD_FOOD_REROLL=32, R10MOD_TREASURE_MULTI=64
};
#ifdef XSYSTEM4_RANCE10_CHEATS
void r10mod_init(void);
void r10mod_before_method(void);
void r10mod_return(void);
void r10mod_before_call(int function);
void r10mod_set_flags(unsigned flags);
unsigned r10mod_get_flags(void);
unsigned r10mod_get_capabilities(void);
bool r10mod_food_reroll_ready(void);
bool r10mod_request_food_reroll(void);
#else
static inline void r10mod_init(void) {}
static inline void r10mod_before_method(void) {}
static inline void r10mod_return(void) {}
static inline void r10mod_before_call(int function) {(void)function;}
#endif
#endif
