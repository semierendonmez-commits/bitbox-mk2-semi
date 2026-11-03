/*
 * FX1 / FX2 / FX3 Send as modulation targets (bitbox-mk2-semi, own code). bitbox MK2 2.3.4.
 *
 * Stock, a pad parameter can be modulated when it is in the modulatable list (28 halfwords at 0x080c97cc, searched by
 * the Pos / parameter lists' INFO handler 0x08091e78): the sends are not, so INFO on them opens no MOD page. The list's
 * last five entries repeat earlier ones (0x15 0x16 0x1b 0x1a 0x1d): three of them become FX1 Send (0xbb), FX2 Send
 * (0xbc) and FX3 Send (0x1c8) (patches/fx3.py). The MOD page then sends its routes (message 0x3a: +0xc target, +0x10
 * slot 0..2, +0x14 source code, +0x16 amount x1000) to the pad like any other.
 *
 * The engine's route add (Sample / Granular pads 0x080af648, Clip / Slicer pads 0x080b7500) knows no send target: the
 * source object for a code is picked inside it (pad level: MVOL, MPAN, PTCH, MODW, our ENV, the clip voices' own ...)
 * and stored in the target's modulation object (value fn 0x080a2d9c: sum of slot source (+4) x amount). Ours wraps the
 * call: for a send target it runs the stock route add once with the target Filter (0xaa) on a copy of the message,
 * reads the source object it put into Filter's modulation object (Sample pads +0x2fc, Clip pads +0x448) for that slot,
 * and puts the whole pad object back as it was (that run touches only the pad object); then the real message as stock.
 * Sample pads' per-voice codes (LFO 4, VEL 13, KEY 14: the voice's +0x518 / +0x80 / +0x88) are read from the pad's
 * last playing voice; these routes are also in the pad's own route list (+0x4ec, 12 bytes each, count +0x54c: per-voice
 * routes only), checked every block (a preset change drops them there). Every audio block (from the pads' send check, fx3_check_a / _b) a pad with send routes gets
 * send = its knob / 1000 + sum(amount x source), clamped to 0..1, through the stock send curve (0x080a1868) into its
 * send gains (Sample pads +0x558 / +0x55c, Clip pads +0xb84 / +0xb88; FX3 in fx3.c); without routes the knob's gain
 * comes back.
 */
#include <stdint.h>

#pragma GCC optimize("O2", "no-tree-loop-distribute-patterns")

#define FN(addr) ((addr) | 1u)
typedef void (*route_fn)(uint8_t *pad, const uint8_t *msg);
typedef float (*curve_fn)(float);
#define fw_route_a    ((route_fn)FN(0x080af648))      /* [mk2] Sample / Granular pads' route add */
#define fw_route_b    ((route_fn)FN(0x080b7500))      /* [mk2] Clip / Slicer pads' */
#define fw_send_curve ((curve_fn)FN(0x080a1868))      /* [mk2] send knob 0..1 -> gain */
unsigned pad_setting(const uint8_t *pad, unsigned param);                          /* filter.c */
void *modsrc_pick(unsigned code, const uint8_t *pad);                             /* modsrc.c */

#define T_FX1 0xbb
#define T_FX2 0xbc
#define T_FX3 0x1c8
#define T_FILTER 0xaa
static const uint16_t targets[3] = {T_FX1, T_FX2, T_FX3};

#define SIZE_A 0x5b0
#define SIZE_B 0xba0
#define MP_A   0x2fc              /* Filter's modulation object (0x34 bytes: slots of {source, amount} from +0) */
#define MP_B   0x448
#define GAIN_A 0x558              /* FX1 / FX2 gains (FX2 + 4) */
#define GAIN_B 0xb84
#define LIST_A 0x4ec              /* Sample pads' route list: 8 x {float amount, u16 target, u16 code, u8 slot} */
#define COUNT_A 0x54c
#define VOICES_A 0x2c             /* 16 voice pointers, count +0x6c, a voice plays when its first byte is set */
#define NVOICES_A 0x6c

