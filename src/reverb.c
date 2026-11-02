/* ported to the bitbox MK2 2.3.4 by tools/port_mk2.py */
/*
 * Reverb types for the FX2 reverb cell (portkit): Plate (stock), Room, Hall, Chamber.
 *
 * Stock: the reverb object (ctor 0x080aacdc, process 0x080aa870, engine id 0x30100 = FX layer 3, row 1) runs a Dattorro
 * plate at 29.8 kHz: it reads its settings, resamples the send down, applies the pre-delay, calls the core
 * 0x080ab0a4(state, in, outL, outR, n) (bl at 0x080aaaf2), resamples back up and applies the wet level. Only that core
 * call is replaced (as in blackbox-mod's src/fx2.c for the Blackbox): every new type gets settings, resampling,
 * pre-delay, run-on and level from the stock code and turns a mono block into a stereo block at 29.8 kHz.
 * Decay arrives in state[0] (= Decay x 0.93), Damping in state[1] (0..1); the micro's plate state has the Blackbox's
 * layout: 12 buffers at state words 8 .. 43 (4 x 512, 2 x 1024, 6 x 8192 floats, re/symbols.md "Reverb").
 *
 * Type is the unused list 0x178 ("FX2": Delay / Reverb, registered at 0x0806bc8e, no other use; the global engine
 * handler 0x080a7e1c ignores it), re-registered as "Type:" and put first into the reverb cell's record (type 0x44,
 * defaults at 0x08089958). The other settings (0x191 .. 0x19b, above the engine's range) follow the stock three, so the
 * cell's settings pages (built from the record in order, 0x08088544) show them; with Type = Plate they are hidden
 * (clip_page, src/clipshift.c). Stock firmware ignores the new values and plays its plate.
 *
 * Hall / Chamber: my own design after the published Griesinger / Dattorro tank ("Effect Design Part 1", J. Dattorro,
 * JAES 1997), not Lexicon code: input band limits, four series input diffusers, early reflections, and a figure-eight
 * tank of two branches (modulated allpass -> delay -> two-band decay (bass multiplier at a crossover, treble damping)
 * -> allpass -> delay), each feeding the other, with decorrelated output taps per side. Delays are power-of-two rings,
 * so Size changes the read distance only (slewed per block, no clicks).
 * Room: blackbox-mod's 8-line FDN (Copyright (c) 2026 Justin Joe, MIT; see THIRD_PARTY.md), with the new controls.
 */
#include <stdint.h>

#pragma GCC optimize("O2", "no-tree-loop-distribute-patterns")

#define FN(addr) ((addr) | 1u)

typedef void (*core_fn)(float *state, const float *in, float *l, float *r, int n);
typedef void (*register_list_fn)(void *table, int param, const char *label, const char *const *names, int count,
                                 const char *xml);
typedef void (*register_knob_fn)(void *table, int param, int type, const char *label, int min, int max,
                                 const char *xml);
typedef void (*store_add_fn)(void *store, unsigned param, int value);
typedef int (*store_get_fn)(const void *store, unsigned param);
#define fw_plate_core    ((core_fn)FN(0x080ab0a4))
#define fw_register_list ((register_list_fn)FN(0x0806a27c))
#define fw_register_knob ((register_knob_fn)FN(0x0806a2d4))
#define fw_store_add     ((store_add_fn)FN(0x080893d6))
int fast_get(const void *store, unsigned param);       /* fastget.c: 0x08089388 with a cache */
#define fw_store_get fast_get

#define APP_FX     (0x24015170u + 0x2200u)     /* app: FX cell records, 0x18 each, by row (0x0807f8da) */
#define OBJ_ID     0x18                        /* engine object id: 3 << 16 | row << 8 (reverb 0x30100) */
#define FS         29800.0f

#define P_TYPE   0x178
#define P_SIZE   0x191
#define P_DIFF   0x192
#define P_BASS   0x193
#define P_XOVER  0x194
#define P_RATE   0x195
#define P_DEPTH  0x196
#define P_EARLY  0x197
#define P_WIDTH  0x198
#define P_HICUT  0x199
#define P_LOCUT  0x19a
#define P_LEVEL  0x19b             /* output level -12 .. +12 dB (500 = 0 dB), every type, Plate too */

enum { ALG_PLATE, ALG_ROOM, ALG_HALL, ALG_CHAMBER, ALGS };
/* Type list (semi): every new type has a second page of settings, "Hall >" (the FX cell page shows 8 entries) */
#define TYPE_ITEMS 7
static const char *const type_names[TYPE_ITEMS] = {"Plate", "Room", "Room >", "Hall", "Hall >", "Chamber",
                                                   "Chmbr >"};
