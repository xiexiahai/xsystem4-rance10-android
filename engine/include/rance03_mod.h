#ifndef RANCE03_MOD_H
#define RANCE03_MOD_H
#include <stdbool.h>
#include <stdint.h>
union vm_value;
#ifdef XSYSTEM4_RANCE03_MOD
enum { R03MOD_HP=1, R03MOD_COOLDOWN=2, R03MOD_DROP=4, R03MOD_ITEMS=8 };
void r03mod_init(void);
void r03mod_before_method(void);
void r03mod_return(void);
bool r03mod_keep_item(union vm_value *value);
void r03mod_idle(void);
void r03mod_poll(void);
void r03mod_set_flags(unsigned flags);
unsigned r03mod_get_state(void);
bool r03mod_action(int action);
void r03mod_call_setter(int function, int object, int value);
#else
static inline void r03mod_init(void) {}
static inline void r03mod_before_method(void) {}
static inline void r03mod_return(void) {}
static inline bool r03mod_keep_item(union vm_value *value) { (void)value; return false; }
static inline void r03mod_idle(void) {}
static inline void r03mod_poll(void) {}
#endif
#endif