struct slot {
    const uint8_t *src;       /* source object (value at +4), or 0: a voice code (LFO / VEL / KEY) */
    float amt;
    uint16_t code;            /* 0 = free */
    uint16_t pad;
};
struct ent {
    uint8_t *pad;             /* the pad object, 0 = free */
    uint8_t cls, dirty, n, pad1;
    struct slot s[3][3];      /* [target][slot] */
};
#define NENT 24
struct sm {
    uint32_t magic;
    uint32_t used;            /* entries in use (the thunks' fast path) */
    struct ent e[NENT];
};
_Static_assert(sizeof(struct sm) <= 0xb80, "patch RAM 0x2001c480..0x2001d000");
#define SM ((struct sm *)0x2001c480u)
#define SMAGIC 0x534d4f44u
#define SNAP ((uint32_t *)0x2001f400u)               /* a pad object's copy (Clip pads 0xba0 bytes) */

static struct sm *sm(void)
{
    struct sm *m = SM;
    if (m->magic != SMAGIC) {
        uint32_t *w = (uint32_t *)m;
        for (unsigned k = 0; k < sizeof(struct sm) / 4; k++)
            w[k] = 0;
        m->magic = SMAGIC;
    }
    return m;
}

static struct ent *find(struct sm *m, const uint8_t *pad, int make, int cls)
{
    struct ent *free_e = 0;
    for (int k = 0; k < NENT; k++) {
        if (m->e[k].pad == pad)
            return &m->e[k];
        if (!m->e[k].pad && !free_e)
            free_e = &m->e[k];
    }
    if (!make || !free_e)
        return 0;
    free_e->pad = (uint8_t *)pad;
    free_e->cls = (uint8_t)cls;
    free_e->dirty = 0;
    free_e->n = 0;
    for (int t = 0; t < 3; t++)
        for (int k = 0; k < 3; k++)
            free_e->s[t][k].code = 0;
    m->used++;
    return free_e;
}

static void release(struct sm *m, struct ent *e)
{
    if (e->n || e->dirty)
        return;
    e->pad = 0;
    if (m->used)
        m->used--;
}

static int target_index(unsigned target)
{
    for (int t = 0; t < 3; t++)
        if (targets[t] == target)
            return t;
    return -1;
}

static void copy_words(uint32_t *dst, const uint32_t *src, unsigned bytes)
{
    for (unsigned k = 0; k < bytes / 4; k++)
        dst[k] = src[k];
}

/* the source object the stock route add picks for this message's code, as it would for Filter (0 if none) */
static const uint8_t *pick_source(uint8_t *pad, const uint8_t *msg, int cls, unsigned slot)
{
    unsigned size = cls ? SIZE_B : SIZE_A, mp = cls ? MP_B : MP_A;
    uint32_t copy[6];
    copy_words(copy, (const uint32_t *)msg, 0x18);
    ((uint16_t *)copy)[6] = T_FILTER;                   /* +0xc */
    uint32_t *snap = SNAP;
    copy_words(snap, (const uint32_t *)pad, size);
    *(uint32_t *)(pad + mp + 8 + 8 * slot) = 0;
    (cls ? fw_route_b : fw_route_a)(pad, (const uint8_t *)copy);
    const uint8_t *src = *(const uint8_t *const *)(pad + mp + 8 + 8 * slot);
    copy_words((uint32_t *)pad, snap, size);
    return src;
}

static void route(uint8_t *pad, const uint8_t *msg, int cls)
{
    int t = target_index(*(const uint16_t *)(msg + 0xc));
    unsigned slot = *(const uint32_t *)(msg + 0x10), code = *(const uint16_t *)(msg + 0x14);
    if (t >= 0 && slot < 3) {
        struct sm *m = sm();
        struct ent *e = find(m, pad, code != 0, cls);
        if (e) {
            struct slot *s = &e->s[t][slot];
            if (s->code)
                e->n--;
            s->code = 0;
            if (code) {
                const uint8_t *src = 0;
                if (cls || (code != 4 && code != 13 && code != 14))
                    src = pick_source(pad, msg, cls, slot);
                if (src || (!cls && (code == 4 || code == 13 || code == 14))) {
                    s->src = src;
                    s->amt = (float)*(const int16_t *)(msg + 0x16) * 0.001f;
                    s->code = (uint16_t)code;
                    e->n++;
                }
            }
            if (!e->n)
                e->dirty |= 0x80;                       /* the knob's gain back on the next block */
        }
    }
    (cls ? fw_route_b : fw_route_a)(pad, msg);
}

