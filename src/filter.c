/* ported to the bitbox MK2 2.3.4 by tools/port_mk2.py */
/*
 * Fidelity (pad crunch + sampler machine modes) + 24 dB state-variable filter, ported to the 1010music bitbox micro,
 * firmware 2.3.4. Based on src/filter.c of blackbox-mod (Copyright (c) 2026 Justin Joe, MIT; see THIRD_PARTY.md),
 * which targets the Blackbox 3.1.9. The DSP is unchanged; addresses, the pad-id range and the patch RAM are the
 * micro's (re/symbols.md). Not ported: the machine-mode input drive (Blackbox 3.1.n rides it on the Saturation knob,
 * which the micro does not have).
 *
 * Stock: every voice runs its pad filter once per audio block (0x080b9548): one RBJ biquad, 12 dB/oct, low-pass
 * when the Filter knob is left of centre and high-pass when right of it, bypassed at centre. This replaces that
 * call (both pad classes) with:
 *   1. Crunch: the pad's Interp list ("Fidelity") gains machine modes and seven fixed rates after Normal / HighQ.
 *      A fixed rate holds the voice's output at a lower sample rate (zero-order hold, no anti-aliasing) and rounds
 *      it to 12 bits, before the filter, so the filter can tame the grit.
 *   2. Filter: 24 dB/oct, two of the trapezoidal state-variable filters from Emilie Gillet's stmlib (dsp/filter.h,
 *      class Svf; MIT licence, Copyright 2014 Emilie Gillet) in series: the first at a fixed Butterworth Q, the second
 *      carrying the Res knob. Cutoff and resonance go through the stock curves, so the Filter and Res knobs keep
 *      their ranges.
 *   3. Drive and a second filter (portkit, new pad parameters on the main page): Drive (0x1c2) pushes the voice into
 *      a tanh saturator before the filters; Filter 2 (0x1c0, bipolar like Filter) and Res 2 (0x1c1) are a second
 *      24 dB filter of the same kind in series after the first. Chain: crunch -> drive -> filter -> filter 2.
 * State lives in the stock filter object, in the part only the stock process function used (re/symbols.md,
 * "Filterobjekt-Layout").
 * Stock firmware treats the new Interp values as HighQ (the engine only tests != 0), so presets stay loadable there.
 */
#include <stdint.h>

#include "codec.h"

#pragma GCC optimize("O2", "no-tree-loop-distribute-patterns")   /* per-sample loops run for every voice; no libc to call */

#define FN(addr) ((addr) | 1u)

typedef float (*curve_fn)(float x);
typedef float (*param_fn)(void *param);
typedef int (*buf_int_fn)(void *buf);
typedef int (*buf_chans_fn)(void *buf, float **left, float **right);
typedef void (*register_fn)(void *table, int param, const char *label, const char *const *names, int count,
                            const char *xml);

#define fw_cutoff_hz  ((curve_fn)FN(0x080a1564))     /* knob 0..1 -> Hz */
#define fw_res_q      ((curve_fn)FN(0x080a15e8))     /* Res 0..1 -> Q */
#define fw_param      ((param_fn)FN(0x080a2d9c))     /* modulated parameter value */
#define fw_buf_silent ((buf_int_fn)FN(0x080a4594))
#define fw_buf_frames ((buf_int_fn)FN(0x080a4588))
#define fw_buf_chans  ((buf_chans_fn)FN(0x080a45d0))
#define fw_register_list ((register_fn)FN(0x0806a27c))

#define FS         48000.0f
#define PI_F       3.14159265f
#define OBJ_PARAMS 0x84          /* filter object -> its parameter block: Filter knob @+8, Res @+0x44 */
#define P_INTERP   0xd2          /* Interp / Fidelity (Blackbox: 0xf1) */
/* New pad parameters. The parameter table holds ids up to 0x1d0 (0x0806a24c), the engine ignores ids above 0x17a
   (0x080b2b42, 0x080a7e84), so the app keeps them in the pad record (presets: xml name) and the engine never sees
   them; filter_process reads them from the record like Interp. */
