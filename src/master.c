/* ported to the bitbox MK2 2.3.4 by tools/port_mk2.py */
/*
 * Master section (portkit): the stock compressor's settings and a master distortion. bitbox MK2: on three Tools tabs
 * (Comp, Cmp2, Dist) next to Main / Rec / MIDIIn (the micro used pages behind its arrow keys; see "pages" below).
 *
 * - Compressor: the firmware has a compressor across Out 1/2 (object engine +0xfb38, run by the block driver at
 *   0x080983a8 when the On/Off flag engine +0x28cc0 is set, i.e. "Compressor:" 0x96 on the settings page). Only On/Off
 *   is reachable stock; threshold (+0x428, float dB), ratio (+0x42c, float), makeup (+0x430, float dB), attack (+0x448)
 *   and release (+0x44c, samples, the gain ramp's lengths) are set by its constructor (0x080ba6c0: -4 dB, 4:1, -4 dB,
 *   480, 12000) and read raw every block (0x080ba43c; same offsets as bitbox-micro-fw-patch, its guards hold here).
 *   master_comp (hooked at 0x080983a8) writes them from the settings store before every block.
 * - Distortion: on bus 0 (Out 1/2) after the FX returns and the master EQ, before the compressor (thunk at 0x08098190,
 *   the compressor flag's load, so it runs whether the compressor is on or not). 2x oversampled (31-tap half-band
 *   up / down), shapers Soft (tanh) / Hard (clip) / Fold (triangle) / Tube (asymmetric tanh, DC blocked; state 0x2406b800), Tone (one-pole
 *   low-pass, the stock cutoff curve), Mix (dry delayed by the filters' 15 samples), Level.
 * - Comp Type (0x19d): Stock = the stock DSP above; SSL = an SSL-style bus compressor of ours (ssl_run, end of
 *   file) with the SSL's switch values (ratio 0x19f, attack 0x17d, release 0x17f incl. Auto); threshold and Gain
 *   shared, Mix (0x19e) for both. The Compressor page shows the type's knobs; master_knob (hook at the dispatcher's
 *   knob call 0x08080c00) rebuilds it when the type changes.
 * - Settings: new ids 0x1a1-0x1a3, 0x1a5-0x1a9, 0x1ab, 0x1ac (none of them a constant anywhere in the stock code) in the settings store app +0x7ed0 (MK2; micro +0x7fd0) (init 0x08089cbc, capacity 64, 36
 *   stock entries; hooked at its last add 0x08089e30), registered next to Fidelity (filter.c). The engine never sees
 *   them (ids > 0x17a). Defaults = the stock compressor; distortion Off.
 * - Pages: the page builder 0x08078ec8 fills code 0x40 (the settings page, screen 0x25) from a table; clip_page
 *   (clipshift.c, wraps all its callers) asks master_page first, which fills our list while a master page is shown.
 *   The knob handler 0x0807f7bc writes code-0x40 knobs straight into the settings store (micro 0x0807d3aa), so editing needs nothing.
 */
#include <stdint.h>

#pragma GCC optimize("O2", "no-tree-loop-distribute-patterns")

#define FN(addr) ((addr) | 1u)

typedef void (*register_list_fn)(void *table, int param, const char *label, const char *const *names, int count,
                                 const char *xml);
typedef void (*register_knob_fn)(void *table, int param, int type, const char *label, int min, int max,
                                 const char *xml);
