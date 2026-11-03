/*
 * More modulation sources (bitbox-mk2-semi). bitbox MK2 2.3.4.
 *
 * Pad modulation: the MOD page's Source list (0x36, 19 names at 0x080c5e0c) is turned into an engine source code by
 * the table at 0x080c5de4 (none 0, EXT1..8 15..22, IN1/2 23/24, TRIG 27, VEL 13, LFO 4, PTCH 25, MODW 26, MVOL 28,
 * MPAN 29, CC 0x8000; 0x0807a830) and back by two searches (0x08083d8a: names, 0x0809182c: the MOD page). The voice
 * start (0x080af360) routes the per-voice codes 4 (the voice's LFO, voice +0x518), 13 (velocity, +0x80) and 14 (+0x88:
 * (note - 60) / 60, set at note on, 0x080b148c) into the voice's parameters; code 14 has no list entry stock.
 * KEY: a 20th list entry for code 14, keyboard tracking (MIDI note relative to C3; e.g. Cutoff follows the keys).
 * ENV: a 21st, code 0x30 (new): the pad's envelope as a modulation source. Pad-level sources are picked in the route
 *   add (0x080af648) by a compare chain (MVOL pad +0x12c, MPAN +0x134, PTCH +0x13c, MODW, ...; 0x080af76e); a thunk at
 *   0x080af77e (the PTCH compare) gives code 0x30 the pad's ENV object (value at +4, as every source). The envelope
 *   follows the pad's own Attack / Decay / Sustain / Release (0x1f, 0x21, 0x22, 0x23, 0..1000) and its gate = any of
 *   its voices playing (Sample / Granular: voice count pad +0x6c; Clip / Slicer: the sub-voices' active bytes pad
 *   +0x260 / +0x670), retriggered when a Sample pad's voice count rises. Updated every audio block (modenv_tick from
 *   master_dist), mono per pad.
 * Tables in the cave, their literals and the counts 19 -> 21 (patches/modsrc.py).
 */
#include <stdint.h>

#define NSRC 24
const uint16_t mod_codes[NSRC] = {0, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 27, 13, 4, 25, 26, 28, 29, 0x8000, 14,
                                  0x30, 0x31, 0x32, 0x33};
const char *const mod_names[NSRC] = {
    "none", "EXT1", "EXT2", "EXT3", "EXT4", "EXT5", "EXT6", "EXT7", "EXT8", "IN1", "IN2", "TRIG", "VEL", "LFO",
    "PTCH", "MODW", "MVOL", "MPAN", "CC", "KEY", "ENV", "CHAOS", "RAND", "NET"};

unsigned pad_setting(const uint8_t *pad, unsigned param);   /* filter.c */

struct envsrc {                /* a source object: value at +4 */
    const uint8_t *pad;
    float value;
    float level;
    uint8_t stage, last_voices, pad1, pad2;
};
#define ENVS ((volatile struct envsrc *)0x2001c200u)      /* [mk2] 16 x 0x10 */
_Static_assert(sizeof(struct envsrc) == 0x10, "16 bytes per pad");
enum { E_IDLE, E_ATTACK, E_DECAY, E_SUSTAIN, E_RELEASE };

/* CHAOS (0x31), RAND (0x32), NET (0x33): per pad, a source object (value at +4) and its state, every audio block
   (modenv_tick).
   CHAOS  a Lorenz attractor (sigma 10, rho 28, beta 8/3; after HetrickCV / NLC chaos), x / 20: -1..1, slow and never
          repeating; the pad's LFO Rate (0x37 ... not used: a fixed 0.3 Hz-ish orbit, each pad on its own start).
   RAND   a new random value -1..1 at every note of the pad (sample and hold, after Random Values / MG-16), held.
   NET    four tanh-coupled nodes with fixed random weights and a slow drive (a small rete-like network, own code),
          node 0: chaotic, slower than CHAOS. */