#define P_FILT2    0x1c0         /* Filter 2: -1000 (low-pass) .. 0 (off) .. 1000 (high-pass), like Filter (0xaa) */
#define P_RES2     0x1c1         /* Res 2: 0 .. 1000, default 500 like Res (0x11) */
#define P_DRIVE    0x1c2         /* Drive: 0 .. 1000 */
#define P_SHIFT    0x1c3         /* Shift (clips, src/clipshift.c): -100 .. 100 ms */
#define P_PLAYLEN  0x1c4         /* Play length (clips, src/clipshift.c): list, 0 = Full */
#define P_FX3SEND  0x1c8
#define P_LFADE    0x183         /* Loop Fade (clips, src/clipshift.c): 0 .. 50 ms, 0 = off */         /* FX3 Send (src/fx3.c): 0 .. 1000, like FX1 / FX2 Send */
#define P_SMODE    0x1c7         /* Stretch Mode (clips, src/stretch.c): Auto / Jump / Smooth */
#define P_STRETCH  0x1c6         /* Stretch (clips, src/clipshift.c): -100 .. 100 % */
extern const char *const playlen_names[129];
static const char *const smode_names[3] = {"Auto", "Jump", "Smooth"};

static const float crunch_rate[] = {16000.0f, 12000.0f, 10000.0f, 8000.0f, 6000.0f, 4000.0f, 2000.0f};
#define CRUNCHES ((int)(sizeof crunch_rate / sizeof crunch_rate[0]))
#define STOCK_INTERP 2           /* Normal, HighQ */

/*
 * Machine modes: the old samplers' pitch path, not just their output rate.
 *   grid    the machine's sample rate. The voice reader (machine_read) plays the pad's sample as if it had been
 *           sampled at this rate: it reads the source only on that grid (after a 5-tap anti-alias FIR at 0.45 x grid),
 *           so pitching skips or doubles stored samples like the SP's carry-adder pitch logic, and the aliasing moves
 *           with the note.
 *   hold    1: the DAC runs at the grid rate whatever the pitch (SP-1200, SP-12, MPC60): zero-order hold at grid
 *           after the voice. 0: a per-voice variable-rate DAC (S950): no fixed hold; the reader interpolates linearly
 *           between grid samples, standing in for its rate-tracking smoothing filter.
 *   out     output stage, an RBJ low-pass {b0, b1, a1, a2} at 48 kHz (b2 = b0): SP-1200 12 kHz Q 1.25, SP-12
 *           12.5 kHz Q 1.2, S950 13.5 kHz Q 0.75, MPC60 16 kHz Q 0.9.
 *   SP Raw  SP1200 without the output filter, like the SP's unfiltered outputs 7-8.
 * All 12-bit. MPC60 is the least documented: 40 kHz 12-bit with the same skip/double pitch is a best guess.
 */
struct machine {
    float grid;
    uint8_t hold, linear, raw;
    float fir[5];
    float out[4];
};
static const struct machine machines[] = {
    {26040.0f, 1, 0, 0, {0.00302f, 0.24563f, 0.5027f, 0.24563f, 0.00302f}, {0.357143f, 0.714286f, 0.0f, 0.428571f}},
    {27500.0f, 1, 0, 0, {-0.00396f, 0.24215f, 0.52363f, 0.24215f, -0.00396f}, {0.376262f, 0.752523f, 0.092392f, 0.412654f}},
    {31250.0f, 0, 1, 0, {-0.02035f, 0.2289f, 0.58291f, 0.2289f, -0.02035f}, {0.361304f, 0.722608f, 0.235922f, 0.209295f}},
    {40000.0f, 1, 0, 0, {-0.03947f, 0.16746f, 0.74402f, 0.16746f, -0.03947f}, {0.506372f, 1.012744f, 0.675162f, 0.350325f}},
    {26040.0f, 1, 0, 1, {0.00302f, 0.24563f, 0.5027f, 0.24563f, 0.00302f}, {0.0f, 0.0f, 0.0f, 0.0f}},     /* SP Raw */
};
#define MACHINES ((int)(sizeof machines / sizeof machines[0]))
#define FIRST_MACHINE STOCK_INTERP                      /* machines before the plain crunches, as in the mod */
#define FIRST_CRUNCH  (STOCK_INTERP + MACHINES)
/* Codec modes after the crunches (codec.c): Phone (8 kHz, mu-law, 3.4 kHz low-pass), ADPCM (IMA 4-bit at 22 kHz),
   u-Law (mu-law at 16 kHz) */
