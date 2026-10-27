/* KOTOR 1 nameplates - WoW-style health plates over every creature in range.
 *
 * Loaded as a dinput8.dll proxy (swkotor.exe imports DirectInput8Create from it). On attach it patches
 * three places in swkotor.exe (1.03 Steam/GOG layout, addresses from LaneDibello's Kotor-Patch-Manager DB):
 *   - CSWGuiMainInterface::Draw's call to CSWGuiPanel::Draw (0x68b6a7): plates are drawn first, under the HUD,
 *     and only when the HUD itself is drawn (no plates in dialogs, menus, cutscenes).
 *   - its call to CSWGuiTargetActionMenu::Draw (0x68b6b3): the engine's own name + health bar are skipped
 *     when the plate already shows them; the action buttons still draw.
 *   - CClientExoAppInternal::HandleMouseOver (0x6222f0): a cursor over a plate counts as a cursor over that
 *     creature, so clicking a plate selects/attacks it exactly like clicking the model.
 */
#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------------------------- engine */
#define APP_MANAGER   (*(char **)0x7a39fc)
#define INVALID_ID    0x7f000000u
#define OBJ_CREATURE  5
#define OBJ_ITEM      6
#define OBJ_PLACEABLE 9
#define OBJ_DOOR      10

typedef void *(__thiscall *GetGameObject_t)(void *client, unsigned id);
typedef void *(__thiscall *NoArgPtr_t)(void *self);
typedef float (__thiscall *NoArgFloat_t)(void *self);
typedef short (__thiscall *IntArgShort_t)(void *self, int a);
typedef void *(__thiscall *VecOut_t)(void *self, float *out);
typedef void *(__thiscall *MapToScreen_t)(void *cam, float *out, float x, float y, float z, int clamp);
typedef void *(__thiscall *GetName_t)(void *creature, void *exostr);
typedef void (__thiscall *ExoStrDtor_t)(void *exostr);
typedef void (__thiscall *Draw_t)(void *self, float dt);
typedef void (__thiscall *MouseOver_t)(void *internal, unsigned id, float *pos, int flag);
typedef int (__thiscall *IntArgInt_t)(void *self, int a);
typedef int (__thiscall *LocGet_t)(void *loc, int lang, void *out, int gender);

#define GetGameObject     ((GetGameObject_t)0x5ed580)
#define GetPlayerCreature ((NoArgPtr_t)0x5ed540)
#define GetModuleCamera   ((NoArgPtr_t)0x5ed560)
#define GetSWGuiManager   ((NoArgPtr_t)0x5eda40)
#define GetServerObject   ((NoArgPtr_t)0x63d4b0)
#define AppearanceHeight  ((NoArgFloat_t)0x6973b0)
#define CreatureGetName   ((GetName_t)0x617090)
#define ExoStringDtor     ((ExoStrDtor_t)0x5e5c20)
#define PanelDraw         ((Draw_t)0x40b760)
#define TargetMenuDraw    ((Draw_t)0x685ed0)
#define PlaceableItemCount ((IntArgInt_t)0x587710)
#define LocStringGet      ((LocGet_t)0x5ea130)      /* CExoLocString::GetString: embedded or dialog.tlk */

#define VT(o, off) (*(void ***)(o))[(off) / 4]
#define F(o, off, T) (*(T *)((char *)(o) + (off)))

typedef struct { char *s; int len; } ExoString;

/* ---------------------------------------------------------------------------------------------- config */
static struct {
    int enabled, show_neutral, show_friendly, show_objects, show_doors, hide_empty, hp_text, toggle_vk, stack,
        require_view, debug;
    float range, object_range, obj_px, scale, alpha_other, bar_w, bar_h, target_mul, name_px, lift;
    char font[64];
} cfg;
static char dir[MAX_PATH], log_path[MAX_PATH];

static void log_msg(const char *fmt, ...)
{
    FILE *f = fopen(log_path, "a");
    if (!f) return;
    va_list a;
    va_start(a, fmt);
    vfprintf(f, fmt, a);
    va_end(a);
    fputc('\n', f);
    fclose(f);
}

static float ini_f(const char *ini, const char *k, float d)
{
    char b[32], def[32];
    snprintf(def, sizeof def, "%g", d);
    GetPrivateProfileStringA("Nameplates", k, def, b, sizeof b, ini);
    return (float)atof(b);
}

static void load_config(void)
{
    char ini[MAX_PATH];
    snprintf(ini, sizeof ini, "%s\\nameplates.ini", dir);
#define I(k, d) GetPrivateProfileIntA("Nameplates", k, d, ini)
    cfg.enabled = I("Enabled", 1);
    cfg.show_neutral = I("ShowNeutral", 0);
    cfg.show_objects = I("ShowObjects", 1);
    cfg.show_doors = I("ShowDoors", 0);
    cfg.hide_empty = I("HideEmpty", 0);
    cfg.show_friendly = I("ShowFriendly", 0);
    cfg.hp_text = I("HealthText", 1);
    cfg.toggle_vk = I("ToggleKey", VK_F6);
    cfg.stack = I("StackPlates", 1);
    cfg.require_view = I("OnlyVisible", 1);
    cfg.debug = I("Debug", 0);
#undef I
    cfg.range = ini_f(ini, "Range", 60.0f);
    cfg.object_range = ini_f(ini, "ObjectRange", 40.0f);
    cfg.obj_px = ini_f(ini, "ObjectNameSize", 15.0f);
    cfg.scale = ini_f(ini, "Scale", 1.0f);
    cfg.alpha_other = ini_f(ini, "OtherAlpha", 0.7f);
    cfg.bar_w = ini_f(ini, "BarWidth", 132.0f);
    cfg.bar_h = ini_f(ini, "BarHeight", 11.0f);
    cfg.target_mul = ini_f(ini, "TargetScale", 1.2f);
    cfg.name_px = ini_f(ini, "NameSize", 17.0f);
    cfg.lift = ini_f(ini, "HeadLift", 0.25f);
    GetPrivateProfileStringA("Nameplates", "Font", "Bahnschrift SemiBold", cfg.font, sizeof cfg.font, ini);
}

