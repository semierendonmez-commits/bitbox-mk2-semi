/*
 * Pad pages: pads 9-16 on the bitbox micro 2.3.4 as a second page of the 2 x 4 pad grid (re/symbols.md, "Seiten-Variante").
 *
 * The engine, the app's pad records and presets already hold 16 pads (rows 0-3 of the framework's 4 x 4 grid); the
 * micro's UI shows rows 0-1 only. Page 1 shows rows 2-3 in the same eight tiles:
 *   - every pad tile carries its real pad address (row << 4 | col) at tile + 0x38; a page switch rewrites them, so
 *     touch, labels and selection compares follow the page by themselves;
 *   - every place that turns a pad address back into a tile (row * 0x680 + col * 0x1a0) gets the page: guards accept
 *     only the current page (C wrappers below, called instead of the stock guarded entry points), row extractions in
 *     the function bodies subtract 2 * page (padpages_thunk.S);
 *   - screen D (the Waveform screen's pad grid) has 16 tile widgets with rows 2-3 hidden; there the rows are swapped
 *     (row ^ 2 * page), no guard needed;
 *   - pages change only by button (left arrow on Pads, the Mixer's field, a second touch on DLY / RVB, the pad picker's
 *     footer); pkA and before also followed the encoder, which jumped back to page 1 (the encoder steps over pads 1-8).
 * Screens (identified from their constructors' layouts, tools/emu_layout.py): A = Effects (8 tiles, DLY / RVB cells,
 * send meters; pk3 wrongly took it for the Pads screen), B = Pads (8 tiles, Keys / Stop; selection arrives as
 * notification 0x63, not 0x112), D = probably the Mixer (8 visible of 16 tiles, four buttons below).
 * The left arrow button (no function on the Pads screen in stock) toggles the page there. The page shows in the title
 * bar on the Pads and Effects screens ("Pads 1-8", "FX 9-16"; the title's snprintf calls in 0x080769e0 are replaced).
 * On the Mixer, one of its four empty text buttons (top left on the device; the UI is turned by 180 degrees) shows the
 * page and toggles it: a text button reports its click to its screen as event 2 carrying the command id at +0x14
 * (0x0805cc74); that field gets PAGE_CMD and the Mixer's input handler is wrapped. pk6 / pk7 put our own text widgets
 * over the Pads panel and the send meters; those were overdrawn by their live neighbours and are gone.
 * Selecting a pad is app command 0x63 (0x08075984: app + 0x14 = selected pad, notification 0x63); 0x111 selects an
 * effect cell (app + 0x28) and was wrongly used up to pk5.
 * On the Effects screen a second touch on the already selected effect cell (DLY / RVB, app + 0x28) toggles the page.
 * The pad picker (the small 2 x 4 grid the slot screens open from their grid icon) follows the page too: its tile
 * addresses are rewritten, its "selected" / "playing" marks get the page (padpages_thunk.S), its tiles show the pad
 * names (5 characters). While a picker of the current screen is open, the left arrow toggles the page (instead of
 * going back). When it opens, or its owner marks a pad, the page follows the marked pad. Its footer stays stock.
 * The encoder's pad stepping (0x08076cec: the next pad in a fixed list of pads 1-8) is replaced by stepping within the
 * current page (pkB on page 2: the selection was not in the list and went to a page-1 pad).
 * Not in this build: MIDI and gate inputs for pads 9-16.
 */
#include <stdint.h>

#define FN(addr) ((addr) | 1u)

struct pmsg {                    /* the stock tile-update message: listener vtable, pad address */
    uint32_t lit;
    uint16_t code;
    uint16_t pad;
};
#define MSG_LIT 0x080bf4ccu

struct cmd {                     /* a command to the app, as the Pads screen posts "select pad" (0x08086678) */
    uint16_t id, pad0;
    uint32_t lit;
    uint16_t code, pad1;
    uint32_t z[3];
};
#define CMD_LIT    0x080bee0cu
#define CMD_SELECT 0x63      /* "select pad" (0x08075984); 0x111 selects an effect cell */