enum { CD_PHONE, CD_ADPCM, CD_ULAW, CODECS };
#define FIRST_CODEC   (STOCK_INTERP + MACHINES + CRUNCHES)
#define NINTERP       (STOCK_INTERP + MACHINES + CRUNCHES + CODECS)
static const float phone_lp[4] = {0.037338f, 0.074677f, -1.383901f, 0.533378f};   /* RBJ low-pass 3.4 kHz, Q 0.707 */

const char *const interp_names[NINTERP] = {
    (const char *)0x080c3590u, (const char *)0x080c3598u,       /* stock "Normal", "HighQ" */
    "SP1200", "SP-12", "S950", "MPC60", "SP Raw",
    "16k", "12k", "10k", "8k", "6k", "4k", "2k",
    "Phone", "ADPCM", "u-Law",
};

/* Replaces the Interp list registration call (0x0806b73e): 2 -> 14 entries, label "Interp:" -> "Fidelity:". The xml
   name "interpqual" stays, so presets are unaffected. */
typedef void (*register_knob_fn)(void *table, int param, int type, const char *label, int min, int max,
                                 const char *xml);
#define fw_register_knob ((register_knob_fn)FN(0x0806a2d4))  /* type 8: 0..max, type 9: bipolar (Filter 0xaa) */

void master_register(void *table);
void slicer_register(void *table);

void interp_register(void *table, int param, const char *label, const char *const *names, int count, const char *xml)
{
    (void)label;
    (void)names;
    (void)count;
    fw_register_list(table, param, "Fidelity:", interp_names, NINTERP, xml);
    /* the new pad parameters, registered next to it (same table) */
    fw_register_knob(table, P_FILT2, 9, "Filter 2:", -1000, 1000, "pkfilt2");
    fw_register_knob(table, P_RES2, 8, "Res 2:", 0, 1000, "pkres2");
    fw_register_knob(table, P_DRIVE, 8, "Drive:", 0, 1000, "pkdrive");
    fw_register_knob(table, P_SHIFT, 1, "Shift ms:", -100, 100, "pkshift");   /* type 1: integer, like Transpose */
    fw_register_list(table, P_PLAYLEN, "Play Len:", playlen_names, 129, "pkplaylen");
    fw_register_knob(table, P_STRETCH, 1, "Stretch %:", -100, 100, "pkstretch");
    fw_register_list(table, P_SMODE, "Str Mode:", smode_names, 3, "pkstretchmode");
    fw_register_knob(table, P_FX3SEND, 8, "FX3 Send:", 0, 1000, "fx3send");
    fw_register_knob(table, P_LFADE, 1, "Loop Fade:", 0, 50, "pkloopfade");    /* ms, clips (clipshift.c) */
    master_register(table);                                                   /* master.c: compressor, distortion */
    slicer_register(table);                                                   /* slicer.c: Sync Grid */
}

/* Pad record init (0x08089400): its two pad blocks add Filter (0xaa) through 0x080893d6 (0x080895b0, 0x080897da);
   the new parameters go in right after it, with their defaults. Records hold 64 entries (0x08089356), pads use 58.
   Presets without them keep these defaults: the reader (0x08082e90) inits the record, then sets only the attributes
   it finds. */
typedef void (*store_add_fn)(void *store, unsigned param, int value);
#define fw_store_add ((store_add_fn)FN(0x080893d6))

void pad_store_add(void *store, unsigned param, int value)
{
    fw_store_add(store, param, value);
    fw_store_add(store, P_FILT2, 0);
    fw_store_add(store, P_RES2, 500);
    fw_store_add(store, P_DRIVE, 0);
    fw_store_add(store, P_SHIFT, 0);
    fw_store_add(store, P_PLAYLEN, 0);
    fw_store_add(store, P_STRETCH, 0);
    fw_store_add(store, P_SMODE, 0);
    fw_store_add(store, P_FX3SEND, 0);
    fw_store_add(store, 0x1b6, 0);                                            /* slicer.c: Sync Grid (Free) */
    fw_store_add(store, 0x1b7, 0);                                            /* slicer.c: Slice Str (Cut) */
    fw_store_add(store, P_LFADE, 0);   /* 67 in the full pad record (58 stock + 9); capacity raised to 72 (sound.py) */
}

