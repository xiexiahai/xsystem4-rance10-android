/* Multi-output geometry queries for the modern PartsEngine ABI.
 * Coordinates and dimensions intentionally delegate to existing getters.
 * Surface queries read existing parts only and never change component type.
 * Invalid surface state preserves outputs, matching PE_GetPartsCGSurfaceArea.
 */
static void PartsEngine_GetComponentAbsolutePos(int number, float *x, float *y)
{
    *x = PE_parts_get_absolute_x(number);
    *y = PE_parts_get_absolute_y(number);
}

static void PartsEngine_GetPartsUpperLeftPos(int number, float *x, float *y, int state)
{
    *x = (float)PE_GetPartsUpperLeftPosX(number, state);
    *y = (float)PE_GetPartsUpperLeftPosY(number, state);
}

static void PartsEngine_GetPartsSize(int number, int *w, int *h, int state)
{
    *w = PE_GetPartsWidth(number, state);
    *h = PE_GetPartsHeight(number, state);
}

static void PartsEngine_GetTypedSurfaceArea(int number, int *x, int *y,
                                          int *w, int *h, int state, enum parts_type type)
{
    if (!parts_state_valid(--state))
        return;
    struct parts *p = parts_try_get(number);
    Rectangle area = p && p->states[state].type == type
        ? p->states[state].common.surface_area : (Rectangle){0};
    *x = area.x; *y = area.y; *w = area.w; *h = area.h;
}

#define PARTS_GEOMETRY_SURFACE(Name, Type) \
static void PartsEngine_Get##Name##SurfaceArea(int number, int *x, int *y, int *w, int *h, int state) \
{ PartsEngine_GetTypedSurfaceArea(number, x, y, w, h, state, Type); }
PARTS_GEOMETRY_SURFACE(PartsText, PARTS_TEXT)
PARTS_GEOMETRY_SURFACE(HGauge, PARTS_HGAUGE)
PARTS_GEOMETRY_SURFACE(VGauge, PARTS_VGAUGE)
PARTS_GEOMETRY_SURFACE(Numeral, PARTS_NUMERAL)
PARTS_GEOMETRY_SURFACE(Construction, PARTS_CONSTRUCTION_PROCESS)
#undef PARTS_GEOMETRY_SURFACE