/* ---------------------------------------------------------------------------------------------- patching */
static int patch(void *at, const void *bytes, size_t n)
{
    DWORD old;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(at, bytes, n);
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return 1;
}

/* Redirect an E8 call at `site` (which must currently call `expect`) to `fn`. */
static int hook_call(unsigned site, unsigned expect, void *fn)
{
    unsigned char *p = (unsigned char *)site;
    if (p[0] != 0xE8 || site + 5 + *(int *)(p + 1) != expect) {
        log_msg("hook_call %08x: unexpected bytes, skipped", site);
        return 0;
    }
    int rel = (int)((unsigned)fn - (site + 5));
    return patch(p + 1, &rel, 4);
}

/* Detour a function entry whose first `n` bytes are position-independent; returns a trampoline. */
static void *hook_entry(unsigned at, const unsigned char *expect, int n, void *fn)
{
    unsigned char *p = (unsigned char *)at;
    if (memcmp(p, expect, n)) {
        log_msg("hook_entry %08x: unexpected bytes, skipped", at);
        return NULL;
    }
    unsigned char *t = VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(t, p, n);
    t[n] = 0xE9;
    *(int *)(t + n + 1) = (int)((at + n) - ((unsigned)t + n + 5));
    unsigned char j[16];
    memset(j, 0x90, sizeof j);
    j[0] = 0xE9;
    *(int *)(j + 1) = (int)((unsigned)fn - (at + 5));
    patch(p, j, n);
    return t;
}

/* ---------------------------------------------------------------------------------------------- font */
#define GLYPH0 32
#define GLYPHS 224
typedef struct { float u0, v0, u1, v1, w, h; } Glyph;
typedef struct { GLuint tex; int px; float line; Glyph g[GLYPHS]; } Font;
static Font font_name, font_small, font_obj;

static int make_font(Font *f, int px, int weight)
{
    HDC dc = CreateCompatibleDC(NULL);
    HFONT hf = CreateFontA(-px, 0, 0, 0, weight, 0, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH, cfg.font);
    SelectObject(dc, hf);
    TEXTMETRICA tm;
    GetTextMetricsA(dc, &tm);
    int cw = 0;
    SIZE sz[GLYPHS];
    for (int i = 0; i < GLYPHS; i++) {
        char c = (char)(GLYPH0 + i);
        GetTextExtentPoint32A(dc, &c, 1, &sz[i]);
        if (sz[i].cx > cw) cw = sz[i].cx;
    }
    int pad = 2, cellw = cw + pad * 2, cellh = tm.tmHeight + pad * 2, cols = 16;
    int W = 16, H = 16;
    while (W < cellw * cols) W *= 2;
    while (H < cellh * (GLYPHS / cols)) H *= 2;

    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    unsigned *bits;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    SelectObject(dc, bmp);
    memset(bits, 0, W * H * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    for (int i = 0; i < GLYPHS; i++) {
        char c = (char)(GLYPH0 + i);
        int x = (i % cols) * cellw + pad, y = (i / cols) * cellh + pad;
        TextOutA(dc, x, y, &c, 1);
        Glyph *g = &f->g[i];
        g->u0 = (float)x / W;
        g->v0 = (float)y / H;
        g->u1 = (float)(x + sz[i].cx) / W;
        g->v1 = (float)(y + tm.tmHeight) / H;
        g->w = (float)sz[i].cx;
        g->h = (float)tm.tmHeight;
    }
    GdiFlush();
    for (int i = 0; i < W * H; i++) {
        unsigned a = bits[i] & 0xff;
        bits[i] = (a << 24) | 0xffffff;
    }
    glGenTextures(1, &f->tex);
    glBindTexture(GL_TEXTURE_2D, f->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, bits);
    f->px = px;
    f->line = (float)tm.tmHeight;
    DeleteObject(bmp);
    DeleteObject(hf);
    DeleteDC(dc);
    return 1;
}

static float text_w(Font *f, const char *s)
{
    float w = 0;
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        if (c >= GLYPH0) w += f->g[c - GLYPH0].w;
    }
    return w;
}

static void text_quads(Font *f, const char *s, float x, float y)
{
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        if (c < GLYPH0) continue;
        Glyph *g = &f->g[c - GLYPH0];
        glTexCoord2f(g->u0, g->v0); glVertex2f(x, y);
        glTexCoord2f(g->u1, g->v0); glVertex2f(x + g->w, y);
        glTexCoord2f(g->u1, g->v1); glVertex2f(x + g->w, y + g->h);
        glTexCoord2f(g->u0, g->v1); glVertex2f(x, y + g->h);
        x += g->w;
    }
}

/* Text with a soft dark outline (8 offset passes), like WoW's OUTLINE font flag. */
static void draw_text(Font *f, const char *s, float x, float y, float r, float g, float b, float a, float k)
{
    x = floorf(x + 0.5f);
    y = floorf(y + 0.5f);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, f->tex);
    glBegin(GL_QUADS);
    glColor4f(0, 0, 0, a * 0.55f);
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx || dy) text_quads(f, s, x + dx * k, y + dy * k);
    glColor4f(r, g, b, a);
    text_quads(f, s, x, y);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

static void rect(float x0, float y0, float x1, float y1, float r, float g, float b, float a)
{
    glColor4f(r, g, b, a);
    glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
}

