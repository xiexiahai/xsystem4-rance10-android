/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RANCE10_CHEATS_H
#define RANCE10_CHEATS_H
enum { R10MOD_HP=1, R10MOD_AP=2, R10MOD_TREASURE=4, R10MOD_ITEMS=8 };
#ifdef XSYSTEM4_RANCE10_CHEATS
void r10mod_init(void);
void r10mod_before_method(void);
void r10mod_return(void);
void r10mod_set_flags(unsigned flags);
unsigned r10mod_get_flags(void);
unsigned r10mod_get_capabilities(void);
#else
static inline void r10mod_init(void) {}
static inline void r10mod_before_method(void) {}
static inline void r10mod_return(void) {}
#endif
#endif