void fx3_init(void);             /* fx3.c */
void fx3_record_ensure(void);

struct note {                    /* a notification from the app to a screen: id, listener, pad address */
    uint16_t id, pad0;
    uint32_t lit;
    uint16_t code;
};

typedef void (*scr_fn)(void *scr, const void *msg);
typedef int (*vis_fn)(void *scr, const void *msg);
#define fw_a_update_body ((scr_fn)FN(0x080862b6))   /* Effects screen: update one tile (after its row/col guard) */
#define fw_a_touch_body  ((scr_fn)FN(0x08086482))   /* Effects screen: tile pressed (after its column guard) */
#define fw_a_notify      ((scr_fn)FN(0x080863a0))   /* Effects screen: notifications (vtable +0x34) */
#define fw_a_input       ((vis_fn)FN(0x080865e4))   /* Effects screen: input events (vtable +0x30) */
#define fw_b_update_body ((scr_fn)FN(0x08084046))   /* Pads screen: update one tile (after its guard) */
#define fw_b_notify      ((scr_fn)FN(0x08084154))   /* Pads screen: notifications (vtable +0x34) */
#define fw_d_update      ((scr_fn)FN(0x08069268))   /* screen D: update one tile (rows 0-3, 16 tiles) */
#define fw_post_cmd      ((scr_fn)FN(0x08060df8))   /* widget: pass a command up to the app */

typedef void (*text_fn)(void *w, const char *text);
#define fw_set_text   ((text_fn)FN(0x0805d1dc))      /* copies the text, then relayouts / redraws (vt+0x3c) */
typedef void (*obj_fn)(void *obj);
struct rect {
    int32_t x, y, w, h;
};
typedef void (*rect_fn)(void *w, const struct rect *r);
#define fw_set_rect   ((rect_fn)FN(0x0805cb36))      /* widget: set rect (non-virtual, as the picker uses it) */
#define MAX_PICKERS   8
#define fw_picker_input  ((vis_fn)FN(0x08073254))     /* pad picker: input events (vtable +0x30) */
#define fw_picker_clear  ((obj_fn)FN(0x080732b0))     /* pad picker: clear all marks */
#define fw_picker_select ((scr_fn)FN(0x080732d6))     /* pad picker: mark the selected pad (msg: lit, address @+4) */
#define PICKER_TILES  0x34                            /* + row * 0x2b0 + col * 0xac, address at +0xa4 */
#define PICKER_ANCHOR 0x634                           /* {x, y} the picker was opened at */
#define PICKER_FOOTER 0x594                           /* text widget below the tiles; hidden (+0x30) when unused */
#define PICKER_FCMD   0x5a8                           /* = footer + 0x14, its click command; -1 = no owner uses it */
#define WIDGET_HIDDEN 0x30
#define WIDGET_DIRTY  0x31
typedef void (*name_fn)(volatile uint8_t *app, const void *msg, char *info);
#define fw_pad_name   ((name_fn)FN(0x080792e8))      /* pad info: file name at +0 (as the tile bodies use it) */
#define UI_MANAGER    (*(uint8_t *volatile *)0x24000dc8u)  /* set at boot (0x08041276) */
#define UI_CONTAINER  0x280                           /* UI manager: the screen container; current screen at +0x34 */
#define WIDGET_PARENT 0x2c
#define SCREEN_ID     0x7fe4                          /* button handler's object: current screen id (2 = Pads) */
#define APP           ((volatile uint8_t *)0x24013cb0u)   /* the app object (as in filter.c) */
#define APP_SELECTED  0x14                            /* app + 0x14: selected pad (0x08075984) */
#define APP_FX_CELL   0x28                            /* app + 0x28: selected effect cell (0x0807b20c) */
#define TEXT_VT       0x080bfc9cu                     /* text widget vtable */
#define TEXT_CMD      0x14                            /* text widget: command id its click reports (event 2, +0xc) */
#define PAGE_CMD      0x7f01u                         /* our command id: no app command (the dispatcher ends at 0x181) */
#define MIXER_TOGGLE  0x2ae8                          /* Mixer screen: text button {161, 186, 158, 35} */
#define fw_d_input    ((vis_fn)FN(0x080691b0))        /* Mixer screen: input events (vtable +0x30) */
typedef int (*snprintf_fn)(char *buf, unsigned size, const char *fmt, ...);
#define fw_snprintf   ((snprintf_fn)FN(0x080bcd58))