static const uint8_t type_alg[TYPE_ITEMS] = {ALG_PLATE, ALG_ROOM, ALG_ROOM, ALG_HALL, ALG_HALL, ALG_CHAMBER,
                                             ALG_CHAMBER};
static unsigned alg_of(unsigned item)
{
    return item < TYPE_ITEMS ? type_alg[item] : ALG_PLATE;
}

struct pdef { uint16_t id; uint16_t def; const char *label, *xml; };
static const struct pdef pdefs[] = {
    {P_SIZE, 500, "Size:", "rvbsize"},
    {P_DIFF, 700, "Diffusion:", "rvbdiff"},
    {P_BASS, 500, "Bass Mult:", "rvbbass"},
    {P_XOVER, 400, "Crossover:", "rvbxover"},
    {P_RATE, 300, "Mod Rate:", "rvbmodrate"},
    {P_DEPTH, 400, "Mod Depth:", "rvbmoddepth"},
    {P_EARLY, 300, "Early:", "rvbearly"},
    {P_WIDTH, 1000, "Width:", "rvbwidth"},
    {P_HICUT, 800, "Hi Cut:", "rvbhicut"},
    {P_LOCUT, 100, "Lo Cut:", "rvblocut"},
    {P_LEVEL, 500, "Level:", "rvblevel"},
};
#define NPARAMS ((int)(sizeof pdefs / sizeof pdefs[0]))

/* Replaces the registration of 0x178 (bl at 0x0806bc8e). Knobs: type 8, 0 .. 1000 (shown as %). */
void reverb_register(void *table, int param, const char *label, const char *const *names, int count, const char *xml)
{
    (void)label;
    (void)names;
    (void)count;
    fw_register_list(table, param, "Type:", type_names, TYPE_ITEMS, xml);
    for (int k = 0; k < NPARAMS; k++)
        fw_register_knob(table, pdefs[k].id, 8, pdefs[k].label, 0, 1000, pdefs[k].xml);
}

/* Reverb cell record (type 0x44): Type before the stock "Decay 600" (bl at 0x08089960) ... */
void reverb_defaults_first(void *store, unsigned param, int value)
{
    fw_store_add(store, P_TYPE, ALG_PLATE);
    fw_store_add(store, param, value);
}

/* ... and the new settings after its last default, "Damping 500" (bl at 0x08089976). */
void reverb_defaults_last(void *store, unsigned param, int value)
{
    fw_store_add(store, param, value);
    for (int k = 0; k < NPARAMS; k++)
        fw_store_add(store, pdefs[k].id, pdefs[k].def);
}

/* --- helpers ------------------------------------------------------------------------------------------------------- */
static float exp2_fast(float x)
{
    if (x < -60.0f)
        return 0.0f;
    if (x > 60.0f)
        x = 60.0f;
    int i = (int)x - (x < 0.0f && (float)(int)x != x);
    float f = x - (float)i, p = 1.0f + f * (0.6951f + f * (0.2262f + f * 0.0782f));
    union { float f; uint32_t u; } v = {p};
    v.u += (uint32_t)i << 23;
    return v.f;
}

/* sine of a 32-bit phase (one cycle = 2^32), parabolic, error < 0.1 % after the correction */
static inline __attribute__((always_inline)) float sin32(uint32_t ph)
{
    float x = (float)(int32_t)ph * (1.0f / 2147483648.0f);           /* -1 .. 1 = -pi .. pi */
    float y = 4.0f * x * (1.0f - (x < 0.0f ? -x : x));
    return y * (0.775f + 0.225f * (y < 0.0f ? -y : y));
}

/* one-pole coefficient for a corner frequency at FS */
static float onepole(float hz)
{
    float w = 6.2831853f * hz / FS;
    return w / (1.0f + w);
}