struct xsrc {
    const uint8_t *pad;
    float value;               /* +4: the source's value */
    float x, y, z;
    uint32_t lcg;
    uint8_t last_voices, kind, pad1, pad2;
    float n[4];
};
#define NX 12                                             /* sources in use at once (a pool: a pad's source is made
                                                             when a route asks for it) */
#define XSRC ((volatile struct xsrc *)0x2001f000u)        /* [mk2] 12 x 0x2c */
_Static_assert(NX * sizeof(struct xsrc) <= 0x2f0, "the source pool fits its slot");
#define XMAGIC (*(volatile uint32_t *)0x2001f2f0u)

/* thunk modsrc_pick_thunk: r1 for the route's code r7 (pad r4); see patches/modsrc.py */
void *modsrc_pick(unsigned code, const uint8_t *pad)
{
    if (code == 0x19)
        return (void *)(pad + 0x13c);                   /* stock: PTCH */
    if (code < 0x30 || code > 0x33)
        return 0;
    uint32_t id = *(const uint32_t *)(pad + 0x18);
    unsigned row = (id >> 8) & 0xff, col = id & 0xff;
    if (row > 3 || col > 3)
        return 0;
    if (code > 0x30) {
        if (XMAGIC != 0x58535243u) {
            volatile uint8_t *b = (volatile uint8_t *)XSRC;
            for (unsigned k = 0; k < NX * sizeof(struct xsrc); k++)
                b[k] = 0;
            XMAGIC = 0x58535243u;
        }
        unsigned k, kind = code - 0x31;
        volatile struct xsrc *x = 0;
        for (k = 0; k < NX; k++)                        /* this pad's source of this kind, or a free one */
            if (XSRC[k].pad == pad && XSRC[k].kind == kind)
                return (void *)&XSRC[k];
        for (k = 0; k < NX && !x; k++)
            if (!XSRC[k].pad)
                x = &XSRC[k];
        if (!x)
            return 0;
        k = (unsigned)(x - XSRC) + (row * 4 + col) * 3;
        {
            x->pad = pad;
            x->kind = (uint8_t)kind;
            x->value = 0.0f;
            x->x = 0.1f + 0.05f * (float)(k % 7);
            x->y = 0.0f;
            x->z = 20.0f + (float)(k % 5);
            x->lcg = 0x9e3779b9u * (k + 1);
            x->last_voices = 0;
            for (int j = 0; j < 4; j++)
                x->n[j] = 0.1f * (float)(j + 1) * ((k & 1) ? -1.0f : 1.0f);
        }
        return (void *)x;
    }
    volatile struct envsrc *e = &ENVS[row * 4 + col];
    if (e->pad != pad) {
        e->pad = pad;
        e->value = e->level = 0.0f;
        e->stage = E_IDLE;
        e->last_voices = 0;
    }
    return (void *)e;
}

/* 0..1000 -> seconds, a cubic curve: 0 -> 1 ms, 500 -> 1.25 s, 1000 -> 10 s */
static float secs(unsigned v)
{
    float x = (float)(v > 1000 ? 1000 : v) * 0.001f;
    return 0.001f + 10.0f * x * x * x;
}