/* State above the stack top, next to Fidelity's PARK (0x2001b100..0x2001b10b); MPU device memory, aligned accesses. */
struct pstate {
    uint32_t magic;
    uint8_t page;                /* 0: pads 1-8, 1: pads 9-16; read by the thunks at +4 */
    uint8_t fx3;                 /* Effects screen: its right cell is FX3 (0x330) instead of RVB (0x310); read by the
                                    send-id thunks at +5 */
    uint8_t busy;                /* inside a page switch */
    uint8_t pad0;
    uint16_t last_sel;           /* last pad selected by a page toggle */
    uint16_t pad1;
    uint8_t *inst[3];            /* screen objects: A (Effects), B (Pads), D (Mixer?) */
    uint8_t *ind2;               /* page label / toggle on the Mixer screen */
    uint8_t *picker[MAX_PICKERS];/* pad pickers */
    uint16_t psel[MAX_PICKERS];  /* the pad each picker's owner last asked to mark (0xffff: none) */
};
#define ST     ((volatile struct pstate *)0x2001b110u)
_Static_assert(sizeof(struct pstate) <= 0xb0, "pad pages state must end before the load meter at 0x2001b1c0");
#define PMAGIC 0x50414745u       /* "PAGE" */
enum { SCR_A, SCR_B, SCR_D };

_Static_assert(sizeof(struct pstate) == 0x4c, "thunks read page at +4");

static unsigned page(void)
{
    return ST->magic == PMAGIC ? ST->page & 1u : 0u;
}

static unsigned row_of(unsigned code) { return (code >> 4) & 0xfu; }
static unsigned col_of(unsigned code) { return code & 0xfu; }

/* on the current page: column 0-3, row 2 * page or 2 * page + 1 */
static int on_page(unsigned code)
{
    return col_of(code) < 4 && (row_of(code) >> 1) == page();
}


static const char *page_label(void)
{
    return page() ? "Pads 9-16" : "Pads 1-8";
}

static void toggle(void *from);
static int picker_open(void);

/* The Mixer's toggle: one of its four empty text buttons (only if it is the text widget we expect). Its click now
   reports PAGE_CMD, which padpages_d_input takes. */
static void make_mixer_toggle(uint8_t *scr)
{
    uint8_t *w = scr + MIXER_TOGGLE;
    if (*(uint32_t *)w != TEXT_VT)
        return;
    *(uint32_t *)(w + TEXT_CMD) = PAGE_CMD;
    ST->ind2 = w;
    fw_set_text(w, page_label());
}

/* --- constructors: remember the screen objects (padpages_thunk.S calls this after each stock constructor) ------- */
void padpages_ctor(unsigned which, uint8_t *scr)
{
    if (ST->magic != PMAGIC) {
        ST->inst[0] = ST->inst[1] = ST->inst[2] = 0;
        ST->ind2 = 0;
        for (unsigned i = 0; i < MAX_PICKERS; i++)
            ST->picker[i] = 0;
        ST->magic = PMAGIC;
    }
    ST->page = 0;                /* a fresh screen has the stock ids: pads 1-8 */
    ST->fx3 = 0;
    ST->busy = 0;
    ST->last_sel = 0xffff;
    if (which < 3)
        ST->inst[which] = scr;
    if (which == SCR_B)          /* the UI manager builds the Pads screen before every screen that owns a pad picker */
        for (unsigned i = 0; i < MAX_PICKERS; i++)
            ST->picker[i] = 0;
    if (which == SCR_D)
        make_mixer_toggle(scr);
    if (which == SCR_A)
        fx3_init();
}