static void rect_grad(float x0, float y0, float x1, float y1, const float *c, float top, float bot, float a)
{
    glColor4f(fminf(c[0] * top, 1), fminf(c[1] * top, 1), fminf(c[2] * top, 1), a);
    glVertex2f(x0, y0); glVertex2f(x1, y0);
    glColor4f(c[0] * bot, c[1] * bot, c[2] * bot, a);
    glVertex2f(x1, y1); glVertex2f(x0, y1);
}

/* ---------------------------------------------------------------------------------------------- plates */
#define MAX_PLATES 192
enum { KIND_CREATURE, KIND_OBJECT };
typedef struct {
    unsigned id;
    int kind;
    float sx, sy, dist;            /* anchor (bottom-centre of the plate stack) in GUI px, metres from player */
    float x0, y0, x1, y1;          /* creature: bar rect; object: name text rect */
    float top;                     /* top of everything drawn (name included), for stacking/hit tests */
    float hp, chip;                /* 0..1 health, lagging "damage taken" chip */
    int cur, max, reaction, target, hover;
    float col[3], alpha;           /* object label colour / dimming */
    int loot;                      /* container that still holds items: sparkles */
    char name[64];
} Plate;

typedef struct { unsigned id; float chip, hp, hold; DWORD seen; } Track;   /* per-creature smoothing */
static Track tracks[MAX_PLATES];
static Plate plates[MAX_PLATES];
static int nplates;
static volatile int hit_count;
static struct { unsigned id; int kind; float x0, y0, x1, y1; float pos[3]; } hits[MAX_PLATES];
static float gui_w = 1, gui_h = 1;
static HWND game_wnd;
static LARGE_INTEGER qpf, last_qpc;
static float anim_t;               /* seconds, drives the loot sparkle */

static Track *track(unsigned id, float hp, DWORD now)
{
    Track *free_t = NULL, *oldest = &tracks[0];
    for (int i = 0; i < MAX_PLATES; i++) {
        if (tracks[i].id == id) return &tracks[i];
        if (!tracks[i].id && !free_t) free_t = &tracks[i];
        if (tracks[i].seen < oldest->seen) oldest = &tracks[i];
    }
    Track *t = free_t ? free_t : oldest;
    t->id = id;
    t->hp = t->chip = hp;
    t->hold = 0;
    t->seen = now;
    return t;
}

static const float COL_HOSTILE[3] = {0.86f, 0.13f, 0.10f};
static const float COL_NEUTRAL[3] = {0.95f, 0.78f, 0.12f};
static const float COL_FRIEND[3] = {0.20f, 0.78f, 0.25f};
static const float COL_LOOT[3] = {1.00f, 0.80f, 0.28f};     /* containers with items, items on the ground */
static const float COL_EMPTY[3] = {0.62f, 0.62f, 0.62f};    /* looted containers */
static const float COL_USE[3] = {0.45f, 0.85f, 1.00f};      /* terminals, workbenches, ... (engine hilite cyan) */
static const float COL_DOOR[3] = {0.78f, 0.82f, 0.90f};

static int cmp_dist(const void *a, const void *b)
{
    const Plate *pa = a, *pb = b;
    if (pa->kind != pb->kind) return pb->kind - pa->kind;            /* object labels under creature plates */
    float d = pb->dist - pa->dist;                                    /* far first, near drawn on top */
    return (d > 0) - (d < 0);
}

static int log_now;   /* Debug=1: dump candidates + plates every 2 s */
/* Killing a creature leaves the dead creature (what the cursor hits) plus an invisible "Remains" placeable at the
   same spot holding the loot (after a reload only the placeable exists). Hovering/targeting the corpse lights
   up the Remains label, and the corpse id counts as covered so the engine's own label stays hidden. */
static float corpse_pos[2][3];
static int corpse_valid[2];          /* [0] hovered corpse, [1] targeted corpse */
static unsigned corpse_alias[2];     /* corpse ids whose Remains got a label this frame */

/* world point -> GUI px; 0 if behind the camera or far off screen */
static int project(void *cam, const float *cpos, const float *fwd, const float *wp, float *sx, float *sy)
{
    float rel = (wp[0] - cpos[0]) * fwd[0] + (wp[1] - cpos[1]) * fwd[1] + (wp[2] - cpos[2]) * fwd[2];
    if (rel < 0.2f) return 0;
    float sp[3];
    float *r = ((MapToScreen_t)VT(cam, 0x28))(cam, sp, wp[0], wp[1], wp[2], 1);
    *sx = r[0] * gui_w;
    *sy = r[1] * gui_h;
    return *sx > -200 && *sx < gui_w + 200 && *sy > -100 && *sy < gui_h + 200;
}

/* Top of a model's bounding box in world z. Gob +0x120/+0x12c hold min/max; whether they're world or model space
   isn't known, so: a box that contains the object's x/y is world space, otherwise offset it by the position. */
static float model_top(const char *gob, const float *pos, float fallback)
{
    if (!gob) return pos[2] + fallback;
    const float *mn = (const float *)(gob + 0x120), *mx = (const float *)(gob + 0x12c);
    if (!(mx[2] > mn[2]) || mx[2] - mn[2] > 50) return pos[2] + fallback;
    int world = pos[0] >= mn[0] - 0.5f && pos[0] <= mx[0] + 0.5f && pos[1] >= mn[1] - 0.5f && pos[1] <= mx[1] + 0.5f;
    float top = world ? mx[2] : pos[2] + mx[2];
    if (top < pos[2] + 0.15f || top > pos[2] + 6) return pos[2] + fallback;
    return top;
}