typedef int (*store_get_fn)(const void *store, unsigned param);
typedef void (*store_add_fn)(void *store, unsigned param, int value);
typedef void (*list_add_fn)(void *list, unsigned param, int value);
typedef void (*comp_fn)(void *comp, void *engine);
typedef int (*planes_fn)(void *buf, float **l, float **r);
typedef int (*frames_fn)(void *buf);
typedef float (*curve_fn)(float x);
#define fw_register_list ((register_list_fn)FN(0x0806a27c))
#define fw_register_knob ((register_knob_fn)FN(0x0806a2d4))
int fast_get(const void *store, unsigned param);       /* fastget.c: 0x08089388 with a cache */
#define fw_store_get fast_get
#define fw_store_add     ((store_add_fn)FN(0x080893d6))
#define fw_list_add      ((list_add_fn)FN(0x080884a4))     /* list: +0 count (u32, max 0x2f), entries of 12 bytes */
#define fw_comp          ((comp_fn)FN(0x080ba43c))
#define fw_planes        ((planes_fn)FN(0x080a4630))
#define fw_frames        ((frames_fn)FN(0x080a4588))
#define fw_cutoff_hz     ((curve_fn)FN(0x080a1564))        /* knob 0..1 -> Hz (Filter, EQ Freq display type 10) */

#define APP        ((uint8_t *)0x24015170u)
#define SETTINGS   (APP + 0x7ed0u)                          /* MK2: settings store (micro +0x7fd0; Tools builder 0x08079776) */

#define P_COMP_ON  0x96                                     /* stock "Compressor:" (On/Off, xml globcomp) */
#define P_C_THRESH 0x1a1
#define P_C_RATIO  0x1a2
#define P_C_ATTACK 0x1a3
#define P_C_REL    0x1a5
#define P_C_GAIN   0x1a6
#define P_C_TYPE   0x19d                                    /* Comp Type: Stock / SSL */
#define P_C_MIX    0x19e                                    /* compressor dry / wet (parallel compression) */
#define P_S_RATIO  0x19f                                    /* SSL: ratio switch 2 / 4 / 10 */
#define P_S_ATTACK 0x17d                                    /* SSL: attack switch 0.1 .. 30 ms */
#define P_S_REL    0x17f                                    /* SSL: release switch 0.1 .. 1.2 s, Auto */
#define P_D_TYPE   0x1a7
#define P_D_DRIVE  0x1a8
#define P_D_TONE   0x1a9
#define P_D_MIX    0x1ab
#define P_D_LEVEL  0x1ac

#define FS         48000.0f
#define SR_MS      48                                       /* samples per ms */

/* --- registration and the settings store -------------------------------------------------------------------------- */
static const char *const dist_names[] = {"Off", "Soft", "Hard", "Fold", "Tube"};
enum { D_OFF, D_SOFT, D_HARD, D_FOLD, D_TUBE, D_TYPES };

static const char *const ctype_names[] = {"Stock", "SSL"};
static const char *const sratio_names[] = {"2:1", "4:1", "10:1"};
static const char *const sattack_names[] = {"0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms"};
static const char *const srel_names[] = {"0.1 s", "0.3 s", "0.6 s", "1.2 s", "Auto"};
enum { C_STOCK, C_SSL, C_TYPES };

/* type 0: a list (names, max = count) */
struct pdef { uint16_t id; uint8_t type; const char *label; int min, max, def; const char *xml; const char *const *names; };
static const struct pdef pdefs[] = {
    {P_C_TYPE, 0, "Comp Type:", 0, C_TYPES, C_STOCK, "pkctype", ctype_names},
    {P_C_THRESH, 7, "Threshold:", -48000, 0, -4000, "pkcthresh", 0},    /* type 7: milli-dB, "-4.0 dB" */
    {P_C_RATIO, 1, "Ratio:", 1, 20, 4, "pkcratio", 0},                  /* type 1: integer (x:1) */
    {P_C_ATTACK, 1, "Attack ms:", 1, 200, 10, "pkcattack", 0},
    {P_C_REL, 1, "Release ms:", 10, 2000, 250, "pkcrelease", 0},
    {P_C_GAIN, 7, "Gain:", -12000, 24000, -4000, "pkcgain", 0},
    {P_C_MIX, 8, "Mix:", 0, 1000, 1000, "pkcmix", 0},
    {P_S_RATIO, 0, "Ratio:", 0, 3, 0, "pksratio", sratio_names},      /* SSL defaults: mix glue 2:1, 30 ms, Auto */
    {P_S_ATTACK, 0, "Attack:", 0, 6, 5, "pksattack", sattack_names},
    {P_S_REL, 0, "Release:", 0, 5, 4, "pksrelease", srel_names},
    {P_D_TYPE, 0, "Dist Type:", 0, D_TYPES, D_OFF, "pkdtype", dist_names},
    {P_D_DRIVE, 7, "Drive:", 0, 36000, 12000, "pkddrive", 0},
    {P_D_TONE, 10, "Tone:", 0, 1000, 1000, "pkdtone", 0},               /* type 10: Hz, the stock cutoff curve */
    {P_D_MIX, 8, "Mix:", 0, 1000, 1000, "pkdmix", 0},
    {P_D_LEVEL, 7, "Dist Lvl:", -24000, 12000, 0, "pkdlevel", 0},  /* MK2: on the Comp2 tab */
};
#define NPDEFS (int)(sizeof pdefs / sizeof pdefs[0])

