/* Scrollbar role prefixes use the AIN11+ string-return ABI directly.
 * Roles in the widget model: thumb, previous, next, background. */
#define SCROLL_OVERRIDE_ROLES(X) \
 X(V,Up,PARTS_WIDGET_VSCROLL,1) \
 X(V,Down,PARTS_WIDGET_VSCROLL,2) \
 X(V,Bar,PARTS_WIDGET_VSCROLL,0) \
 X(V,BG,PARTS_WIDGET_VSCROLL,3) \
 X(H,Left,PARTS_WIDGET_HSCROLL,1) \
 X(H,Right,PARTS_WIDGET_HSCROLL,2) \
 X(H,Bar,PARTS_WIDGET_HSCROLL,0) \
 X(H,BG,PARTS_WIDGET_HSCROLL,3)

static void ScrollOverride_Set(int number,int type,int role,bool flat,struct string *name)
{
    parts_widget_get(number,type);
    if(!parts_widget_set_scroll_override(parts_get(number),flat,role,name))
        VM_ERROR("Invalid or missing scrollbar override skin");
}
static struct string *ScrollOverride_Get(int number,int type,int role,bool flat)
{
    struct parts_widget *w=parts_widget_get(number,type);
    struct string *name=flat?w->scroll_flat_override[role]:w->scroll_cg_override[role];
    return string_dup(name?name:&EMPTY_STRING);
}
#define SCROLL_OVERRIDE_IMPL(axis,role,type,index) \
 static void Scroll_Set##axis##ScrollbarOverride##role##CGName(int number,struct string *name) \
 { ScrollOverride_Set(number,type,index,false,name); } \
 static struct string *Scroll_Get##axis##ScrollbarOverride##role##CGName(int number) \
 { return ScrollOverride_Get(number,type,index,false); } \
 static void Scroll_Set##axis##ScrollbarOverride##role##FlatName(int number,struct string *name) \
 { ScrollOverride_Set(number,type,index,true,name); } \
 static struct string *Scroll_Get##axis##ScrollbarOverride##role##FlatName(int number) \
 { return ScrollOverride_Get(number,type,index,true); }
SCROLL_OVERRIDE_ROLES(SCROLL_OVERRIDE_IMPL)
#undef SCROLL_OVERRIDE_IMPL

#define SCROLL_OVERRIDE_EXPORT_ROLE(axis,role,type,index) \
 HLL_EXPORT(Set##axis##ScrollbarOverride##role##CGName,Scroll_Set##axis##ScrollbarOverride##role##CGName), \
 HLL_EXPORT(Get##axis##ScrollbarOverride##role##CGName,Scroll_Get##axis##ScrollbarOverride##role##CGName), \
 HLL_EXPORT(Set##axis##ScrollbarOverride##role##FlatName,Scroll_Set##axis##ScrollbarOverride##role##FlatName), \
 HLL_EXPORT(Get##axis##ScrollbarOverride##role##FlatName,Scroll_Get##axis##ScrollbarOverride##role##FlatName),
#define SCROLL_OVERRIDE_EXPORTS SCROLL_OVERRIDE_ROLES(SCROLL_OVERRIDE_EXPORT_ROLE)