/* --- guards: replace the stock guarded entries at their call sites ------------------------------------------------ */
void padpages_a_update(void *scr, const struct pmsg *m)          /* for 0x080862a4 */
{
    if (on_page(m->code))
        fw_a_update_body(scr, m);
}

void padpages_a_touch(void *scr, const struct pmsg *m)           /* for 0x08086478 */
{
    if (on_page(m->code))
        fw_a_touch_body(scr, m);
}

void padpages_b_update(void *scr, const struct pmsg *m)          /* for 0x08084034 */
{
    if (on_page(m->code))
        fw_b_update_body(scr, m);
}

int padpages_b_visible(void *scr, const struct pmsg *m)          /* for 0x08084138: also no layer bits */
{
    (void)scr;
    return (m->code & 0x1f00u) == 0 && on_page(m->code);
}

/* --- page switch ------------------------------------------------------------------------------------------------------ */
static volatile uint16_t *tile_id(uint8_t *scr, unsigned base, unsigned row, unsigned col)
{
    return (volatile uint16_t *)(scr + base + row * 0x680u + col * 0x1a0u + 0x38u);
}

/* Update the tiles of one screen with the current page's pads (D: all 16, its rows are swapped). */
static void refresh(unsigned which)
{
    uint8_t *scr = ST->inst[which];
    struct pmsg m = {MSG_LIT, 0, 0};
    if (!scr)
        return;
    for (unsigned r = 0; r < (which == SCR_D ? 4u : 2u); r++)
        for (unsigned c = 0; c < 4; c++) {
            m.code = (uint16_t)(((which == SCR_D ? r : 2u * page() + r) << 4) | c);
            if (which == SCR_A)
                fw_a_update_body(scr, &m);
            else if (which == SCR_B)
                fw_b_update_body(scr, &m);
            else
                fw_d_update(scr, &m);
        }
}

/* --- pad picker --------------------------------------------------------------------------------------------------- */
/* A pad's name for a small tile: the file name without its extension, cut to fit 64 pixels. */
static void pad_name(unsigned code, char *out, unsigned size)
{
    char info[0x60];
    for (unsigned i = 0; i < sizeof info; i++)
        info[i] = 0;
    *(uint32_t *)(info + 0x48) = 0xffff;             /* as the tile bodies prepare it (0x08084046) */
    struct pmsg m = {MSG_LIT, (uint16_t)code, 0};
    fw_pad_name(APP, &m, info);
    unsigned n = 0;
    while (n + 1 < size && info[n] && info[n] != '.')       /* size 6: five characters fit a 64-pixel tile */
        out[n] = info[n], n++;
    out[n] = 0;
}

/* The picker's tiles get the page's addresses and names; the owner's mark is redone. */
static void picker_page(unsigned i)
{
    uint8_t *pk = ST->picker[i];
    unsigned p = page();
    for (unsigned r = 0; r < 2; r++)
        for (unsigned c = 0; c < 4; c++) {
            uint8_t *t = pk + PICKER_TILES + r * 0x2b0u + c * 0xacu;
            unsigned code = ((2u * p + r) << 4) | c;
            char name[6];
            *(volatile uint16_t *)(t + 0xa4) = (uint16_t)code;
            pad_name(code, name, sizeof name);
            fw_set_text(t, name);
        }
    fw_picker_clear(pk);
    uint16_t sel = ST->psel[i] != 0xffff ? ST->psel[i] : *(volatile uint16_t *)(APP + APP_SELECTED);
    struct pmsg m = {MSG_LIT, sel, 0};
    fw_picker_select(pk, &m);
}

