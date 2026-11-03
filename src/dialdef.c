/*
 * Double tap on a dial -> its default value (bitbox-mk2-semi, own code). bitbox MK2 2.3.4.
 *
 * The dial widget (class vtable 0x080c94f8, message handler [12] 0x0808fa24) gets 0x164 for a tap (touch and
 * release without moving; it selects the dial: posts 0x165), 0x163 (drag start / end, +0xc 1 / 0) and 0x162 (drag:
 * +0xc = distance). Our handler sits in that vtable slot, calls the stock one and watches the taps: a second tap on
 * the same dial within 350 ms of the first (no drag in between) sets the default value. Only the value changes; modulation slots, other parameters and the dial's
 * page are left alone.
 *
 * Default: the dial knows its cell (+0x32) and parameter (+0x94). The cell gives the record the knob handler
 * (0x0807f7bc) would write to (settings, a pad, an FX row, the master row); the stock record init (0x08089e7c, a
 * switch over the record type, only store adds) is run for every type into a scratch record of ours, and the type
 * whose parameter list matches the record best gives the default (so the FX rows' changing types, our added
 * parameters and older presets with missing entries all work). The change goes out as the dial's own 0x6e message
 * (exactly as a drag sends it) and the dial is redrawn with 0x0808f718.
 */
#include <stdint.h>

#pragma GCC optimize("O2", "no-tree-loop-distribute-patterns")

#define FN(addr) ((addr) | 1u)
typedef int (*msg_fn)(void *w, const void *msg);
typedef void (*init_fn)(void *rec, const uint8_t *type);
typedef void (*setv_fn)(void *dial, unsigned param, int value, int force);
#define fw_dial_msg   ((msg_fn)FN(0x0808fa24))        /* [mk2] stock dial message handler */
#define fw_post       ((msg_fn)FN(0x08061224))        /* [mk2] widget -> parent message */
#define fw_rec_init   ((init_fn)FN(0x08089e7c))       /* [mk2] record reset + defaults for a type */
#define fw_dial_set   ((setv_fn)FN(0x0808f718))       /* [mk2] dial value + position + redraw */
#define MSG_TAG       0x080bff80u

#define APP ((uint8_t *)0x24015170u)

#define DOUBLE_BLOCKS 131u                            /* 350 ms in audio blocks (128 frames at 48 kHz) */

struct dd {
    uint32_t magic;
    uint8_t *dial;            /* last touch-down */
    uint32_t t;
    uint8_t armed, pad[3];
    uint8_t rec[0x20];        /* scratch record (store +0..0xf, routes +0x10..0x17) */
    uint32_t last_type;
    uint32_t blocks;          /* audio blocks (dd_block from master_dist): the UI's clock */
};
#define DD ((volatile struct dd *)0x2001c300u)
#define DD_MAGIC 0x44444631u

static void dd_init(void)
{
    if (DD->magic == DD_MAGIC)
        return;
    volatile uint8_t *p = (volatile uint8_t *)DD;
    for (unsigned k = 0; k < sizeof(struct dd); k++)
        p[k] = 0;
    DD->magic = DD_MAGIC;
}

/* the record a knob message for this cell writes (0x0807f7bc; kind 1 = special cases: none), or 0 */
static uint8_t *cell_record(unsigned cell)
{
    unsigned kind = (cell >> 8) & 0x1f, row = (cell >> 4) & 0xf, col = cell & 0xf;
    if ((cell & ~0x100u) == 0x40)
        return APP + 0x7ed0;
    if (kind == 2 && row == 3 && col == 3)
        return APP + 0x2278;
    if (kind == 2) {
        unsigned idx = cell & 0xff;
        return idx <= 0x1f ? APP + 0x1c00 + idx * 0x30 : 0;
    }
    if (kind == 3)
        return APP + 0x2200 + row * 0x18;
    if (kind == 0) {                                  /* the pads (0x08078e90): 4 rows x 5, else the song record */
        if (!(row & 0xc) && col <= 4)
            return APP + 0x1840 + row * 0xf0 + col * 0x30;
        return APP + 0x7f38;
    }
    return 0;
}