static void add_creature(char *obj, char *cr, void *cam, const float *cpos, const float *fwd, float dist,
                         int is_target, int is_hover, DWORD now)
{
    char *so = GetServerObject(obj);
    if (!so) return;
    int maxhp = ((IntArgShort_t)VT(so, 0x98))(so, 1);
    int cur = ((IntArgShort_t)VT(so, 0x9c))(so, 0);
    if (cur <= 0 || maxhp <= 0) return;                                     /* dead: no plate */

    float *pos = &F(obj, 0x24, float);
    char *app = F(cr, 0x21c, char *);
    float h = app ? AppearanceHeight(app) : 0;
    if (!(h > 0.3f && h < 30)) h = 1.75f;
    float wp[3] = {pos[0], pos[1], pos[2] + h + cfg.lift}, sx, sy;
    if (!project(cam, cpos, fwd, wp, &sx, &sy)) return;

    Plate *p = &plates[nplates++];
    memset(p, 0, sizeof *p);
    p->id = F(obj, 4, unsigned);
    p->kind = KIND_CREATURE;
    p->sx = sx;
    p->sy = sy;
    p->dist = dist;
    p->cur = cur;
    p->max = maxhp;
    p->hp = fminf((float)cur / maxhp, 1);
    p->reaction = F(cr, 0x2dc, unsigned char);
    p->target = is_target;
    p->hover = is_hover;
    ExoString s = {0, 0};
    CreatureGetName(cr, &s);
    if (s.s) {
        strncpy(p->name, s.s, sizeof p->name - 1);
        ExoStringDtor(&s);
    }
    Track *t = track(p->id, p->hp, now);
    t->seen = now;
    if (p->hp < t->hp) t->hold = 0.35f;             /* fresh damage: chip waits, then drains */
    if (p->hp > t->chip) t->chip = p->hp;
    t->hp = p->hp;
    p->chip = t->chip;
}

/* Object names. The client copy (+0xe8, what the hover label shows) only arrives from the server once the object
   is hovered, so placeables read their server-side localized name (+0x228) the way the engine reads item names
   (CExoLocString::GetString with the client language). Cached per id; the cache resets with the area. */
static struct { unsigned id; char name[64]; } name_cache[512];
static int name_count;
static void *name_area;

static const char *object_name(char *client, char *obj, int type)
{
    unsigned id = F(obj, 4, unsigned);
    for (int i = 0; i < name_count; i++)
        if (name_cache[i].id == id) return name_cache[i].name;
    char buf[64] = "";
    const char *cl = F(obj, 0xe8, char *);
    if (type == OBJ_DOOR && !(cl && *cl)) cl = F(obj, 0xfc, char *);          /* CSWCDoor::GetName's string */
    if (cl && *cl) strncpy(buf, cl, sizeof buf - 1);
    if (!*buf && type == OBJ_PLACEABLE) {
        char *so = GetServerObject(obj);
        if (so) {
            ExoString str = {0, 0};
            int lang = F(*(char **)(client + 4), 0x144, int);             /* CClientExoApp::GetClientLanguage */
            if (LocStringGet(so + 0x228, lang, &str, 0) && str.s) strncpy(buf, str.s, sizeof buf - 1);
            if (str.s) ExoStringDtor(&str);
        }
    }
    if (!*buf) return "";                        /* not cached: the client name may still arrive */
    if (name_count == (int)(sizeof name_cache / sizeof *name_cache)) name_count = 0;
    name_cache[name_count].id = id;
    memcpy(name_cache[name_count].name, buf, sizeof buf);
    return name_cache[name_count++].name;
}

static void add_object(char *client, char *obj, int type, char *gob, void *cam, const float *cpos, const float *fwd,
                       float dist, int is_target, int is_hover)
{
    char *so = type == OBJ_PLACEABLE ? GetServerObject(obj) : NULL;
    if (type == OBJ_PLACEABLE) {
        /* the .utp Useable flag: scenery (landspeeders) and invisible script markers ("Zone Controller") are
           selectable for scripts but can't be used */
        int usable = so ? F(so, 0x328, int) : 0;
        if (log_now)
            log_msg("    plc %08x usable=%d inv=%d", F(obj, 4, unsigned), usable, so ? F(so, 0x334, int) : -1);
        if (!usable) return;
    }
    const char *name = object_name(client, obj, type);
    if (log_now) log_msg("    obj %08x t%d name '%s'", F(obj, 4, unsigned), type, name);
    if (!*name) return;
    const float *col = type == OBJ_DOOR ? COL_DOOR : type == OBJ_ITEM ? COL_LOOT : COL_USE;
    float alpha = 1;
    int loot = type == OBJ_ITEM, empty = 0;
    if (type == OBJ_PLACEABLE) {
        /* containers (incl. "Remains") have an item repository; empty ones are dimmed (KPM's empty-container test) */
        if (so && F(so, 0x324, void *)) {
            if (PlaceableItemCount(so, 0) > 0) {
                col = COL_LOOT;
                loot = 1;
            } else {
                if (cfg.hide_empty && !is_target && !is_hover) return;
                col = COL_EMPTY;
                empty = 1;
                alpha = 0.6f;
            }
        }
    }
    float *pos = &F(obj, 0x24, float);
    float wp[3] = {pos[0], pos[1], model_top(gob, pos, 0.8f) + 0.15f}, sx, sy;
    if (!project(cam, cpos, fwd, wp, &sx, &sy)) return;

    Plate *p = &plates[nplates++];
    memset(p, 0, sizeof *p);
    p->id = F(obj, 4, unsigned);
    p->kind = KIND_OBJECT;
    p->sx = sx;
    p->sy = sy;
    p->dist = dist;
    for (int c = 0; c < 2; c++) {
        if (!corpse_valid[c] || type != OBJ_PLACEABLE) continue;
        float ex = pos[0] - corpse_pos[c][0], ey = pos[1] - corpse_pos[c][1];
        if (ex * ex + ey * ey < 1.5f * 1.5f) {
            if (c) is_target = 1;
            else is_hover = 1;
            corpse_valid[c] = 2;
        }
    }
    p->target = is_target;
    p->hover = is_hover;
    memcpy(p->col, col, sizeof p->col);
    p->alpha = alpha;
    p->loot = loot;
    snprintf(p->name, sizeof p->name, empty ? "%s [empty]" : "%s", name);
}