/* The Pads screen's encoder lists (8 x {MSG_LIT, pad address}: row-wise 0x24021dc8, column-wise 0x24021e08; filled
   once at boot by 0x080847c4 with pads 1-8, read only by the screen: 0x080840c4 finds the selected pad in them,
   0x0808438c turns the encoder's index into the pad to select). Their rows follow the page. */
#define ENC_LISTS 0x24021dc8u
static void enc_lists(unsigned p)
{
    for (unsigned i = 0; i < 16; i++) {
        volatile uint16_t *a = (volatile uint16_t *)(ENC_LISTS + 8u * i + 4u);
        *a = (uint16_t)((*a & ~0x20u) | (p << 5));
    }
}

static void set_page(unsigned p)
{
    p &= 1u;
    ST->page = (uint8_t)p;
    enc_lists(p);
    for (unsigned r = 0; r < 2; r++)
        for (unsigned c = 0; c < 4; c++) {
            uint16_t id = (uint16_t)(((2u * p + r) << 4) | c);
            if (ST->inst[SCR_A])
                *tile_id(ST->inst[SCR_A], 0x34, r, c) = id;
            if (ST->inst[SCR_B])
                *tile_id(ST->inst[SCR_B], 0x34, r, c) = id;
        }
    if (ST->inst[SCR_D])
        for (unsigned r = 0; r < 4; r++)
            for (unsigned c = 0; c < 4; c++)
                *tile_id(ST->inst[SCR_D], 0x3ac, r, c) = (uint16_t)(((r ^ (2u * p)) << 4) | c);
    refresh(SCR_A);
    refresh(SCR_B);
    refresh(SCR_D);
    if (ST->ind2)
        fw_set_text(ST->ind2, page_label());
    for (unsigned i = 0; i < MAX_PICKERS; i++)
        if (ST->picker[i])
            picker_page(i);
}

static void select_pad(void *scr, unsigned code)
{
    struct cmd c = {CMD_SELECT, 0, CMD_LIT, (uint16_t)code, 0, {0, 0, 0}};
    fw_post_cmd(scr, &c);
}

/* --- Pads screen (B): notifications; on page 1 "all new" refills the tiles with pads 9-16 ---------------------------- */
void padpages_b_notify(void *scr, const struct note *n)
{
    fw_b_notify(scr, n);
    if (n->id == 0x67 && ST->magic == PMAGIC && !ST->busy && page())
        refresh(SCR_B);
}

/* --- Effects screen (A): a second touch on the selected right cell switches it RVB <-> FX3 (pages: left arrow) ----- */
#define FX_RVB   0x310
#define FX_FX3   0x330                                 /* FX cell row 3 (row 2, 0x320, is the master EQ) */
#define RVB_TILE 0xed4                                 /* the right cell (button, id at +0x38, label 0x0805dcdc) */
typedef void (*label_fn)(void *tile, const char *name);
#define fw_tile_label ((label_fn)FN(0x0805dcdc))
#define CMD_FX_CELL 0x111                              /* "select effect cell" (0x0807b20c: app + 0x28) */

static void fx3_switch(uint8_t *scr, unsigned on)
{
    fx3_record_ensure();
    ST->fx3 = (uint8_t)on;
    uint8_t *tile = scr + RVB_TILE;
    *(volatile uint16_t *)(tile + 0x38) = (uint16_t)(on ? FX_FX3 : FX_RVB);
    fw_tile_label(tile, on ? "FX3" : "RVB");
    struct cmd c = {CMD_FX_CELL, 0, CMD_LIT, (uint16_t)(on ? FX_FX3 : FX_RVB), 0, {0, 0, 0}};
    fw_post_cmd(scr, &c);
    refresh(SCR_A);                                    /* tiles: second send indicator */
    unsigned sel = *(volatile uint16_t *)(scr + 0x1bd4);       /* the screen's selected pad: displays and knob */
    if ((sel & 0x1f00u) == 0 && col_of(sel) < 4 && row_of(sel) < 4) {
        struct pmsg m = {MSG_LIT, (uint16_t)sel, 0};
        fw_a_touch_body(scr, &m);
    }
}