static inline unsigned st_count(const uint8_t *s) { return *(const uint16_t *)(s + 0xa); }
static inline const uint8_t *st_ent(const uint8_t *s) { return *(const uint8_t *const *)(s + 4); }

static int st_find(const uint8_t *s, unsigned id, int *val)
{
    const uint8_t *e = st_ent(s);
    unsigned n = st_count(s);
    if (!e)
        return 0;
    for (unsigned i = 0; i < n; i++)
        if (*(const uint16_t *)(e + 8 * i) == id) {
            if (val)
                *val = *(const int32_t *)(e + 8 * i + 4);
            return 1;
        }
    return 0;
}

static int score(const uint8_t *tmp, const uint8_t *rec)
{
    const uint8_t *e = st_ent(tmp);
    unsigned n = st_count(tmp), m = st_count(rec), hit = 0;
    for (unsigned i = 0; i < n; i++)
        hit += st_find(rec, *(const uint16_t *)(e + 8 * i), 0);
    return 3 * (int)hit - (int)n - (int)m;            /* = hit - misses on both sides */
}

/* default of `param` for the record `rec` (by the best matching record type) */
static int find_default(const uint8_t *rec, unsigned param, int *out)
{
    uint8_t *tmp = (uint8_t *)DD->rec;
    uint8_t t;
    int best = -100000, found = 0, val = 0;
    unsigned first = DD->last_type;
    /* the type that matched last time first: an exact match ends the search */
    for (unsigned k = 0; k <= 0x4a; k++) {
        unsigned type = k == 0 ? first : k;
        if (type < 1 || type > 0x4a || (k && type == first))
            continue;
        t = (uint8_t)type;
        fw_rec_init(tmp, &t);
        if (!st_count(tmp) || !st_find(tmp, param, 0))
            continue;
        int s = score(tmp, rec);
        if (s > best) {
            best = s;
            st_find(tmp, param, &val);
            found = 1;
            DD->last_type = type;
            if (st_count(tmp) == st_count(rec) && s == (int)st_count(rec))
                break;                                /* every entry matches */
        }
    }
    if (found)
        *out = val;
    return found;
}

static void set_default(uint8_t *dial)
{
    unsigned cell = *(uint16_t *)(dial + 0x32), param = *(uint16_t *)(dial + 0x94);
    if (param == 0x17 || param == 0x6d || param == 0x1ba || param == 0)
        return;                                       /* FX type, pad mode, our page switch: never */
    uint8_t *rec = cell_record(cell);
    if (!rec || !st_ent(rec))
        return;
    unsigned sp = param == 0x7d ? 0x7c : param == 0xbe ? 0xbd : param;   /* the knob handler's remaps */
    if (!st_find(rec, sp, 0))
        return;
    int def;
    if (!find_default(rec, sp, &def))
        return;
    if (param == 0xbe)
        def -= 1;
    uint32_t msg[6];
    msg[0] = 0x6e;
    msg[1] = MSG_TAG;
    msg[2] = cell;
    msg[3] = param;
    msg[4] = (uint32_t)def;
    msg[5] = 0;
    fw_post(dial, msg);
    fw_dial_set(dial, param, def, 1);
}

/* every audio block (HAL's uwTick is not a plain ms count on this build) */
void dd_block(void)
{
    if (DD->magic == DD_MAGIC)
        DD->blocks++;
}

int dialdef_msg(uint8_t *dial, const uint8_t *msg)
{
    unsigned cmd = *(const uint16_t *)msg;
    int fire = 0;
    dd_init();
    if (cmd == 0x164) {                               /* a tap (touch and release without a drag) */
        uint32_t now = DD->blocks;
        fire = DD->armed && DD->dial == dial && now - DD->t < DOUBLE_BLOCKS;
        DD->armed = !fire;
        DD->dial = dial;
        DD->t = now;
    } else if (cmd == 0x163 || cmd == 0x162) {        /* a drag: no double tap across it */
        DD->armed = 0;
    }
    int r = fw_dial_msg(dial, msg);
    if (fire)
        set_default(dial);
    return r;
}