struct fstate {                  /* at filter object +4..+0x44; the stock coefficient sets and states used to live here */
    uint32_t magic;
    float g, r, drive;           /* smoothed: SVF frequency coefficient, damping of the resonant stage, Drive 0..1 */
    float s1[2][2], s2[2][2];    /* integrator states [stage][channel] */
    float phase, held[2];        /* crunch: hold phase and held sample per channel */
    uint32_t on;                 /* filter was running last block; the stock voice-start reset (0x080b98bc) zeroes it */
};
#define MAGIC 0x53564632u        /* "SVF2" */
#define R_BUTTERWORTH 1.84776f   /* 1 / 0.5412: first stage of a 4-pole Butterworth */
#define SMOOTH 0.33333f          /* per-block coefficient smoothing, as the stock filter */

_Static_assert(4 + sizeof(struct fstate) == 0x44, "fstate at +4..+0x43");

/* Filter 2's state, in the two parts of the object nobody else uses: +0x44..+0x5f (zeroed with `on` by the voice-start
   reset 0x080b98bc, so on2 = 0 there) and +0x74..+0x83 (after the machine output stage's ostate). */
struct f2head {
    float g, r;                  /* smoothed, as in fstate */
    uint32_t on;                 /* running last block */
    float s1[2], s2[2];          /* first stage, per channel */
};
struct f2tail {
    float s1[2], s2[2];          /* second (resonant) stage */
};
#define F2HEAD 0x44
#define F2TAIL 0x74
_Static_assert(F2HEAD + sizeof(struct f2head) <= 0x60, "f2head inside the reset range +0x40..+0x5f");

/* tan(x) for 0 <= x < 1.45, Pade (3,2); 3% low at the top of the range, exact enough below. */
static float tan_approx(float x)
{
    float x2 = x * x;
    return x * (15.0f - x2) / (15.0f - 6.0f * x2);
}

static float quantize12(float x)
{
    float s = x * 2048.0f;
    int i = (int)(s + (s >= 0.0f ? 0.5f : -0.5f));
    return (float)i * (1.0f / 2048.0f);
}

/* Output stage of a machine mode. State in the filter object at +0x60..+0x73, which only the stock process (replaced)
   and its coefficient helper 0x080b93a4 (called only from it) used. */
struct ostate {
    uint32_t magic;
    float z[2][2];
};
#define OMAGIC 0x4d4f5554u       /* "MOUT" */

_Static_assert(0x60 + sizeof(struct ostate) <= F2TAIL, "ostate must stay below filter 2's tail");
_Static_assert(F2TAIL + sizeof(struct f2tail) <= OBJ_PARAMS, "f2tail must stay below the parameter pointer at +0x84");

static void out_stage(uint8_t *obj, float **ch, int n, const float *c)
{
    struct ostate *o = (struct ostate *)(obj + 0x60);
    if (o->magic != OMAGIC) {
        o->z[0][0] = o->z[0][1] = o->z[1][0] = o->z[1][1] = 0.0f;
        o->magic = OMAGIC;
    }
    float b0 = c[0], b1 = c[1], a1 = c[2], a2 = c[3];
    for (int k = 0; k < 2; k++) {
        float *x = ch[k];
        if (!x)
            continue;
        float z1 = o->z[k][0], z2 = o->z[k][1];
        for (int i = 0; i < n; i++) {                       /* transposed direct form II */
            float in = x[i], y = b0 * in + z1;
            z1 = b1 * in - a1 * y + z2;
            z2 = b0 * in - a2 * y;
            x[i] = y;
        }
        o->z[k][0] = z1;
        o->z[k][1] = z2;
    }
}

/* the Codec modes: sample and hold at the codec's rate, each new sample through the codec; ADPCM keeps its
   predictor in the machine output stage's slot (obj +0x60, unused by these modes) */
struct adstate {
    uint32_t magic;
    struct adpcm a[2];
};
#define AMAGIC 0x41445043u       /* "ADPC" */
_Static_assert(0x60 + sizeof(struct adstate) <= F2TAIL, "adstate must stay below filter 2's tail");