/* from interp_register (filter.c): the parameter table */
void master_register(void *table)
{
    for (int k = 0; k < NPDEFS; k++) {
        const struct pdef *p = &pdefs[k];
        if (p->type == 0)
            fw_register_list(table, p->id, p->label, p->names, p->max, p->xml);
        else
            fw_register_knob(table, p->id, p->type, p->label, p->min, p->max, p->xml);
    }
}

/* hook at the settings store init's last add (0x08089e30): that add, then ours with their defaults */
void master_store_add(void *store, unsigned param, int value)
{
    fw_store_add(store, param, value);
    for (int k = 0; k < NPDEFS; k++)
        fw_store_add(store, pdefs[k].id, pdefs[k].def);
}

/* --- pages (bitbox MK2): three Tools tabs next to Main / Rec / MIDIIn --------------------------------------------------
 * The MK2 Tools screen is a tab page (builder 0x0807974c, message code 0x40): tab labels at 0x080c7ea4 (6 slots, the
 * first empty one ends the tab row), 8 ids per tab at 0x080c7ebc + tab * 16, of which the screen shows the first four
 * (2 x 2, column-wise). patches/master.py fills tabs 3..5 (Comp, Cmp2, Dist) as data. The builder copies the ids and
 * their values from the settings store; master_tools_page (all its callers) swaps the Stock ratio / attack / release
 * for the SSL switches while Comp Type is SSL. The tab is rebuilt on every tab change, so Comp Type (tab Comp)
 * takes effect on the Comp2 tab's knobs the next time it is shown. Editing: the knob handler writes code-0x40 knobs
 * straight into the settings store (as on the micro), so nothing else is needed.
 */
#define TOOLS_CODE 0x40
#define TAB_COMP   3
#define TAB_COMP2  4

typedef int (*tools_fn)(void *app, const void *msg, uint8_t *out, int tab);
#define fw_tools_page ((tools_fn)FN(0x0807974c))

static void swap_id(uint8_t *out, unsigned from, unsigned to)
{
    for (int k = 0; k < 8; k++) {
        uint16_t *id = (uint16_t *)(out + 2 + 2 * k);
        if (*id == from) {
            *id = (uint16_t)to;
            *(int32_t *)(out + 0x14 + 4 * k) = fw_store_get(SETTINGS, to);
        }
    }
}

int master_tools_page(void *app, const void *msg, uint8_t *out, int tab)
{
    int r = fw_tools_page(app, msg, out, tab);
    if (*(const uint16_t *)((const uint8_t *)msg + 4) == TOOLS_CODE && (tab == TAB_COMP || tab == TAB_COMP2)
        && fw_store_get(SETTINGS, P_C_TYPE) == C_SSL) {
        swap_id(out, P_C_RATIO, P_S_RATIO);
        swap_id(out, P_C_ATTACK, P_S_ATTACK);
        swap_id(out, P_C_REL, P_S_REL);
    }
    return r;
}

/* clipshift.c's clip_page asks this first on the micro (its settings page is built by the pad page builder); the MK2's
   Tools page is the tab page above, so nothing to do here. */