int padpages_a_input(void *scr, const struct note *n)
{
    unsigned cell = *(volatile uint16_t *)(APP + APP_FX_CELL);
    if (n->id == 3 && ST->magic == PMAGIC && (n->code == FX_RVB || n->code == FX_FX3) && n->code == cell) {
        fx3_switch(scr, n->code == FX_RVB);
        return 0;
    }
    return fw_a_input(scr, n);
}

/* padpages_thunk.S: the Effects screen's FX2 send id (0xbc) at five places -> FX3 Send (0x1c8) while FX3 is shown */

void padpages_a_notify(void *scr, const struct note *n)
{
    fw_a_notify(scr, n);
    if (n->id == 0x67 && ST->magic == PMAGIC && !ST->busy && page())
        refresh(SCR_A);
}

/* --- toggling: left arrow on the Pads screen, the two toggle widgets ------------------------------------------------ */
/* Flip the page and select the pad at the same place on the other page (rows 0 <-> 2, 1 <-> 3); `from` is the widget
   the select command is passed up from. */
static void toggle(void *from)
{
    if (ST->magic != PMAGIC || ST->busy)
        return;
    unsigned sel = *(volatile uint16_t *)(APP + APP_SELECTED);
    ST->busy = 1;
    set_page(page() ^ 1u);
    ST->busy = 0;
    if (from && (sel & 0x1f00u) == 0 && col_of(sel) < 4 && row_of(sel) < 4) {
        unsigned target = sel ^ 0x20u;
        ST->last_sel = (uint16_t)target;
        select_pad(from, target);
    }
}

/* padpages_thunk.S: left_arrow, at the button handler 0x080839e6. Pads screen: toggle and select the pad at the same
   place; an open pad picker of the current screen: toggle (its back function is dropped then). Returns 1 if taken. */
int master_left(uint8_t *app);         /* master.c */

int padpages_left(uint8_t *obj)
{
    if (master_left(obj))                                      /* Tools screen: Compressor / settings pages */
        return 1;
    if (ST->magic != PMAGIC)
        return 0;
    if (obj[SCREEN_ID] == 2) {
        toggle(ST->inst[SCR_B]);
        return 1;
    }
    if (obj[SCREEN_ID] == 0x30 && ST->inst[SCR_A]) {          /* Effects screen */
        toggle(ST->inst[SCR_A]);
        return 1;
    }
    if (picker_open()) {
        toggle(0);
        return 1;
    }
    return 0;
}

/* --- encoder: next / previous pad within the current page (replaces 0x08076cec at 0x0807ca30 / 0x0807ca56) --------- */
void padpages_step(struct pmsg *out, void *app, const struct pmsg *sel, int dir)
{
    (void)app;
    unsigned p = page(), code = sel->code;
    int idx;
    if ((code & 0x1f00u) == 0 && col_of(code) < 4 && (row_of(code) >> 1) == p)
        idx = (int)((row_of(code) - 2u * p) * 4u + col_of(code));
    else
        idx = dir > 0 ? -1 : 0;                      /* from outside the page: first / last pad of the page */
    idx = (idx + dir) % 8;
    if (idx < 0)
        idx += 8;
    out->lit = MSG_LIT;
    out->code = (uint16_t)(((2u * p + (unsigned)idx / 4u) << 4) | ((unsigned)idx % 4u));
}

/* --- Mixer (D): its toggle's click (event 2 with PAGE_CMD) --------------------------------------------------------- */
int padpages_d_input(void *scr, const struct note *n)
{
    if (n->id == 2 && *(const uint32_t *)((const uint8_t *)n + 0xc) == PAGE_CMD) {
        toggle(scr);
        return 0;
    }
    return fw_d_input(scr, n);
}