static void collect(void)
{
    nplates = 0;
    char *client = APP_MANAGER ? *(char **)(APP_MANAGER + 4) : NULL;
    if (!client) return;
    char *internal = *(char **)(client + 4);
    char *module = internal ? *(char **)(internal + 0x18) : NULL;
    char *area = module ? *(char **)(module + 0x48) : NULL;
    void *cam = GetModuleCamera(client);
    char *player = GetPlayerCreature(client);
    char *gm = GetSWGuiManager(client);
    if (!area || !cam || !player || !gm) return;
    gui_w = F(gm, 0x6c, short);
    gui_h = F(gm, 0x6e, short);
    if (area != name_area) {
        name_area = area;
        name_count = 0;
    }

    float cpos[3], q[4];
    memcpy(cpos, ((VecOut_t)VT(cam, 0x20))(cam, cpos), 12);
    memcpy(q, ((VecOut_t)VT(cam, 0x24))(cam, q), 16);
    float fwd[3] = {-2 * (q[0] * q[2] + q[1] * q[3]), -2 * (q[1] * q[2] - q[0] * q[3]),
                    -(1 - 2 * (q[0] * q[0] + q[1] * q[1]))};
    float *ppos = &F(player, 0x24, float);
    unsigned target = F(internal, 0x2b4, unsigned), hover = F(internal, 0x4a4, unsigned);
    unsigned sel[2] = {hover, target};
    for (int c = 0; c < 2; c++) {
        corpse_valid[c] = 0;
        corpse_alias[c] = 0;
        if (!sel[c] || sel[c] == INVALID_ID) continue;
        char *go = GetGameObject(client, sel[c]);
        char *o = go ? ((NoArgPtr_t)VT(go, 0xc))(go) : NULL;
        if (!o || F(o, 8, unsigned char) != OBJ_CREATURE) continue;
        char *so = GetServerObject(o);
        if (!so || ((IntArgShort_t)VT(so, 0x9c))(so, 0) > 0) continue;
        memcpy(corpse_pos[c], &F(o, 0x24, float), 12);
        corpse_valid[c] = 1;
    }

    unsigned *ids = F(area, 0x20c, unsigned *);
    int n = F(area, 0x210, int);
    DWORD now = GetTickCount();
    for (int i = 0; i < n && nplates < MAX_PLATES; i++) {
        char *go = GetGameObject(client, ids[i]);
        if (!go) continue;
        char *obj = ((NoArgPtr_t)VT(go, 0xc))(go);
        /* +0x90: selectable - the same test the engine's own "hilite area objects" uses */
        if (!obj || !F(obj, 0x90, unsigned char)) continue;
        int type = F(obj, 8, unsigned char);
        if (type == OBJ_CREATURE) {
            if (obj == player) continue;
        } else if (!(((type == OBJ_PLACEABLE || type == OBJ_ITEM) && cfg.show_objects) ||
                     (type == OBJ_DOOR && cfg.show_doors)))
            continue;
        unsigned id = F(obj, 4, unsigned);
        int is_target = id == target, is_hover = id == hover;

        float *pos = &F(obj, 0x24, float);
        float dx = pos[0] - ppos[0], dy = pos[1] - ppos[1], dz = pos[2] - ppos[2];
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        /* Gob::IsInViewVolume (+0x11): the renderer flags every model it draws this frame (frustum + room
           visibility), i.e. exactly what you could click on. Culled rooms behind doors stay unlabelled. */
        char *anim = F(obj, 0x68, char *);
        char *gob = anim ? F(anim, 0xb8, char *) : NULL;
        int in_view = gob ? F(gob, 0x11, unsigned char) : 1;
        float range = type == OBJ_CREATURE ? cfg.range : cfg.object_range;
        if (log_now)
            log_msg("  cand %08x t%d d=%.1f view=%d%s%s", id, type, dist, in_view, is_target ? " T" : "",
                    is_hover ? " H" : "");
        if (!is_target && !is_hover && (dist > range || (cfg.require_view && !in_view))) continue;

        if (type == OBJ_CREATURE) {
            char *cr = ((NoArgPtr_t)VT(obj, 0x2c))(obj);
            if (!cr) continue;
            int reaction = F(cr, 0x2dc, unsigned char);   /* 0 neutral, 1 friendly, 2 hostile */
            if (reaction == 1 && !cfg.show_friendly && !is_target && !is_hover) continue;
            if (reaction == 0 && !cfg.show_neutral && !is_target && !is_hover) continue;
            add_creature(obj, cr, cam, cpos, fwd, dist, is_target, is_hover, now);
        } else {
            add_object(client, obj, type, gob, cam, cpos, fwd, dist, is_target, is_hover);
        }
    }
    for (int c = 0; c < 2; c++)
        if (corpse_valid[c] == 2) corpse_alias[c] = sel[c];
}

static void update_chips(float dt)
{
    for (int i = 0; i < MAX_PLATES; i++) {
        Track *t = &tracks[i];
        if (!t->id) continue;
        if (t->hold > 0) t->hold -= dt;
        else if (t->chip > t->hp) t->chip = fmaxf(t->hp, t->chip - dt * 0.9f);
    }
}

static float marker_size(float k) { return floorf(5 * k + 0.5f); }