static void codec_mode(uint8_t *obj, struct fstate *st, float **ch, int n, int mode)
{
    static const float rates[CODECS] = {8000.0f, 22050.0f, 16000.0f};
    struct adstate *ad = (struct adstate *)(obj + 0x60);
    if (mode == CD_ADPCM && ad->magic != AMAGIC) {
        ad->a[0].pred = ad->a[1].pred = 0;
        ad->a[0].index = ad->a[1].index = 0;
        ad->magic = AMAGIC;
    }
    float step = rates[mode] / FS, phase = st->phase;
    float held0 = st->held[0], held1 = st->held[1];
    for (int i = 0; i < n; i++) {
        phase += step;
        if (phase >= 1.0f) {
            phase -= 1.0f;
            if (mode == CD_ADPCM) {
                held0 = adpcm_q(&ad->a[0], ch[0][i]);
                if (ch[1])
                    held1 = adpcm_q(&ad->a[1], ch[1][i]);
            } else {
                held0 = ulaw_q(ch[0][i]);
                if (ch[1])
                    held1 = ulaw_q(ch[1][i]);
            }
        }
        ch[0][i] = held0;
        if (ch[1])
            ch[1][i] = held1;
    }
    st->phase = phase;
    st->held[0] = held0;
    st->held[1] = held1;
    if (mode == CD_PHONE)
        out_stage(obj, ch, n, phone_lp);
}

static void crunch(struct fstate *st, float **ch, int n, float rate)
{
    float step = rate / FS, phase = st->phase;
    float held0 = st->held[0], held1 = st->held[1];
    for (int i = 0; i < n; i++) {
        phase += step;
        if (phase >= 1.0f) {
            phase -= 1.0f;
            held0 = quantize12(ch[0][i]);
            if (ch[1])
                held1 = quantize12(ch[1][i]);
        }
        ch[0][i] = held0;
        if (ch[1])
            ch[1][i] = held1;
    }
    st->phase = phase;
    st->held[0] = held0;
    st->held[1] = held1;
}

/*
 * stmlib Svf::Process<FILTER_MODE_LOW_PASS / HIGH_PASS> for one channel with coefficients fixed for the block.
 */
#define SVF_LOOP(OUT)                                                                                              \
    for (int i = 0; i < n; i++) {                                                                                  \
        float hp = (x[i] - rg * s1 - s2) * h;                                                                      \
        float bp = g * hp + s1;                                                                                    \
        s1 = g * hp + bp;                                                                                          \
        float lp = g * bp + s2;                                                                                    \
        s2 = g * bp + lp;                                                                                          \
        x[i] = OUT;                                                                                                \
    }

static void svf(float *x, int n, float g, float r, float *s1p, float *s2p, int highpass)
{
    float h = 1.0f / (1.0f + r * g + g * g), rg = r + g, s1 = *s1p, s2 = *s2p;
    if (highpass) {
        SVF_LOOP(hp)
    } else {
        SVF_LOOP(lp)
    }
    *s1p = s1;
    *s2p = s2;
}

/* tanh, rational (musicdsp "rational tanh approximation"): x (27 + x^2) / (27 + 9 x^2), clamped at |x| = 3 where it
   meets +-1 with zero slope. Error a few percent, one division. */