/* --- title bar: replaces snprintf(buf, 0x18, "Pads") / ("FX") in 0x080769e0 --------------------------------------- */
int padpages_title(char *buf, unsigned size, const char *name)
{
    return fw_snprintf(buf, size, "%s %s", name, page() ? "9-16" : "1-8");
}

/* Constructor wrapper (padpages_thunk.S: ctor_picker): remember the picker. */
void padpages_picker(uint8_t *pk)
{
    if (ST->magic != PMAGIC) {
        ST->inst[0] = ST->inst[1] = ST->inst[2] = 0;
        ST->ind2 = 0;
        for (unsigned i = 0; i < MAX_PICKERS; i++)
            ST->picker[i] = 0;
        ST->page = 0;
        ST->magic = PMAGIC;
    }
    unsigned i = 0;
    while (i < MAX_PICKERS && ST->picker[i] && ST->picker[i] != pk)
        i++;
    if (i < MAX_PICKERS) {
        ST->picker[i] = pk;
        ST->psel[i] = 0xffff;
    }
}

static int picker_is_open(const uint8_t *pk)
{
    uint8_t *ui = UI_MANAGER;
    if (!ui || pk[WIDGET_HIDDEN])
        return 0;
    return *(uint8_t *volatile *)(pk + WIDGET_PARENT) == *(uint8_t *volatile *)(ui + UI_CONTAINER + 0x34);
}

/* Show the page of a marked pad (a grid pad on the other page): returns 1 if the page was switched (set_page then
   brought every picker to it). */
static int follow_mark(unsigned code)
{
    if (code == 0xffff || (code & 0x1f00u) != 0 || col_of(code) >= 4 || row_of(code) >= 4)
        return 0;
    unsigned want = row_of(code) >> 1;
    if (want == page() || ST->busy)
        return 0;
    ST->busy = 1;
    set_page(want);
    ST->busy = 0;
    return 1;
}

static int picker_index(const uint8_t *pk)
{
    for (unsigned i = 0; i < MAX_PICKERS; i++)
        if (ST->picker[i] == pk)
            return (int)i;
    return -1;
}

/* Replaces the last call in the picker's placing (0x08073564: set_rect(footer, {x, 0, 264, 64})), so every time the
   picker opens, its tiles, names and footer are brought to the current page. */
void padpages_picker_place(uint8_t *f, const struct rect *r)
{
    fw_set_rect(f, r);
    int i = picker_index(f - PICKER_FOOTER);
    if (i < 0)
        return;
    if (!follow_mark(ST->psel[i]))
        picker_page((unsigned)i);
}

/* Replaces the owners' calls of the picker's "mark selected" (0x080732d6): remember the pad; if the picker is open on
   the current screen and the pad is on the other page, go there; then mark it. */
void padpages_picker_select(uint8_t *pk, const struct pmsg *m)
{
    int i = picker_index(pk);
    if (i >= 0) {
        ST->psel[i] = m->code;
        if (picker_is_open(pk))
            follow_mark(m->code);
    }
    fw_picker_select(pk, m);
}

/* Is a pad picker of the current screen open? (its parent is the current screen and it is not hidden) */
static int picker_open(void)
{
    for (unsigned i = 0; i < MAX_PICKERS; i++)
        if (ST->picker[i] && picker_is_open(ST->picker[i]))
            return 1;
    return 0;
}

/* Picker input (vtable +0x30): its toggle's click flips the page and keeps the picker open. */
int padpages_picker_input(void *pk, const struct note *n)
{
    if (n->id == 2 && *(const uint32_t *)((const uint8_t *)n + 0xc) == PAGE_CMD) {
        toggle(0);
        return 0;
    }
    return fw_picker_input(pk, n);
}