static void layout(float k)
{
    qsort(plates, nplates, sizeof *plates, cmp_dist);
    float name_h = font_name.line * 0.95f + 2 * k;
    for (int i = 0; i < nplates; i++) {
        Plate *p = &plates[i];
        if (p->kind == KIND_CREATURE) {
            float s = k * (p->target ? cfg.target_mul : 1.0f) *
                      (1.0f - 0.25f * fminf(p->dist / fmaxf(cfg.range, 1), 1));
            float w = cfg.bar_w * s, h = fmaxf(cfg.bar_h * s, 4);
            p->x0 = floorf(p->sx - w / 2);
            p->x1 = p->x0 + floorf(w);
            p->y1 = floorf(p->sy);
            p->y0 = p->y1 - floorf(h);
            p->top = p->y0 - name_h;
        } else {
            float tw = text_w(&font_obj, p->name), m = marker_size(k);
            p->x0 = floorf(p->sx - tw / 2) - 2 * k;
            p->x1 = p->x0 + tw + 4 * k;
            p->y1 = floorf(p->sy - 2 * m - 2 * k);
            p->y0 = p->y1 - font_obj.line;
            p->top = p->y0;
        }
    }
    if (!cfg.stack) return;
    /* WoW-style stacking: a plate overlapping a nearer one is nudged upward (nearest plates keep their spot);
       creature plates and object labels stack separately so loot never shoves an enemy's bar around */
    float gap = 2 * k;
    for (int i = nplates - 1; i >= 0; i--) {
        Plate *p = &plates[i];
        for (int pass = 0; pass < 8; pass++) {
            int moved = 0;
            for (int j = nplates - 1; j > i; j--) {
                Plate *o = &plates[j];
                if (o->kind != p->kind || p->x1 < o->x0 || p->x0 > o->x1) continue;
                if (p->y1 + gap <= o->top || p->top >= o->y1 + gap) continue;
                float d = p->y1 - (o->top - gap);
                p->y0 -= d;
                p->y1 -= d;
                p->top -= d;
                moved = 1;
            }
            if (!moved) break;
        }
    }
}

static void diamond(float cx, float cy, float r, float cr, float cg, float cb, float a)
{
    glColor4f(cr, cg, cb, a);
    glVertex2f(cx, cy - r); glVertex2f(cx + r, cy); glVertex2f(cx, cy + r); glVertex2f(cx - r, cy);
}

/* Object label: a gem marker on top of the model, name above it. Looted containers are grey and faded. */
static void draw_object(Plate *p, float k)
{
    const float *c = p->col;
    float a = p->alpha, m = marker_size(k), cx = floorf(p->sx), cy = floorf(p->sy) - m;
    /* loot sparkle: a slow pulse, like WoW's lootable-corpse glitter */
    float pulse = p->loot ? 0.75f + 0.25f * sinf(anim_t * 4.0f + (p->id & 0xff) * 0.7f) : 1;

    glBegin(GL_QUADS);
    if (p->loot) diamond(cx, cy, m * (1.6f + 0.3f * pulse), c[0], c[1], c[2], 0.25f * a * pulse);   /* halo */
    diamond(cx, cy, m + fmaxf(1, floorf(k)), 0, 0, 0, 0.85f * a);
    diamond(cx, cy, m, fminf(c[0] * 1.15f, 1), fminf(c[1] * 1.15f, 1), fminf(c[2] * 1.15f, 1), a * pulse);
    diamond(cx, cy - m * 0.35f, m * 0.35f, 1, 1, 1, 0.45f * a);                                      /* glint */
    glEnd();

    int lit = p->hover || p->target;
    draw_text(&font_obj, p->name, p->x0 + 2 * k, p->y0, lit ? 1 : c[0], lit ? 1 : c[1], lit ? 1 : c[2],
              lit ? 1 : a, 1);
}

static void draw_plate(Plate *p, float k, int any_target)
{
    if (p->kind == KIND_OBJECT) {
        draw_object(p, k);
        return;
    }
    const float *c = p->reaction == 2 ? COL_HOSTILE : p->reaction == 0 ? COL_NEUTRAL : COL_FRIEND;
    float a = (any_target && !p->target && !p->hover) ? cfg.alpha_other : 1.0f;
    float x0 = p->x0, y0 = p->y0, x1 = p->x1, y1 = p->y1, b = fmaxf(1, floorf(k + 0.25f));
    float fill = x0 + (x1 - x0) * p->hp, chip = x0 + (x1 - x0) * p->chip;
    float lit = p->hover ? 1.25f : 1.0f;

    glBegin(GL_QUADS);
    if (p->target) {                                             /* white selection frame */
        float t = b * 2;
        rect(x0 - b - t, y0 - b - t, x1 + b + t, y0 - b, 1, 1, 1, 0.95f);
        rect(x0 - b - t, y1 + b, x1 + b + t, y1 + b + t, 1, 1, 1, 0.95f);
        rect(x0 - b - t, y0 - b, x0 - b, y1 + b, 1, 1, 1, 0.95f);
        rect(x1 + b, y0 - b, x1 + b + t, y1 + b, 1, 1, 1, 0.95f);
    }
    rect(x0 - b, y0 - b, x1 + b, y1 + b, 0, 0, 0, 0.9f * a);                     /* border */
    rect(x0, y0, x1, y1, 0.06f, 0.06f, 0.07f, 0.85f * a);                         /* empty track */
    if (chip > fill) rect(fill, y0, chip, y1, 1.0f, 0.86f, 0.55f, 0.9f * a);    /* recent damage */
    float mid = y0 + (y1 - y0) * 0.45f;
    rect_grad(x0, y0, fill, mid, c, 1.35f * lit, 1.05f * lit, a);                /* glossy top half */
    rect_grad(x0, mid, fill, y1, c, 0.95f * lit, 0.6f * lit, a);
    rect(x0, y0, fill, y0 + fmaxf(1, floorf(k)), 1, 1, 1, 0.18f * a);            /* top highlight line */
    glEnd();

    Font *fn = &font_name;
    float tw = text_w(fn, p->name);
    float nr = 1, ng = 1, nb = 1;
    if (!p->target) {                                     /* names take a pale tint of the reaction colour */
        nr = 0.75f + 0.25f * c[0];
        ng = 0.75f + 0.25f * c[1];
        nb = 0.75f + 0.25f * c[2];
    }
    draw_text(fn, p->name, (x0 + x1) / 2 - tw / 2, y0 - b - 1 - fn->line * 0.95f, nr, ng, nb, a, 1);

    if (cfg.hp_text && p->target && (y1 - y0) >= font_small.line * 0.8f) {
        char t[16];
        snprintf(t, sizeof t, "%d%%", (int)(p->hp * 100 + 0.5f));
        float w = text_w(&font_small, t);
        draw_text(&font_small, t, (x0 + x1) / 2 - w / 2, (y0 + y1) / 2 - font_small.line / 2, 1, 1, 1, a, 1);
    }
}