static float tanh_s(float x)                           /* tanh, soft enough: x (27 + x^2) / (27 + 9 x^2) */
{
    x = x > 3.0f ? 3.0f : x < -3.0f ? -3.0f : x;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static int pad_voices(const uint8_t *pad)
{
    unsigned mode = pad_setting(pad, 0x6d);
    if (mode == 1 || mode == 2)
        return (pad[0x260] ? 1 : 0) + (pad[0x670] ? 1 : 0);
    int v = *(const int32_t *)(pad + 0x6c);
    return v < 0 ? 0 : v > 64 ? 64 : v;
}

static void xsrc_tick(void)
{
    static const float wnet[4][4] = {{0.0f, 1.9f, -1.3f, 0.6f}, {-1.7f, 0.0f, 1.4f, -0.8f},
                                     {1.1f, -1.6f, 0.0f, 1.8f}, {-0.9f, 1.2f, -1.5f, 0.0f}};
    if (XMAGIC != 0x58535243u)
        return;
    for (int k = 0; k < NX; k++) {
        volatile struct xsrc *x = &XSRC[k];
        if (!x->pad)
            continue;
        if (x->kind == 0) {                             /* Lorenz, 4 Euler substeps of 0.0016 per block */
            float a = x->x, b = x->y, c = x->z;
            for (int i = 0; i < 4; i++) {
                float dx = 10.0f * (b - a), dy = a * (28.0f - c) - b, dz = a * b - 2.6667f * c;
                a += 0.0016f * dx;
                b += 0.0016f * dy;
                c += 0.0016f * dz;
            }
            x->x = a;
            x->y = b;
            x->z = c;
            float v = a * 0.05f;
            x->value = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
        } else if (x->kind == 1) {                      /* S&H on every note */
            int v = pad_voices(x->pad);
            if (v > x->last_voices) {
                x->lcg = x->lcg * 1664525u + 1013904223u;
                x->value = (float)(int)(x->lcg >> 8) * (2.0f / 16777216.0f) - 1.0f;
            }
            x->last_voices = (uint8_t)v;
        } else {                                        /* the net: n' = tanh(W n + drive), smoothed */
            float in[4];
            x->y += 0.0021f;                            /* a slow drive phase */
            if (x->y > 6.2831853f)
                x->y -= 6.2831853f;
            float drv = x->y < 3.1415927f ? 0.3f : -0.3f;
            for (int i = 0; i < 4; i++) {
                float s = drv * (i & 1 ? -1.0f : 1.0f);
                for (int j = 0; j < 4; j++)
                    s += wnet[i][j] * x->n[j];
                in[i] = tanh_s(s * 1.3f);
            }
            for (int i = 0; i < 4; i++)
                x->n[i] += 0.02f * (in[i] - x->n[i]);
            x->value = x->n[0];
        }
    }
}

/* every audio block (128 frames) */
void modenv_tick(void)
{
    xsrc_tick();
    const float dt = 128.0f / 48000.0f;
    for (int k = 0; k < 16; k++) {
        volatile struct envsrc *e = &ENVS[k];
        const uint8_t *pad = e->pad;
        if (!pad)
            continue;
        unsigned mode = pad_setting(pad, 0x6d);
        int voices, gate;
        if (mode == 1 || mode == 2) {
            voices = (pad[0x260] ? 1 : 0) + (pad[0x670] ? 1 : 0);
        } else {
            voices = *(const int32_t *)(pad + 0x6c);
            voices = voices < 0 ? 0 : voices > 64 ? 64 : voices;
        }
        gate = voices > 0;
        if (gate && (e->stage == E_IDLE || e->stage == E_RELEASE || voices > e->last_voices))
            e->stage = E_ATTACK;                         /* note on (or a new voice): from where it is */
        if (!gate && e->stage != E_IDLE)
            e->stage = E_RELEASE;
        e->last_voices = (uint8_t)voices;
        float lv = e->level, sus = (float)pad_setting(pad, 0x22) * 0.001f;
        sus = sus < 0.0f ? 0.0f : sus > 1.0f ? 1.0f : sus;
        switch (e->stage) {
        case E_ATTACK:
            lv += dt / secs(pad_setting(pad, 0x1f));
            if (lv >= 1.0f) {
                lv = 1.0f;
                e->stage = E_DECAY;
            }
            break;
        case E_DECAY:
            lv -= dt / secs(pad_setting(pad, 0x21)) * (1.0f - sus);
            if (lv <= sus) {
                lv = sus;
                e->stage = E_SUSTAIN;
            }
            break;
        case E_SUSTAIN:
            lv = sus;
            break;
        case E_RELEASE:
            lv -= dt / secs(pad_setting(pad, 0x23));
            if (lv <= 0.0f) {
                lv = 0.0f;
                e->stage = E_IDLE;
            }
            break;
        default:
            lv = 0.0f;
        }
        e->level = lv;
        e->value = lv;
    }
}