int master_page(void *app, const void *msg, uint8_t *list)
{
    (void)app;
    (void)msg;
    (void)list;
    return 0;
}

/* --- compressor: settings onto the live object, every block (hook at 0x080983a8) ----------------------------------- */
#define C_THRESH  0x428
#define C_RATIO   0x42c
#define C_MAKEUP  0x430
#define C_ATTACK  0x448
#define C_RELEASE 0x44c

typedef void *(*bus_fn)(void *engine, unsigned id);
#define fw_bus ((bus_fn)FN(0x080a9498))
typedef void (*node_fn)(void *node, void *engine);

static void ssl_run(void *bus);
#define DRY_L ((float *)0x2406e000u)                     /* dry copy for Mix: 2 x 512 floats (patch RAM, free) */
#define DRY_R ((float *)0x2406e800u)

/* Comp Type Stock: the stock DSP with our settings; SSL: ours (ssl_run) on the same bus, then the node the stock DSP
   would hand on to (object +8, vtable +0xc). Mix (both): dry / wet. */
void master_comp(uint8_t *comp, void *engine)
{
    void *bus = fw_bus(engine, *(const uint16_t *)(comp + 0x1e));
    int mix = fw_store_get(SETTINGS, P_C_MIX);
    int silent = *((const uint8_t *)bus + 0x10);
    int n = fw_frames(bus);
    float *l = 0, *r = 0;
    int dry = mix >= 0 && mix < 1000 && !silent && n > 0 && n <= 512;
    if (dry) {
        fw_planes(bus, &l, &r);
        for (int k = 0; k < n; k++) {
            DRY_L[k] = l[k];
            DRY_R[k] = r ? r[k] : 0.0f;
        }
    }
    if (fw_store_get(SETTINGS, P_C_TYPE) == C_SSL) {
        ssl_run(bus);
        void *child = *(void **)(comp + 8);
        if (child)
            ((node_fn)(*(uint32_t **)child)[3])(child, engine);
    } else {
        int ratio = fw_store_get(SETTINGS, P_C_RATIO);
        if (ratio >= 1) {                                /* our entries are there (0 = missing: stock values stay) */
            int att = fw_store_get(SETTINGS, P_C_ATTACK), rel = fw_store_get(SETTINGS, P_C_REL);
            *(volatile float *)(comp + C_THRESH) = (float)fw_store_get(SETTINGS, P_C_THRESH) * 0.001f;
            *(volatile float *)(comp + C_RATIO) = (float)(ratio > 20 ? 20 : ratio);
            *(volatile float *)(comp + C_MAKEUP) = (float)fw_store_get(SETTINGS, P_C_GAIN) * 0.001f;
            *(volatile int32_t *)(comp + C_ATTACK) = (att < 1 ? 1 : att > 200 ? 200 : att) * SR_MS;
            *(volatile int32_t *)(comp + C_RELEASE) = (rel < 10 ? 10 : rel > 2000 ? 2000 : rel) * SR_MS;
        }
        fw_comp(comp, engine);
    }
    if (dry) {
        float w = (float)mix * 0.001f;
        for (int k = 0; k < n; k++) {
            l[k] = DRY_L[k] + w * (l[k] - DRY_L[k]);
            if (r && r != l)
                r[k] = DRY_R[k] + w * (r[k] - DRY_R[k]);
        }
    }
}

/* --- distortion ---------------------------------------------------------------------------------------------------- */
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

static float db_gain(int mdb)
{
    return exp2_fast((float)mdb * (0.001f / 6.0206f));
}