static int toggled_off;

static void draw_plates(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = last_qpc.QuadPart ? (float)(now.QuadPart - last_qpc.QuadPart) / qpf.QuadPart : 0;
    last_qpc = now;
    if (dt > 0.25f) dt = 0.25f;
    anim_t += dt;

    static int key_was;
    HDC hdc = wglGetCurrentDC();
    if (hdc) game_wnd = WindowFromDC(hdc);
    int key = cfg.toggle_vk && GetForegroundWindow() == game_wnd && (GetAsyncKeyState(cfg.toggle_vk) & 0x8000);
    if (key && !key_was) toggled_off = !toggled_off;
    key_was = key;

    hit_count = 0;
    if (toggled_off) return;
    static DWORD log_t;
    log_now = cfg.debug && GetTickCount() - log_t > 2000;
    if (log_now) {
        log_t = GetTickCount();
        log_msg("frame:");
    }
    collect();
    update_chips(dt);
    if (!nplates) return;

    float k = gui_h / 1440.0f * cfg.scale;
    layout(k);

    RECT rc;
    GetClientRect(game_wnd, &rc);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    /* the engine leaves lightmap/env units enabled; plates are single-texture */
    static void (APIENTRY *active_texture)(GLenum);
    if (!active_texture) active_texture = (void (APIENTRY *)(GLenum))wglGetProcAddress("glActiveTextureARB");
    for (int u = 3; u >= 0 && active_texture; u--) {
        active_texture(0x84C0 + u); /* GL_TEXTURE0 + u */
        glDisable(GL_TEXTURE_2D);
        glDisable(0x8513); /* GL_TEXTURE_CUBE_MAP */
        glDisable(GL_TEXTURE_GEN_S); glDisable(GL_TEXTURE_GEN_T);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    if (!font_name.tex) {
        make_font(&font_name, (int)(cfg.name_px * k + 0.5f), FW_SEMIBOLD);
        make_font(&font_small, (int)(cfg.bar_h * k * cfg.target_mul * 0.95f + 0.5f), FW_BOLD);
        make_font(&font_obj, (int)(cfg.obj_px * k + 0.5f), FW_SEMIBOLD);
        log_msg("fonts: %s %d/%d px, gui %gx%g, client %ldx%ld", cfg.font, font_name.px, font_small.px, gui_w,
                gui_h, rc.right, rc.bottom);
    }
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0, gui_w, gui_h, 0, -1, 1);
    glMatrixMode(GL_TEXTURE); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glViewport(0, 0, rc.right - rc.left, rc.bottom - rc.top);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(0x8620); /* GL_VERTEX_PROGRAM_ARB */
    glDisable(0x8804); /* GL_FRAGMENT_PROGRAM_ARB */
    glDisable(0x8522); /* GL_REGISTER_COMBINERS_NV */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glShadeModel(GL_SMOOTH);
    glDepthMask(GL_FALSE);
    glColorMask(1, 1, 1, 1);

    int any_target = 0;
    for (int i = 0; i < nplates; i++) any_target |= plates[i].target;
    for (int i = 0; i < nplates; i++) draw_plate(&plates[i], k, any_target);

    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glMatrixMode(GL_TEXTURE); glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();

    /* hit rects, topmost last; the name (and an object's marker) counts as part of the plate */
    char *client = *(char **)(APP_MANAGER + 4);
    for (int i = 0; i < nplates; i++) {
        Plate *p = &plates[i];
        hits[i].id = p->id;
        hits[i].kind = p->kind;
        hits[i].x0 = p->x0 - 3 * k;
        hits[i].x1 = p->x1 + 3 * k;
        hits[i].y0 = p->top;
        hits[i].y1 = p->kind == KIND_OBJECT ? fmaxf(p->y1, p->sy) : p->y1 + 3 * k;
        char *go = GetGameObject(client, p->id);
        char *obj = go ? ((NoArgPtr_t)VT(go, 0xc))(go) : NULL;
        if (obj) memcpy(hits[i].pos, &F(obj, 0x24, float), 12);
    }
    hit_count = nplates;
    if (log_now)
        for (int i = 0; i < nplates; i++)
            log_msg("plate %08x '%s' r%d %d/%d at %.0f,%.0f d=%.1f%s%s", plates[i].id, plates[i].name,
                    plates[i].reaction, plates[i].cur, plates[i].max, plates[i].sx, plates[i].sy, plates[i].dist,
                    plates[i].target ? " T" : "", plates[i].hover ? " H" : "");
}