static inline __attribute__((always_inline)) float tanh_r(float x)
{
    x = x > 3.0f ? 3.0f : x < -3.0f ? -3.0f : x;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/*
 * Drive: gain 1 + 15 d^2 (up to +24 dB) into tanh, then makeup towards 1 / tanh(gain) so a full-scale peak stays
 * near full scale while quieter parts come up; the first 10 % of the knob fade in from dry, so there is no step.
 */
static void drive_stage(float *x, int n, float d)
{
    float gain = 1.0f + 15.0f * d * d;
    float makeup = (1.0f - d) + d / tanh_r(gain);
    float wet = d * 10.0f;
    wet = wet > 1.0f ? 1.0f : wet;
    for (int i = 0; i < n; i++) {
        float y = tanh_r(x[i] * gain) * makeup;
        x[i] += wet * (y - x[i]);
    }
}

/* knob (-1 .. 1, 0 = off: low-pass left, high-pass right) and Res (0 .. 1) -> SVF g and r, through the stock curves */
static void svf_coeffs(float knob, float res, float *g, float *r)
{
    float hz = fw_cutoff_hz(knob < 0.0f ? knob + 1.0f : knob);
    float q = fw_res_q(res);
    if (hz < 10.0f)
        hz = 10.0f;
    if (hz > 0.45f * FS)
        hz = 0.45f * FS;
    if (!(q > 0.1f))
        q = 0.1f;                                            /* the stock curve's own minimum (Res at 0) */
    if (q > 40.0f)
        q = 40.0f;
    *g = tan_approx(PI_F * hz / FS);
    *r = 1.0f / q;
}

/*
 * The pad's Interp setting, read from the app's record of the pad (what the screen shows and presets save), not from
 * the engine's copy at pad +0x564: the engine's pad reset (0x080b2802, message type 0x40; same as the Blackbox's
 * FUN_080596b0) sets that copy back to Normal.
 * Engine pad id (pad +0x18) = row << 8 | col, row and col 0..3; app pad record = app + 0x1840 + row * 0xf0 + col * 0x30
 * (init loop 0x08079fbc), read with the stock store lookup 0x08089388.
 */
typedef int (*setting_fn)(const void *store, unsigned param);
int fast_get(const void *store, unsigned param);       /* fastget.c: 0x08089388 with a cache */
#define fw_setting fast_get
#define APP_PADS   (0x24015170u + 0x1840u)

unsigned pad_setting(const uint8_t *pad, unsigned param)
{
    if (!pad)
        return 0;
    uint32_t id = *(const uint32_t *)(pad + 0x18);
    unsigned row = (id >> 8) & 0xff, col = id & 0xff;
    if (row >= 4 || col >= 4)
        return 0;
    return (unsigned)fw_setting((const void *)(APP_PADS + row * 0xf0u + col * 0x30u), param);
}

static unsigned pad_interp(const uint8_t *pad)
{
    return pad_setting(pad, P_INTERP);
}

static float clamp1(float x, float lo)
{
    return x < lo ? lo : x > 1.0f ? 1.0f : x;
}

/* Replaces the per-voice filter call. pad = the pad object the voice belongs to (thunks in filter_thunk.S). */
void filter_process(uint8_t *obj, void *buf, const uint8_t *pad)
{
    if (fw_buf_silent(buf))
        return;
    struct fstate *st = (struct fstate *)(obj + 4);
    struct f2head *f2 = (struct f2head *)(obj + F2HEAD);
    struct f2tail *t2 = (struct f2tail *)(obj + F2TAIL);
    if (st->magic != MAGIC) {
        uint32_t *w = (uint32_t *)st;
        for (unsigned i = 0; i < sizeof *st / 4; i++)
            w[i] = 0;
        f2->on = 0;
        st->magic = MAGIC;
    }
    unsigned interp = pad_interp(pad);
    int crunching = interp >= STOCK_INTERP && interp < NINTERP;
    uint8_t *params = *(uint8_t **)(obj + OBJ_PARAMS);
    float knob = params ? fw_param(params + 8) : 0.0f;
    float knob2 = clamp1((float)(int)pad_setting(pad, P_FILT2) * 0.001f, -1.0f);
    float drive = clamp1((float)(int)pad_setting(pad, P_DRIVE) * 0.001f, 0.0f);
    if (knob == 0.0f)
        st->on = 0;
    if (knob2 == 0.0f)
        f2->on = 0;
    if (drive == 0.0f)
        st->drive = 0.0f;
    if (knob == 0.0f && knob2 == 0.0f && drive == 0.0f && !crunching)
        return;

    float *ch[2] = {0, 0};
    int n = fw_buf_frames(buf);
    fw_buf_chans(buf, &ch[0], &ch[1]);
    if (!ch[0] || n <= 0)
        return;
    if (interp >= FIRST_MACHINE && interp < FIRST_MACHINE + MACHINES) {
        const struct machine *m = &machines[interp - FIRST_MACHINE];
        if (m->hold) {
            crunch(st, ch, n, m->grid);
        } else {
            for (int k = 0; k < 2; k++)
                if (ch[k])
                    for (int i = 0; i < n; i++)
                        ch[k][i] = quantize12(ch[k][i]);
        }
        if (!m->raw)
            out_stage(obj, ch, n, m->out);
    } else if (interp >= FIRST_CODEC && interp < NINTERP) {
        codec_mode(obj, st, ch, n, interp - FIRST_CODEC);
    } else if (crunching) {
        crunch(st, ch, n, crunch_rate[interp - FIRST_CRUNCH]);
    }

    if (drive > 0.0f) {
        st->drive += SMOOTH * (drive - st->drive);
        for (int c = 0; c < 2; c++)
            if (ch[c])
                drive_stage(ch[c], n, st->drive);
    }

    if (knob != 0.0f && params) {
        float g, r;
        svf_coeffs(knob, fw_param(params + 0x44), &g, &r);
        if (!st->on) {
            st->g = g;
            st->r = r;
            for (int k = 0; k < 2; k++)
                st->s1[k][0] = st->s1[k][1] = st->s2[k][0] = st->s2[k][1] = 0.0f;
            st->on = 1;
        } else {
            st->g += SMOOTH * (g - st->g);
            st->r += SMOOTH * (r - st->r);
        }
        int hp = knob > 0.0f;
        for (int c = 0; c < 2; c++) {
            if (!ch[c])
                continue;
            svf(ch[c], n, st->g, R_BUTTERWORTH, &st->s1[0][c], &st->s2[0][c], hp);
            svf(ch[c], n, st->g, st->r, &st->s1[1][c], &st->s2[1][c], hp);
        }
    }

    if (knob2 != 0.0f) {
        float g, r;
        svf_coeffs(knob2, clamp1((float)(int)pad_setting(pad, P_RES2) * 0.001f, 0.0f), &g, &r);
        if (!f2->on) {
            f2->g = g;
            f2->r = r;
            f2->s1[0] = f2->s1[1] = f2->s2[0] = f2->s2[1] = 0.0f;
            t2->s1[0] = t2->s1[1] = t2->s2[0] = t2->s2[1] = 0.0f;
            f2->on = 1;
        } else {
            f2->g += SMOOTH * (g - f2->g);
            f2->r += SMOOTH * (r - f2->r);
        }
        int hp = knob2 > 0.0f;
        for (int c = 0; c < 2; c++) {
            if (!ch[c])
                continue;
            svf(ch[c], n, f2->g, R_BUTTERWORTH, &f2->s1[c], &f2->s2[c], hp);
            svf(ch[c], n, f2->g, f2->r, &t2->s1[c], &t2->s2[c], hp);
        }
    }
}

/*
 * The voice reader. Sample voices read their sample through 0x080aeca8, which fetches the source into a scratch buffer
 * and resamples it with one of two kernels chosen by the engine's Interp copy (pad +0x564 != 0 -> HighQ):
 * 0x080a27a8 (Normal) or 0x080a283c (HighQ). Both take (rate s0, src r0, n_src r1, int pos r2, int *newpos r3,
 * float *frac, float *dst, int n); the reading point is src[pos + frac] and moves by |rate| per output frame.
 * voice_read (below) parks the reader's pad, absolute source position and signed rate here, so the machine grid is
 * anchored on the sample itself, not on each block's scratch buffer.
 * At rate exactly 1.0 the stock code copies without a kernel; the hold after the voice then does the same job.
 */
struct read_park {
    const uint8_t *pad;
    int32_t pos;
    float rate;
};
#define PARK ((volatile struct read_park *)0x2001ad00u)     /* MK2: above the stack top (tests/test_ram.py);
                                                               MPU device memory, aligned */

typedef void (*kernel_fn)(float rate, const float *src, int n_src, int pos, int *newpos, float *frac, float *dst,
                          int n);
#define fw_kernel_normal ((kernel_fn)FN(0x080a27a8))
#define fw_kernel_hq     ((kernel_fn)FN(0x080a283c))

static inline float floor_f(float x)
{
    int i = (int)x;
    return (float)(i - (x < (float)i));
}

static inline int round_i(float x)
{
    return (int)(x + (x >= 0.0f ? 0.5f : -0.5f));
}

static void machine_read(const struct machine *m, float rate, const float *src, int n_src, int pos, int *newpos,
                         float *frac, float *dst, int n)
{
    /* scratch index -> absolute source position. Forward: scratch[0] = source[P - 4]. Reverse: the reader fetched
       from round(P) - n_src + 4 and reversed the buffer, so scratch[j] = source[round(P) + 3 - j]. The micro's reader
       uses the same offsets as the Blackbox's (0x080aed28 / 0x080aeee0). */
    int32_t base = PARK->pos - 4;
    float sgn = 1.0f;
    if (PARK->rate < 0.0f) {
        sgn = -1.0f;
        base = PARK->pos + 3;
    }
    /* Anchor once per call in double (the M7 FPU has it), then work relative to the grid point below. */
    double gd = 48000.0 / (double)m->grid;
    double kb = (double)(int32_t)((double)base / gd);
    if (kb * gd > (double)base)
        kb -= 1.0;
    float a0 = (float)((double)base - kb * gd);      /* 0 <= a0 < g: where the scratch sits on the grid */
    float g = (float)gd, inv_g = m->grid * (1.0f / 48000.0f);
    const float *h = m->fir;
    int hi = n_src - 3;
    float f = *frac;
    int idx = pos;
    for (int i = 0; i < n; i++) {
        float gp = (a0 + sgn * ((float)idx + f)) * inv_g, k0 = floor_f(gp);
        int j0 = round_i(sgn * (k0 * g - a0));
        j0 = j0 < 2 ? 2 : j0 > hi ? hi : j0;
        const float *s = src + j0;
        float v = h[0] * s[-2] + h[1] * s[-1] + h[2] * s[0] + h[3] * s[1] + h[4] * s[2];
        if (m->linear) {
            int j1 = round_i(sgn * ((k0 + 1.0f) * g - a0));
            j1 = j1 < 2 ? 2 : j1 > hi ? hi : j1;
            s = src + j1;
            float w = h[0] * s[-2] + h[1] * s[-1] + h[2] * s[0] + h[3] * s[1] + h[4] * s[2];
            v += (gp - k0) * (w - v);
        }
        dst[i] = v;
        float adv = rate + f;
        int step = (int)adv;
        idx += step;
        f = adv - (float)step;
    }
    *frac = f;
    *newpos = idx;
}

typedef int (*voice_read_fn)(float frac, float rate, const uint8_t *pad, void *src, int pos, int a6, int frames,
                             int offset, void *scratch, void *out);
#define fw_voice_read ((voice_read_fn)FN(0x080aeca8))

static const struct machine *pad_machine(const uint8_t *pad)
{
    unsigned interp = pad_interp(pad);
    return interp >= FIRST_MACHINE && interp < FIRST_MACHINE + MACHINES ? &machines[interp - FIRST_MACHINE] : 0;
}

/*
 * Replaces the calls to the sample-voice reader 0x080aeca8 (@0x080afeee, 0x080b0134, 0x080b01f6): park the pad,
 * absolute position and signed rate for the kernels. At exactly the original pitch the stock reader copies the source
 * without a kernel; in a machine mode the rate goes in as the next float above 1.0, so those voices still read through
 * the machine's grid and anti-alias taps. The voice's own position bookkeeping is the caller's, unaffected.
 * Same argument list as the Blackbox's reader: s0, s1, r0-r3 and four stack words.
 */
int voice_read(float frac, float rate, const uint8_t *pad, void *src, int pos, int a6, int frames, int offset,
               void *scratch, void *out)
{
    PARK->pad = pad;
    PARK->pos = pos;
    PARK->rate = rate;
    if (rate == 1.0f && pad_machine(pad))
        rate = 1.00000012f;
    return fw_voice_read(frac, rate, pad, src, pos, a6, frames, offset, scratch, out);
}

static const struct machine *read_machine(void)
{
    return pad_machine(PARK->pad);
}

/* Replace the kernel calls in 0x080aeca8: Normal @0x080aef5c / 0x080aef80 / 0x080aefa4, HighQ @0x080aeda8 /
   0x080aee34 / 0x080aee58. */
void read_normal(float rate, const float *src, int n_src, int pos, int *newpos, float *frac, float *dst, int n)
{
    const struct machine *m = read_machine();
    if (m)
        machine_read(m, rate, src, n_src, pos, newpos, frac, dst, n);
    else
        fw_kernel_normal(rate, src, n_src, pos, newpos, frac, dst, n);
}

void read_hq(float rate, const float *src, int n_src, int pos, int *newpos, float *frac, float *dst, int n)
{
    const struct machine *m = read_machine();
    if (m)
        machine_read(m, rate, src, n_src, pos, newpos, frac, dst, n);
    else
        fw_kernel_hq(rate, src, n_src, pos, newpos, frac, dst, n);
}