static float clamp01(float x)
{
    return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

/* the plate's buffers: state word index, floats */
static const struct { uint8_t word; uint16_t len; } bufs[12] = {
    {0x08, 512}, {0x0b, 512}, {0x0e, 512}, {0x11, 512}, {0x14, 1024}, {0x19, 1024},
    {0x1e, 8192}, {0x21, 8192}, {0x25, 8192}, {0x27, 8192}, {0x29, 8192}, {0x2b, 8192},
};
#define BUF(state, i) (*(float **)((uint32_t *)(state) + bufs[i].word))

/* settings of the cell, all 0..1 (Type as index) */
struct settings {
    unsigned type;
    float size, diff, bass, xover, rate, depth, early, width, hicut, locut, level;
};

static void read_record(const void *rec, struct settings *s)
{
    s->type = alg_of((unsigned)fw_store_get(rec, P_TYPE));
    float *dst[NPARAMS] = {&s->size, &s->diff, &s->bass, &s->xover, &s->rate, &s->depth, &s->early, &s->width,
                           &s->hicut, &s->locut, &s->level};
    for (int k = 0; k < NPARAMS; k++)
        *dst[k] = clamp01((float)fw_store_get(rec, pdefs[k].id) * 0.001f);
}

static void read_settings(const uint8_t *obj, struct settings *s)
{
    s->type = ALG_PLATE;
    s->level = 0.5f;
    if (!obj)
        return;
    uint32_t id = *(const uint32_t *)(obj + OBJ_ID);
    unsigned row = (id >> 8) & 0xff;
    if ((id >> 16) != 3 || row >= 5)
        return;
    read_record((const void *)(APP_FX + row * 0x18u), s);
}

/* --- state (patch RAM, device memory: copied in / out once per block) ------------------------------------------------ */
struct rstate {
    uint32_t magic, alg;
    /* hall / chamber */
    uint32_t w8k, w4k, w1k, w512;      /* write positions of the 8192 / 4096 / 1024 / 512 rings */
    float lp_a[2], lp_b[2];            /* crossover low parts / treble low-pass per branch: [0] crossover, [1] treble */
    float fb_a, fb_b;                  /* branch ends (feed the other branch) */
    float hc, lc;                      /* input hi-cut / lo-cut states */
    float size;                        /* slewed size factor */
    uint32_t lfo, wander;              /* modulation phases */
    /* room */
    uint32_t er_pos;
    uint16_t in_ap_pos[4], line_pos[8];
    uint8_t line_ap_pos[8];
    float rlp[8];
    uint32_t rlfo[4];
};
#define RS1 ((volatile struct rstate *)0x2406cc00u)    /* reverb cell; after the WSOLA grain states (..0x2406c9ff) */
#define RS2 ((volatile struct rstate *)0x2406cd00u)    /* FX3 (src/fx3.c) */
_Static_assert(sizeof(struct rstate) <= 0x100, "reverb state must fit its 0x100 slot (tests/test_ram.py)");
#define RMAGIC 0x52564231u             /* "RVB1" */
_Static_assert(sizeof(struct rstate) <= 0x100, "reverb state fits its patch RAM slot");

static void clear(float *state, volatile struct rstate *RS)
{
    for (int i = 0; i < 12; i++) {
        float *b = BUF(state, i);
        if (b)
            for (int k = 0; k < bufs[i].len; k++)
                b[k] = 0.0f;
    }
    volatile uint32_t *w = (volatile uint32_t *)RS;
    for (unsigned k = 0; k < sizeof(struct rstate) / 4; k++)
        w[k] = 0;
    RS->size = 1.0f;
    for (int k = 0; k < 4; k++)
        RS->rlfo[k] = (uint32_t)k * 0x40000000u;
}

/* Make-up gains: noise in, Decay 40 / 70 %, steady-state RMS against the stock plate (tests/test_reverb.py): Hall /
   Chamber were ~3.2 dB, Room ~9 dB quieter. */
#define TANK_GAIN 1.45f
#define ROOM_GAIN 2.8f

/* --- Hall / Chamber -------------------------------------------------------------------------------------------------- */
/* Lengths at 29.8 kHz (Dattorro's figures at 29.761 kHz). Rings: input diffusers in buffers 0-3 (512), modulated
   allpasses in 4-5 (1024), the four delays in 7-10 (8192), the second allpasses in 11 (two halves of 4096), early
   reflections in 6 (8192). */
struct tank {
    float in_ap[4];                    /* input diffuser lengths */
    float map_a, map_b;                /* modulated allpasses */
    float d1, ap2, d2;                 /* branch A after its modulated allpass */
    float d3, ap4, d4;                 /* branch B */
    float rt_min, rt_log2_range;       /* RT60 = rt_min x 2 ^ (rt_log2_range x decay) */
    float er_scale;
};
/* Hall: Dattorro's tank, RT60 0.3 .. 12 s; Chamber: shorter, denser lines, RT60 0.2 .. 5 s */
static const struct tank hall = {{142, 107, 379, 277}, 672, 908, 4453, 1800, 3720, 4217, 2656, 3163, 0.3f, 5.3219f, 1.0f};
static const struct tank chamber = {{142, 107, 379, 277}, 520, 700, 2300, 1100, 1900, 2150, 1500, 1650, 0.2f, 4.6439f,
                                    0.6f};

/* output taps (Dattorro's, on the Hall's lines; other tanks scale them by their line lengths): line, distance, sign */
enum { T_D1, T_AP2, T_D2, T_D3, T_AP4, T_D4 };
struct tap { uint8_t line; uint16_t dist; int8_t sign; };
static const struct tap taps_l[7] = {{T_D3, 266, 1}, {T_D3, 2974, 1}, {T_AP4, 1913, -1}, {T_D4, 1996, 1},
                                     {T_D1, 1990, -1}, {T_AP2, 187, -1}, {T_D2, 1066, -1}};
static const struct tap taps_r[7] = {{T_D1, 353, 1}, {T_D1, 3627, 1}, {T_AP2, 1228, -1}, {T_D2, 2673, 1},
                                     {T_D3, 2111, -1}, {T_AP4, 335, -1}, {T_D4, 121, -1}};
/* early reflections: 8 taps per side (at size 1), alternating signs, falling */
static const uint16_t er_l[8] = {211, 389, 557, 761, 997, 1231, 1499, 1811};
static const uint16_t er_r[8] = {263, 431, 619, 829, 1061, 1319, 1597, 1907};
static const float er_g[8] = {0.62f, -0.53f, 0.46f, -0.4f, 0.34f, -0.29f, 0.24f, -0.2f};

static inline __attribute__((always_inline)) float ring(const float *b, unsigned mask, uint32_t w, float dist)
{
    int id = (int)dist;
    float f = dist - (float)id;
    float a = b[(w - (uint32_t)id) & mask], c = b[(w - (uint32_t)id - 1u) & mask];
    return a + f * (c - a);
}

static void tank_run(volatile struct rstate *RS, float *state, const struct tank *tk, const struct settings *st,
                     const float *in, float *outl, float *outr, int n)
{
    float *ap_in[4] = {BUF(state, 0), BUF(state, 1), BUF(state, 2), BUF(state, 3)};
    float *map_a = BUF(state, 4), *map_b = BUF(state, 5), *er = BUF(state, 6);
    float *d1 = BUF(state, 7), *d2 = BUF(state, 8), *d3 = BUF(state, 9), *d4 = BUF(state, 10);
    float *ap2 = BUF(state, 11), *ap4 = BUF(state, 11) + 4096;

    float decay = clamp01(state[0] * (1.0f / 0.93f)), damp = clamp01(state[1]);
    /* size x 0.5 .. x 1.5, slewed towards the setting */
    float target = 0.5f + st->size, sz = RS->size;
    sz += 0.05f * (target - sz);
    if (!(sz > 0.4f) || sz > 1.6f)
        sz = target;
    float rt = tk->rt_min * exp2_fast(tk->rt_log2_range * decay);
    float bass_mult = exp2_fast((st->bass - 0.5f) * 2.0f);                    /* x 0.5 .. x 2 */
    float xo = onepole(100.0f * exp2_fast(st->xover * 4.3219f));              /* 100 .. 2000 Hz */
    float treble = 1.0f - 0.92f * damp;                                         /* treble low-pass coefficient */
    float hc = onepole(1000.0f * exp2_fast(st->hicut * 3.8074f));              /* 1 .. 14 kHz */
    float lc = onepole(20.0f * exp2_fast(st->locut * 4.6439f));                /* 20 .. 500 Hz */
    float g_in1 = 0.75f * st->diff, g_in2 = 0.625f * st->diff;                  /* input diffusion */
    float g_dec1 = -0.7f * (0.4f + 0.6f * st->diff), g_dec2 = 0.5f;             /* tank diffusion */
    float depth = 24.0f * st->depth;                                            /* samples */
    uint32_t lfo_inc = (uint32_t)(4294967296.0f / FS * (0.1f * exp2_fast(st->rate * 5.6439f)));  /* 0.1 .. 5 Hz */
    float er_lvl = 0.6f * st->early, side = st->width;

    float la = (tk->map_a + tk->d1 + tk->ap2 + tk->d2) * sz, lb = (tk->map_b + tk->d3 + tk->ap4 + tk->d4) * sz;
    float k_rt = -3.0f * 3.3219281f / (rt * FS);
    float gm_a = exp2_fast(k_rt * la), gm_b = exp2_fast(k_rt * lb);
    float gb_a = exp2_fast(k_rt * la / bass_mult), gb_b = exp2_fast(k_rt * lb / bass_mult);

    /* modulated allpasses live in 1024-float rings: length x size + 2 x depth must stay below 1023 */
    float l_map_a = tk->map_a * (sz < 1.4f ? sz : 1.4f), l_map_b = tk->map_b * (sz < 1.0f ? sz : 1.0f);
    float l_d1 = tk->d1 * sz, l_ap2 = tk->ap2 * sz, l_d2 = tk->d2 * sz;
    float l_d3 = tk->d3 * sz, l_ap4 = tk->ap4 * sz, l_d4 = tk->d4 * sz;
    float l_er = tk->er_scale * sz;
    const float tap_scale[6] = {tk->d1 / hall.d1 * sz, tk->ap2 / hall.ap2 * sz, tk->d2 / hall.d2 * sz,
                                tk->d3 / hall.d3 * sz, tk->ap4 / hall.ap4 * sz, tk->d4 / hall.d4 * sz};

    /* tap distances in samples, once per block */
    uint32_t erl[8], err[8], tl[7], tr[7];
    for (int t = 0; t < 8; t++) {
        erl[t] = (uint32_t)(er_l[t] * l_er);
        err[t] = (uint32_t)(er_r[t] * l_er);
    }
    for (int t = 0; t < 7; t++) {
        tl[t] = (uint32_t)(taps_l[t].dist * tap_scale[taps_l[t].line]);
        tr[t] = (uint32_t)(taps_r[t].dist * tap_scale[taps_r[t].line]);
    }
    uint32_t w8 = RS->w8k, w4 = RS->w4k, w1 = RS->w1k, w5 = RS->w512;
    float xa = RS->lp_a[0], ta = RS->lp_a[1], xb = RS->lp_b[0], tb = RS->lp_b[1];
    float fa = RS->fb_a, fb = RS->fb_b, hcs = RS->hc, lcs = RS->lc;
    uint32_t lfo = RS->lfo, wan = RS->wander;
    /* the LFO (<= 5 Hz) moves ~1 % of a cycle per block: its values at the block's ends, linear in between (no sines
       per sample); the phases advance exactly as per sample */
    uint32_t wan_inc = lfo_inc / 7u + 1u;
    uint32_t lfo_e = lfo + (uint32_t)n * lfo_inc, wan_e = wan + (uint32_t)n * wan_inc;
    float m1 = 0.75f * sin32(lfo) + 0.25f * sin32(wan);
    float m2 = 0.75f * sin32(lfo + 0x40000000u) + 0.25f * sin32(wan + 0x9e3779b9u);
    float inv_n = n > 0 ? 1.0f / (float)n : 0.0f;
    float dm1 = (0.75f * sin32(lfo_e) + 0.25f * sin32(wan_e) - m1) * inv_n;
    float dm2 = (0.75f * sin32(lfo_e + 0x40000000u) + 0.25f * sin32(wan_e + 0x9e3779b9u) - m2) * inv_n;
    lfo = lfo_e;
    wan = wan_e;

    for (int i = 0; i < n; i++) {
        /* input: band limits */
        float x = in[i];
        hcs += hc * (x - hcs);
        lcs += lc * (hcs - lcs);
        x = hcs - lcs;
        /* early reflections */
        er[w8 & 8191] = x;
        float el = 0.0f, erv = 0.0f;
        for (int t = 0; t < 8; t++) {
            el += er_g[t] * er[(w8 - erl[t]) & 8191];
            erv += er_g[t] * er[(w8 - err[t]) & 8191];
        }
        /* input diffusers */
        float d = x;
        for (int k = 0; k < 4; k++) {
            float *b = ap_in[k];
            float y = b[(w5 - (uint32_t)tk->in_ap[k]) & 511];
            float g = k < 2 ? g_in1 : g_in2;
            float v = d - g * y;
            b[w5 & 511] = v;
            d = y + g * v;
        }
        /* modulation: two quadrature sines + a slow wander, linear across the block (set up before the loop) */
        m1 += dm1;
        m2 += dm2;

        /* branch A */
        float a = d + fb;
        {
            float y = ring(map_a, 1023, w1, l_map_a + depth * (1.0f + m1));
            float v = a - g_dec1 * y;
            map_a[w1 & 1023] = v;
            a = y + g_dec1 * v;
        }
        d1[w8 & 8191] = a;
        a = ring(d1, 8191, w8, l_d1);
        xa += xo * (a - xa);
        a = xa * gb_a + (a - xa) * gm_a;            /* bass and mid decay */
        ta += treble * (a - ta);
        a = ta;
        {
            float y = ring(ap2, 4095, w4, l_ap2);
            float v = a + g_dec2 * y;
            ap2[w4 & 4095] = v;
            a = y - g_dec2 * v;
        }
        d2[w8 & 8191] = a;
        float end_a = ring(d2, 8191, w8, l_d2);

        /* branch B */
        float b = d + fa;
        {
            float y = ring(map_b, 1023, w1, l_map_b + depth * (1.0f + m2));
            float v = b - g_dec1 * y;
            map_b[w1 & 1023] = v;
            b = y + g_dec1 * v;
        }
        d3[w8 & 8191] = b;
        b = ring(d3, 8191, w8, l_d3);
        xb += xo * (b - xb);
        b = xb * gb_b + (b - xb) * gm_b;
        tb += treble * (b - tb);
        b = tb;
        {
            float y = ring(ap4, 4095, w4, l_ap4);
            float v = b + g_dec2 * y;
            ap4[w4 & 4095] = v;
            b = y - g_dec2 * v;
        }
        d4[w8 & 8191] = b;
        float end_b = ring(d4, 8191, w8, l_d4);

        fa = end_a;                                  /* A's end feeds B, B's end feeds A */
        fb = end_b;

        /* output taps */
        /* output taps (Dattorro's signs: + + - + - - -) */
        float sl = d3[(w8 - tl[0]) & 8191] + d3[(w8 - tl[1]) & 8191] - ap4[(w4 - tl[2]) & 4095]
                   + d4[(w8 - tl[3]) & 8191] - d1[(w8 - tl[4]) & 8191] - ap2[(w4 - tl[5]) & 4095]
                   - d2[(w8 - tl[6]) & 8191];
        float sr = d1[(w8 - tr[0]) & 8191] + d1[(w8 - tr[1]) & 8191] - ap2[(w4 - tr[2]) & 4095]
                   + d2[(w8 - tr[3]) & 8191] - d3[(w8 - tr[4]) & 8191] - ap4[(w4 - tr[5]) & 4095]
                   - d4[(w8 - tr[6]) & 8191];
        float mid = 0.5f * (sl + sr), sd = 0.5f * (sl - sr) * side;
        outl[i] = TANK_GAIN * (er_lvl * el + 0.6f * (mid + sd));
        outr[i] = TANK_GAIN * (er_lvl * erv + 0.6f * (mid - sd));

        w8++;
        w4++;
        w1++;
        w5++;
    }
    RS->w8k = w8;
    RS->w4k = w4;
    RS->w1k = w1;
    RS->w512 = w5;
    RS->lp_a[0] = xa;
    RS->lp_a[1] = ta;
    RS->lp_b[0] = xb;
    RS->lp_b[1] = tb;
    RS->fb_a = fa;
    RS->fb_b = fb;
    RS->hc = hcs;
    RS->lc = lcs;
    RS->size = sz;
    RS->lfo = lfo;
    RS->wander = wan;
}

/* --- Room (blackbox-mod's FDN, with the new controls) ----------------------------------------------------------------- */
#define ER_LEN 8192
static const uint16_t room_in_ap[4] = {142, 107, 379, 277};
static const uint16_t room_line[8] = {887, 1031, 1153, 1279, 1409, 1543, 1693, 1867};
static const uint8_t room_line_ap[8] = {37, 53, 67, 83, 97, 109, 113, 127};
static const uint16_t room_er_l[8] = {131, 197, 283, 359, 467, 571, 709, 853};
static const uint16_t room_er_r[8] = {157, 233, 311, 421, 523, 647, 779, 929};
static const float room_er_g[8] = {0.55f, -0.47f, 0.41f, -0.35f, 0.3f, -0.25f, 0.21f, -0.17f};
static const float out_sign_l[8] = {1, -1, 1, -1, 1, 1, -1, -1}, out_sign_r[8] = {1, 1, -1, -1, -1, 1, 1, -1};
static const uint8_t mod_line[4] = {0, 2, 5, 7};

static inline __attribute__((always_inline)) float allpass(float *buf, unsigned *pos, unsigned len, float g, float x)
{
    float y = buf[*pos], w = x + g * y;
    buf[*pos] = w;
    *pos = *pos + 1 >= len ? 0 : *pos + 1;
    return y - g * w;
}

static void room(volatile struct rstate *RS, float *state, const struct settings *st, const float *in, float *outl,
                 float *outr, int n)
{
    float *er = BUF(state, 6);
    float *in_ap[4] = {BUF(state, 0), BUF(state, 1), BUF(state, 2), BUF(state, 3)};
    float *line[8], *lap[8];
    for (int k = 0; k < 8; k++) {
        line[k] = BUF(state, 7 + k / 2) + (k & 1) * 4096;
        lap[k] = BUF(state, 4 + k / 4) + (k & 3) * 128;
    }
    float decay = clamp01(state[0] * (1.0f / 0.93f)), damp = clamp01(state[1]);
    float rt60 = 0.25f + 2.25f * decay, lpc = 0.9f - 0.7f * damp;
    float size = exp2_fast((st->size - 0.5f) * 2.0f);                         /* x 0.5 .. x 2 */
    float bass_mult = exp2_fast((st->bass - 0.5f) * 2.0f);
    float xo = onepole(100.0f * exp2_fast(st->xover * 4.3219f));
    float hc = onepole(1000.0f * exp2_fast(st->hicut * 3.8074f));
    float lc = onepole(20.0f * exp2_fast(st->locut * 4.6439f));
    float gd = 0.5f + 0.25f * st->diff;                                         /* input diffusion 0.5 .. 0.75 */
    float g[8], gb[8];
    unsigned len[8];
    for (int k = 0; k < 8; k++) {
        float l = (float)room_line[k] * size;
        len[k] = l < 64.0f ? 64u : l > 4000.0f ? 4000u : (unsigned)l;
        float kr = -3.0f * 3.321928f * (float)(len[k] + room_line_ap[k]) / (rt60 * FS);
        g[k] = exp2_fast(kr);
        gb[k] = exp2_fast(kr / bass_mult);
    }
    float depth = 20.0f * st->depth, er_lvl = 0.5f * st->early, side = st->width;
    uint32_t inc = (uint32_t)(4294967296.0f / FS * (0.1f * exp2_fast(st->rate * 5.6439f)));
    static const uint32_t spread[4] = {0u, 0x51eb851fu, 0x9e3779b9u, 0xc6a7ef9eu};

    unsigned erl[8], err[8];
    for (int t = 0; t < 8; t++) {
        erl[t] = (unsigned)(room_er_l[t] * size);
        err[t] = (unsigned)(room_er_r[t] * size);
    }
    unsigned ep = RS->er_pos, ip[4], lpos[8], apos[8];
    float lp[8], xl[8];
    uint32_t lfo0 = RS->rlfo[0];
    /* modulation offsets at the block's ends, linear in between (see tank_run) */
    float moff[4], dmoff[4];
    {
        uint32_t lfo_e = lfo0 + (uint32_t)n * inc;
        float inv_n = n > 0 ? 1.0f / (float)n : 0.0f;
        for (int m = 0; m < 4; m++) {
            moff[m] = depth * (1.0f + sin32(lfo0 + inc + spread[m]));
            dmoff[m] = (depth * (1.0f + sin32(lfo_e + inc + spread[m])) - moff[m]) * inv_n;
        }
        lfo0 = lfo_e;
    }
    float hcs = RS->hc, lcs = RS->lc;
    for (int k = 0; k < 4; k++)
        ip[k] = RS->in_ap_pos[k];
    for (int k = 0; k < 8; k++) {
        lpos[k] = RS->line_pos[k];
        if (lpos[k] >= len[k])
            lpos[k] = 0;
        apos[k] = RS->line_ap_pos[k];
        lp[k] = RS->rlp[k];
        xl[k] = 0.0f;
    }
    for (int i = 0; i < n; i++) {
        float x = in[i];
        hcs += hc * (x - hcs);
        lcs += lc * (hcs - lcs);
        x = hcs - lcs;
        er[ep] = x;
        float el = 0.0f, erv = 0.0f;
        for (int t = 0; t < 8; t++) {
            el += room_er_g[t] * er[(ep - erl[t]) & (ER_LEN - 1)];
            erv += room_er_g[t] * er[(ep - err[t]) & (ER_LEN - 1)];
        }
        ep = (ep + 1) & (ER_LEN - 1);
        float d = 0.5f * (el + erv);
        for (int k = 0; k < 4; k++)
            d = allpass(in_ap[k], &ip[k], room_in_ap[k], k < 2 ? gd : gd * 0.833f, d);

        float o[8], rd[8];
        for (int k = 0; k < 8; k++)
            rd[k] = line[k][lpos[k]];
        for (int m = 0; m < 4; m++) {
            int k = mod_line[m];
            float off = moff[m];
            moff[m] += dmoff[m];
            int io = (int)off;
            float fr = off - (float)io;
            unsigned a = lpos[k] + (unsigned)io, ln = len[k];
            a = a >= ln ? a - ln : a;
            unsigned b = a + 1 >= ln ? 0 : a + 1;
            rd[k] = line[k][a] + fr * (line[k][b] - line[k][a]);
        }
        for (int k = 0; k < 8; k++) {
            float v = allpass(lap[k], &apos[k], room_line_ap[k], 0.5f, rd[k]);
            lp[k] += lpc * (v - lp[k]);
            xl[k] += xo * (lp[k] - xl[k]);
            o[k] = xl[k] * gb[k] + (lp[k] - xl[k]) * g[k];
        }
        float a0 = o[0] + o[1], a1 = o[0] - o[1], a2 = o[2] + o[3], a3 = o[2] - o[3];
        float a4 = o[4] + o[5], a5 = o[4] - o[5], a6 = o[6] + o[7], a7 = o[6] - o[7];
        float b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3;
        float b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
        const float sc = 0.35355339f;
        float h[8] = {(b0 + b4) * sc, (b1 + b5) * sc, (b2 + b6) * sc, (b3 + b7) * sc,
                      (b0 - b4) * sc, (b1 - b5) * sc, (b2 - b6) * sc, (b3 - b7) * sc};
        float sl = 0.0f, sr = 0.0f;
        for (int k = 0; k < 8; k++) {
            line[k][lpos[k]] = d + h[k];
            lpos[k] = lpos[k] + 1 >= len[k] ? 0 : lpos[k] + 1;
            sl += out_sign_l[k] * o[k];
            sr += out_sign_r[k] * o[k];
        }
        float mid = 0.5f * (sl + sr), sd = 0.5f * (sl - sr) * side;
        outl[i] = ROOM_GAIN * (er_lvl * el + 0.3f * (mid + sd));
        outr[i] = ROOM_GAIN * (er_lvl * erv + 0.3f * (mid - sd));
    }
    RS->er_pos = ep;
    RS->hc = hcs;
    RS->lc = lcs;
    RS->rlfo[0] = lfo0;
    for (int k = 0; k < 4; k++)
        RS->in_ap_pos[k] = (uint16_t)ip[k];
    for (int k = 0; k < 8; k++) {
        RS->line_pos[k] = (uint16_t)lpos[k];
        RS->line_ap_pos[k] = (uint8_t)apos[k];
        RS->rlp[k] = lp[k];
    }
}

/* --- the core call ---------------------------------------------------------------------------------------------------- */
#define RVB_OBJ (*(uint8_t *volatile *)0x2406cbf0u)    /* set by reverb_core_thunk: the reverb object (r4 there) */

/* one instance: Type switch clears the buffers, then the algorithm */
static void run(volatile struct rstate *RS, float *state, const struct settings *st, const float *in, float *outl,
                float *outr, int n)
{
    if (RS->magic != RMAGIC || RS->alg != st->type) {
        clear(state, RS);
        RS->alg = st->type;
        RS->magic = RMAGIC;
    }
    switch (st->type) {
    case ALG_ROOM:
        room(RS, state, st, in, outl, outr, n);
        break;
    case ALG_HALL:
        tank_run(RS, state, &hall, st, in, outl, outr, n);
        break;
    case ALG_CHAMBER:
        tank_run(RS, state, &chamber, st, in, outl, outr, n);
        break;
    default:
        fw_plate_core(state, in, outl, outr, n);
    }
    /* Level: -12 .. +12 dB (2 ^ (x x 4 - 2)), 0 dB at 500 */
    if (st->level != 0.5f) {
        float g = exp2_fast(st->level * 3.9863f - 1.9932f);
        for (int i = 0; i < n; i++) {
            outl[i] *= g;
            outr[i] *= g;
        }
    }
}

/* Replaces the core call in the reverb's process (bl at 0x080aaaf2), via reverb_core_thunk. */
void reverb_core(float *state, const float *in, float *outl, float *outr, int n)
{
    struct settings st;
    read_settings(RVB_OBJ, &st);
    run(RS1, state, &st, in, outl, outr, n);
}

/* FX3 (src/fx3.c): a plate state of its own, settings from an FX cell record; Decay and Damping already in state[0..1] */
void reverb_run_record(float *state, const void *rec, const float *in, float *outl, float *outr, int n)
{
    struct settings st;
    read_record(rec, &st);
    run(RS2, state, &st, in, outl, outr, n);
}

/* the cell's Type, for the page filter (clip_page): record of an FX address (layer 3, row) */
unsigned reverb_type_of(const void *rec)
{
    return alg_of((unsigned)fw_store_get(rec, P_TYPE));
}

/* hidden with Type = Plate (Level stays: it works on the plate too) */
const uint16_t reverb_param_ids[NPARAMS] = {P_SIZE, P_DIFF, P_BASS, P_XOVER, P_RATE, P_DEPTH, P_EARLY, P_WIDTH,
                                            P_HICUT, P_LOCUT, 0};

/* FX cell page filter (fx.c's fx_page, from clip_page): 1 = a reverb record's page. Plate: the stock three and Level;
   page 1: Type, Decay, Predelay, Damping, Size, Diffusion, Early, Level; page 2 ("Hall >"): Type and the other seven. */
typedef void (*list_remove_fn)(void *list, unsigned param);
#define fw_list_remove ((list_remove_fn)FN(0x080884c8))   /* [mk2] */
static const uint16_t rvb_page1[] = {P_BASS, P_XOVER, P_RATE, P_DEPTH, P_WIDTH, P_HICUT, P_LOCUT, 0};
static const uint16_t rvb_page2[] = {0x1a, 0x1e, 0x1d, P_SIZE, P_DIFF, P_EARLY, P_LEVEL, 0};

static int has(const uint8_t *list, unsigned param)
{
    unsigned n = *(const uint32_t *)list;
    for (unsigned i = 0; i < n && i < 0x30; i++)
        if (*(const uint16_t *)(list + 4 + 12 * i) == param)
            return 1;
    return 0;
}

int reverb_page_filter(void *list, const void *rec)
{
    if (!has(list, P_TYPE))
        return 0;
    unsigned item = (unsigned)fw_store_get(rec, P_TYPE);
    const uint16_t *hide = rvb_page1;
    if (alg_of(item) == ALG_PLATE)
        hide = reverb_param_ids;
    else if (item < TYPE_ITEMS && (item == 2 || item == 4 || item == 6))
        hide = rvb_page2;
    for (; *hide; hide++)
        fw_list_remove(list, *hide);
    return 1;
}