/* ---------------------------------------------------------------------------------------------- hooks */
static void __fastcall hk_panel_draw(void *panel, void *edx, float dt)
{
    (void)edx;
    if (cfg.enabled) draw_plates();
    PanelDraw(panel, dt);
}

/* The engine's overhead widget: skip its name/health labels when our plate already covers that creature. */
static void __fastcall noop_draw(void *self, void *edx, float dt) { (void)self; (void)edx; (void)dt; }
static void *fake_vt[4][64];
static void *real_vt[4];
static const int MENU_LABELS[4] = {0x184c, 0x170c, 0x15cc, 0x198c};

static void __fastcall hk_target_draw(void *menu, void *edx, float dt)
{
    (void)edx;
    char *client = *(char **)(APP_MANAGER + 4);
    char *internal = *(char **)(client + 4);
    unsigned hover = F(internal, 0x4a4, unsigned), target = F(internal, 0x2b4, unsigned);
    unsigned shown = hover != INVALID_ID && hover ? hover : target;
    int covered = 0;
    if (!toggled_off && cfg.enabled)
        for (int i = 0; i < hit_count; i++)
            if (hits[i].id == shown) covered = 1;
    if (!toggled_off && cfg.enabled && shown && (shown == corpse_alias[0] || shown == corpse_alias[1])) covered = 1;
    if (!covered) {
        TargetMenuDraw(menu, dt);
        return;
    }
    for (int i = 0; i < 4; i++) {
        void ***ctl = (void ***)((char *)menu + MENU_LABELS[i]);
        real_vt[i] = *ctl;
        memcpy(fake_vt[i], *ctl, sizeof fake_vt[i]);
        fake_vt[i][0x38 / 4] = (void *)noop_draw;
        *ctl = fake_vt[i];
    }
    TargetMenuDraw(menu, dt);
    for (int i = 0; i < 4; i++) *(void **)((char *)menu + MENU_LABELS[i]) = real_vt[i];
}

static MouseOver_t mouse_over_orig;
static void __fastcall hk_mouse_over(void *internal, void *edx, unsigned id, float *pos, int flag)
{
    (void)edx;
    if (cfg.enabled && !toggled_off && hit_count && game_wnd) {
        POINT pt;
        RECT rc;
        GetCursorPos(&pt);
        ScreenToClient(game_wnd, &pt);
        GetClientRect(game_wnd, &rc);
        float mx = pt.x * gui_w / fmaxf(1, (float)rc.right), my = pt.y * gui_h / fmaxf(1, (float)rc.bottom);
        for (int i = hit_count - 1; i >= 0; i--) {
            if (mx >= hits[i].x0 && mx <= hits[i].x1 && my >= hits[i].y0 && my <= hits[i].y1) {
                id = hits[i].id;
                pos = hits[i].pos;
                break;
            }
        }
    }
    mouse_over_orig(internal, id, pos, flag);
}

/* The stock Steam exe is SteamStub-packed: its code is still encrypted when DllMain runs and is decrypted by the
   stub before the game starts. DllMain installs only if the code already reads as 1.03 (GOG, de-DRM'd exes);
   otherwise the first DirectInput8Create call (game init, long after the stub ran) does. */
static int installed;

static int code_ready(void)
{
    unsigned char *p = (unsigned char *)0x68b6a7;
    return !IsBadReadPtr(p, 16) && p[0] == 0xE8 && 0x68b6a7 + 5 + *(int *)(p + 1) == 0x40b760;
}

static void install(void)
{
    static const unsigned char mouse_over_prologue[7] = {0x56, 0x8b, 0xf1, 0x8b, 0x4c, 0x24, 0x0c};
    int ok = 1;
    installed = 1;
    if (IsBadReadPtr((void *)0x68b6a7, 16) || IsBadReadPtr((void *)0x6222f0, 16)) {
        log_msg("not a KOTOR 1.03 exe, nothing hooked");
        return;
    }
    ok &= hook_call(0x68b6a7, 0x40b760, (void *)hk_panel_draw);
    ok &= hook_call(0x68b6b3, 0x685ed0, (void *)hk_target_draw);
    mouse_over_orig = (MouseOver_t)hook_entry(0x6222f0, mouse_over_prologue, 7, (void *)hk_mouse_over);
    log_msg("hooks installed: %s, mouse-over %s", ok ? "ok" : "PARTIAL", mouse_over_orig ? "ok" : "skipped");
}

/* ---------------------------------------------------------------------------------------------- proxy */
typedef HRESULT(WINAPI *DI8Create_t)(HINSTANCE, DWORD, const void *, void **, void *);

HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD ver, const void *riid, void **out, void *outer)
{
    static DI8Create_t real;
    if (!installed) install();
    if (!real) {
        char p[MAX_PATH];
        GetSystemDirectoryA(p, MAX_PATH);
        strcat(p, "\\dinput8.dll");
        HMODULE m = LoadLibraryA(p);
        real = m ? (DI8Create_t)GetProcAddress(m, "DirectInput8Create") : NULL;
        if (!real) return E_FAIL;
    }
    return real(inst, ver, riid, out, outer);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    (void)r;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameA(h, dir, MAX_PATH);
    char *s = strrchr(dir, '\\');
    if (s) *s = 0;
    snprintf(log_path, sizeof log_path, "%s\\nameplates.log", dir);
    DeleteFileA(log_path);
    QueryPerformanceFrequency(&qpf);
    load_config();
    log_msg("KOTOR nameplates 1.0 - enabled=%d range=%g", cfg.enabled, cfg.range);
    /* every hook checks the original bytes at its site first and is skipped on a mismatch */
    if (code_ready()) install();
    else log_msg("exe code not readable yet (SteamStub?), hooking at DirectInput init");
    return TRUE;
}