/* hooks at the pads' route add calls (0x080b2b22, 0x080b8bf4) */
void sendmod_route_a(uint8_t *pad, const uint8_t *msg) { route(pad, msg, 0); }
void sendmod_route_b(uint8_t *pad, const uint8_t *msg) { route(pad, msg, 1); }

static int in_list(const uint8_t *pad, unsigned target, unsigned slot, unsigned code)
{
    unsigned n = *(const uint32_t *)(pad + COUNT_A);
    if (n > 8)
        n = 8;
    for (unsigned k = 0; k < n; k++) {
        const uint8_t *r = pad + LIST_A + 12 * k;
        if (*(const uint16_t *)(r + 4) == target && r[8] == slot && *(const uint16_t *)(r + 6) == code)
            return 1;
    }
    return 0;
}

static float voice_value(const uint8_t *pad, unsigned code)
{
    unsigned n = *(const uint32_t *)(pad + NVOICES_A);
    if (n > 16)
        n = 16;
    unsigned off = code == 4 ? 0x518 : code == 13 ? 0x80 : 0x88;
    for (int k = (int)n - 1; k >= 0; k--) {
        const uint8_t *v = *(const uint8_t *const *)(pad + VOICES_A + 4 * k);
        if (v && *v)
            return *(const float *)(v + off + 4);
    }
    return 0.0f;
}

static float clamp01(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }

/* the modulated send level 0..1 of target t (base = the knob / 1000); has = there are routes */
static float level(struct ent *e, int t, float base, int *has)
{
    float sum = 0.0f;
    int any = 0;
    for (int k = 0; k < 3; k++) {
        struct slot *s = &e->s[t][k];
        if (!s->code)
            continue;
        if (!s->src && !in_list(e->pad, targets[t], (unsigned)k, s->code)) {   /* voice route gone (preset ...) */
            s->code = 0;
            e->n--;
            e->dirty |= (uint8_t)(1u << t);
            continue;
        }
        float v = s->src ? *(const float *)(s->src + 4) : voice_value(e->pad, s->code);
        sum += s->amt * v;
        any = 1;
    }
    *has = any;
    return clamp01(base + sum);
}

/* thunks fx3_check_a / _b (every block of a playing pad, before its send gains are read): cls 0 Sample, 1 Clip */
void sendmod_apply(uint8_t *pad, int cls)
{
    struct sm *m = SM;
    if (m->magic != SMAGIC || !m->used)
        return;
    struct ent *e = find(m, pad, 0, cls);
    if (!e)
        return;
    float *gain = (float *)(pad + (cls ? GAIN_B : GAIN_A));
    for (int t = 0; t < 2; t++) {
        int has;
        float base = (float)(int)pad_setting(pad, targets[t]) * 0.001f;
        float v = level(e, t, clamp01(base), &has);
        if (has) {
            gain[t] = v > 0.0f ? fw_send_curve(v) : 0.0f;
            e->dirty |= (uint8_t)(1u << t);
        } else if (e->dirty & (0x80 | (1u << t))) {
            gain[t] = base > 0.0f ? fw_send_curve(clamp01(base)) : 0.0f;
            e->dirty &= (uint8_t)~(1u << t);
        }
    }
    int has3;
    level(e, 2, 0.0f, &has3);                           /* (FX3: fx3.c asks sendmod_fx3; here only the list check) */
    e->dirty &= (uint8_t)~0x84u;
    if (!e->n)
        release(m, e);
}

/* fx3.c: FX3 Send level 0..1 for this pad (base = its knob / 1000) */
float sendmod_fx3(const uint8_t *pad, float base)
{
    struct sm *m = SM;
    if (m->magic != SMAGIC || !m->used)
        return base;
    for (int k = 0; k < NENT; k++)
        if (m->e[k].pad == pad) {
            int has;
            float v = level(&m->e[k], 2, base, &has);
            return has ? v : base;
        }
    return base;
}

/* fx3.c: any FX3 route on this pad (FX3's send runs even with the knob at 0) */
int sendmod_fx3_on(const uint8_t *pad)
{
    struct sm *m = SM;
    if (m->magic != SMAGIC || !m->used)
        return 0;
    for (int k = 0; k < NENT; k++)
        if (m->e[k].pad == pad)
            for (int j = 0; j < 3; j++)
                if (m->e[k].s[2][j].code)
                    return 1;
    return 0;
}