static inline __attribute__((always_inline)) float tanh_r(float x)
{
    x = x > 3.0f ? 3.0f : x < -3.0f ? -3.0f : x;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

#define TUBE_BIAS 0.35f
static inline __attribute__((always_inline)) float shape(int type, float u)
{
    switch (type) {
    case D_SOFT:
        return tanh_r(u);
    case D_HARD:
        return u > 1.0f ? 1.0f : u < -1.0f ? -1.0f : u;
    case D_FOLD: {                                        /* triangle fold: identity up to +-1, then back and forth */
        float t = (u + 1.0f) * 0.25f;
        t -= (float)(int)t - (t < 0.0f && (float)(int)t != t);
        float a = t - 0.5f;
        return 1.0f - 4.0f * (a < 0.0f ? -a : a);
    }
    default:                                              /* Tube: asymmetric, even harmonics; DC removed after */
        return tanh_r(u + TUBE_BIAS) - tanh_r(TUBE_BIAS);
    }
}

/* 2x half-band (31 taps, Kaiser 7): odd taps 1, 3, .. 15; centre 0.5 */
static const float HB[8] = {0.314318663f, -0.094598776f, 0.046056884f, -0.023741433f,
                            0.011624296f, -0.005037228f, 0.001760217f, -0.000394379f};
#define HLEN 16

/* delay lines as rings stored twice (newest first): a window of the last 16 (8) values is always contiguous, nothing
   is shifted per sample */
struct dch {
    float xr[2 * HLEN];          /* input; window x[0..15], x[15] = the dry signal, aligned with the output */
    float sbr[2 * HLEN];         /* shaped odd (in-between) samples */
    float sar[16];               /* shaped even samples (8) */
    uint32_t xp, sbp, sap;
    float lp, dcx, dcy;          /* tone low-pass, DC blocker */
};
struct dist {
    uint32_t magic;
    float drive, mix, level;     /* smoothed: the last block's values */
    struct dch ch[2];
};
#define DS ((struct dist *)0x2406b800u)
_Static_assert(sizeof(struct dist) <= 0x300, "distortion state must end before 0x2406bb00 (tests/test_ram.py)");
#define DMAGIC 0x44495355u       /* "DISU" (ring layout) */

static float run1(struct dch *c, int type, float in, float drive, float tone_a, int tone_on)
{
    unsigned p = c->xp = (c->xp - 1u) & (HLEN - 1);
    c->xr[p] = c->xr[p + HLEN] = in;
    const float *x = &c->xr[p];
    /* up: even = x[n-8], odd = interpolated at n-7.5 */
    float b = HB[0] * (x[7] + x[8]) + HB[1] * (x[6] + x[9]) + HB[2] * (x[5] + x[10]) + HB[3] * (x[4] + x[11])
            + HB[4] * (x[3] + x[12]) + HB[5] * (x[2] + x[13]) + HB[6] * (x[1] + x[14]) + HB[7] * (x[0] + x[15]);
    float sa = shape(type, x[8] * drive);
    float sbv = shape(type, 2.0f * b * drive);
    unsigned q = c->sap = (c->sap - 1u) & 7u;
    c->sar[q] = c->sar[q + 8] = sa;
    unsigned r = c->sbp = (c->sbp - 1u) & (HLEN - 1);
    c->sbr[r] = c->sbr[r + HLEN] = sbv;
    const float *sb = &c->sbr[r];
    /* down: centre sa[7] (= x[n-15]) */
    float y = 0.5f * c->sar[q + 7]
            + HB[0] * (sb[7] + sb[8]) + HB[1] * (sb[6] + sb[9]) + HB[2] * (sb[5] + sb[10]) + HB[3] * (sb[4] + sb[11])
            + HB[4] * (sb[3] + sb[12]) + HB[5] * (sb[2] + sb[13]) + HB[6] * (sb[1] + sb[14]) + HB[7] * (sb[0] + sb[15]);
    if (type == D_TUBE) {                                  /* DC blocker, ~8 Hz */
        float o = y - c->dcx + 0.999f * c->dcy;
        c->dcx = y;
        c->dcy = o;
        y = o;
    }
    if (tone_on) {
        c->lp += tone_a * (y - c->lp);
        y = c->lp;
    }
    return y;
}

/* master_thunk.S at 0x08098190 (bus 0 = r4): bus 0 through the distortion */
void master_dist(void *bus0)
{
    struct dist *d = DS;
    int type = fw_store_get(SETTINGS, P_D_TYPE);
    if (type <= D_OFF || type >= D_TYPES) {
        d->magic = 0;                                    /* fresh state when it is switched on again */
        return;
    }
    if (*((const uint8_t *)bus0 + 0x10))                 /* silent: nothing to shape (the tails are 15 samples) */
        return;
    int n = fw_frames(bus0);
    if (n <= 0 || n > 512)
        return;
    float *l, *r;
    fw_planes(bus0, &l, &r);
    if (!l)
        return;

    float drive = db_gain(fw_store_get(SETTINGS, P_D_DRIVE));
    float mix = (float)fw_store_get(SETTINGS, P_D_MIX) * 0.001f;
    float level = db_gain(fw_store_get(SETTINGS, P_D_LEVEL));
    mix = mix < 0.0f ? 0.0f : mix > 1.0f ? 1.0f : mix;
    if (d->magic != DMAGIC) {
        uint32_t *w = (uint32_t *)d;
        for (unsigned k = 0; k < sizeof *d / 4; k++)
            w[k] = 0;
        d->drive = drive;
        d->mix = mix;
        d->level = level;
        d->magic = DMAGIC;
    }
    int tone = fw_store_get(SETTINGS, P_D_TONE), tone_on = tone < 990;
    float tone_a = 1.0f;
    if (tone_on) {
        float hz = fw_cutoff_hz((float)tone * 0.001f);
        hz = hz < 20.0f ? 20.0f : hz > 20000.0f ? 20000.0f : hz;
        float w = 6.2831853f * hz / FS;                  /* one-pole: a = 1 - e^-w, ~ w / (1 + w) */
        tone_a = w / (1.0f + w);
    }

    /* per-sample ramps from the last block's values (no zipper) */
    float inv = 1.0f / (float)n;
    float dr = d->drive, dd = (drive - dr) * inv;
    float mx = d->mix, dm = (mix - mx) * inv;
    float lv = d->level, dl = (level - lv) * inv;
    for (int k = 0; k < n; k++) {
        dr += dd;
        mx += dm;
        lv += dl;
        float wl = run1(&d->ch[0], type, l[k], dr, tone_a, tone_on);
        float dryl = d->ch[0].xr[d->ch[0].xp + HLEN - 1];
        l[k] = (dryl + mx * (wl - dryl)) * lv;
        if (r && r != l) {
            float wr = run1(&d->ch[1], type, r[k], dr, tone_a, tone_on);
            float dryr = d->ch[1].xr[d->ch[1].xp + HLEN - 1];
            r[k] = (dryr + mx * (wr - dryr)) * lv;
        }
    }
    d->drive = drive;
    d->mix = mix;
    d->level = level;
}

/* --- SSL-style bus compressor ------------------------------------------------------------------------------------- */
/* Feed-forward VCA design: stereo-linked peak detector (max of |L|, |R|) in dB, static curve with a 2 dB soft knee,
   the gain reduction smoothed with the attack / release switch (decoupled one-pole, in dB). Auto release: a fast
   (0.1 s) and a slow path (attack 300 ms, release 1.2 s); the deeper reduction wins, so short peaks recover fast and
   sustained compression holds. Switch values as on the SSL bus compressor; threshold and makeup (Gain) continuous. */
static const float ssl_ratio[] = {2.0f, 4.0f, 10.0f};
static const float ssl_attack_s[] = {0.0001f, 0.0003f, 0.001f, 0.003f, 0.01f, 0.03f};
static const float ssl_release_s[] = {0.1f, 0.3f, 0.6f, 1.2f};
#define SSL_AUTO   4
#define SSL_KNEE   2.0f
struct sslst { uint32_t magic; float gf, gs; };
#define SST ((struct sslst *)0x2406d180u)
#define SMAGIC 0x53534c21u       /* "SSL!" */

static inline __attribute__((always_inline)) float log2_fast(float x)
{
    union { float f; uint32_t u; } v = {x};
    float e = (float)((int)(v.u >> 23) - 128);              /* the polynomial maps m in [1, 2) to [1, 2) */
    v.u = (v.u & 0x7fffffu) | 0x3f800000u;
    float m = v.f;
    return e + (-0.34484843f * m + 2.02466578f) * m - 0.67487759f;
}

/* one-pole: e^(-d), d = 1 / (t fs) <= 0.21 here; series, as exp2_fast is only good to ~2.5e-4 near 1, which would cap
   every time constant at ~80 ms */
static float coef(float seconds)
{
    float d = 1.0f / (seconds * FS);
    return 1.0f - d * (1.0f - d * (0.5f - d * (1.0f / 6.0f - d * (1.0f / 24.0f))));
}

static int idx(unsigned param, int count, int def)
{
    int v = fw_store_get(SETTINGS, param);
    return v >= 0 && v < count ? v : def;
}

static void ssl_run(void *bus)
{
    struct sslst *s = SST;
    if (s->magic != SMAGIC) {
        s->gf = s->gs = 0.0f;
        s->magic = SMAGIC;
    }
    int n = fw_frames(bus);
    if (n <= 0 || n > 512)
        return;
    float thr = (float)fw_store_get(SETTINGS, P_C_THRESH) * 0.001f;
    float slope = 1.0f / ssl_ratio[idx(P_S_RATIO, 3, 0)] - 1.0f;          /* gain change per dB over: 1/R - 1 */
    float makeup = (float)fw_store_get(SETTINGS, P_C_GAIN) * 0.001f;
    int rel = idx(P_S_REL, 5, SSL_AUTO);
    float aa = coef(ssl_attack_s[idx(P_S_ATTACK, 6, 5)]);
    float ar = coef(rel == SSL_AUTO ? 0.1f : ssl_release_s[rel]);
    float sa = coef(0.3f), sr = coef(1.2f);
    float gf = s->gf, gs = s->gs;
    if (*((const uint8_t *)bus + 0x10)) {                 /* silent: let the reduction recover, signal untouched */
        for (int k = 0; k < n; k++) {
            gf *= ar;
            gs *= sr;
        }
        s->gf = gf;
        s->gs = gs;
        return;
    }
    float *l, *r;
    fw_planes(bus, &l, &r);
    if (!l)
        return;
    if (r == l)
        r = 0;
    for (int k = 0; k < n; k++) {
        float x = l[k] < 0.0f ? -l[k] : l[k];
        if (r) {
            float y = r[k] < 0.0f ? -r[k] : r[k];
            x = y > x ? y : x;
        }
        float over = (x > 1e-6f ? 6.0206f * log2_fast(x) : -120.0f) - thr;
        float gc;
        if (over <= -0.5f * SSL_KNEE)
            gc = 0.0f;
        else if (over < 0.5f * SSL_KNEE) {
            float t = over + 0.5f * SSL_KNEE;
            gc = slope * t * t * (0.5f / SSL_KNEE);
        } else
            gc = slope * over;
        gf = gc < gf ? aa * gf + (1.0f - aa) * gc : ar * gf + (1.0f - ar) * gc;
        float g = gf;
        if (rel == SSL_AUTO) {
            gs = gc < gs ? sa * gs + (1.0f - sa) * gc : sr * gs + (1.0f - sr) * gc;
            g = gs < gf ? gs : gf;
        }
        float lin = exp2_fast((g + makeup) * (1.0f / 6.0206f));
        l[k] *= lin;
        if (r)
            r[k] *= lin;
    }
    s->gf = gf;
    s->gs = gs;
}

/* bitbox MK2: labels of the three new Tools tabs (patches/master.py points the empty label slots here; the builder
   copies 7 characters) */
const char tab_midi[] = "MIDI";            /* stock "MIDIIn" no longer fits with six tabs */
const char tab_comp[] = "Comp";
const char tab_comp2[] = "Cmp2";
const char tab_dist[] = "Dist";
