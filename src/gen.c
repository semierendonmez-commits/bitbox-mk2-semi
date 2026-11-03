/*
 * GEN: a generative step sequencer with step conditions for the bitbox MK2 (bitbox-mk2-semi, own code). 2.3.4.
 *
 * The MK2 has no sequencer of its own (the framework's sequencer code from the blackbox is there but has no MK2
 * screen; 1010music's comparison chart lists none). GEN plays the pads from four generator lanes, locked to the
 * MK2's clock: the engine's song position (engine +0x30, 960 ticks per beat, follows the internal tempo and MIDI
 * clock; tempo at engine +0x18). A hit is the same note message the grid sends when a pad is touched (0x43 note on,
 * 0x42 note off, engine queue 0x080963cc), so the pad plays exactly as from a touch (launch mode, choke, voices).
 *
 * Settings (Tools -> More, pages "Gen", "Gen 2", "L1 Gen" / "L1 Shape" / "L1 Cond" .. "L4 ..."):
 *   Gen:     Run (Off / On), Fill (Off / On: the Fill conditions), Epoch (Off, 4 / 8 / 16 / 32 bars)
 *   Gen 2:   Seed (0..999), Mutate % (Evolve's change per cycle, Breakcut's variation)
 *   Ln Gen:  Algo (Off, Euclid, Breakcut, Call+Resp, Evolve, Drummap, Random), Pad (1..16, top left = 1), Steps (1..32)
 *   Ln Shape: Hits (density 0..32), Rotate (0..31; Drummap: the map's X), Rate (1/32, 1/16, 1/8, 1/4, 1/16T, 1/8T)
 *   Ln Cond: Cond (Always, 1:2, 2:2, 1:3, 2:3, 3:3, 1:4, 2:4, 3:4, 4:4, Fill, !Fill, First, !First), Prob %, Vel
 *
 * Algorithms (all deterministic from Seed and the song position, so they repeat when the song restarts):
 *   Euclid     Hits spread as evenly as possible over Steps, rotated.
 *   Breakcut   BBCut-style: each cycle the steps are cut into segments of 1..4 steps (Hits: more hits = shorter
 *              cuts); every cut starts with a hit, the first of a beat accented; sometimes the last cut becomes a roll
 *              (every step, rising velocity). A new cut each cycle.
 *   Call+Resp  3 + 1: three cycles of the call (Euclid of Hits), the fourth the response (the call moved by a
 *              quarter, with a few hits more from Seed).
 *   Evolve     starts as Euclid and mutates each cycle (Mutate %: steps flipped), pulled back toward Hits.
 *   Drummap    a drum map per lane (lane 1 kick, 2 snare, 3 closed hat, 4 percussion; own maps, two variations
 *              morphed by Rotate): the steps whose level is over the Hits density play, louder steps louder.
 *   Random     each step plays with probability Hits / Steps.
 * Step conditions (per lane, on every hit): A:B plays on the A-th of every B cycles of the lane (cycle = Steps
 * steps), Fill / !Fill with Fill on / off, First / !First on the first cycle after Run (or not); then Prob %.
 * Epoch (EPOCH-lite): a long arc of N bars: the first quarter at half density, the second at 3/4, then full, the last
 * bar a fill (Fill conditions true) - Euclid / Call / Evolve / Random / Drummap densities follow it, Breakcut rolls more.
 */
#include <stdint.h>

#pragma GCC optimize("O2", "no-tree-loop-distribute-patterns")

#define FN(addr) ((addr) | 1u)
typedef void (*post_fn)(void *engine, const void *msg);
typedef void (*reg_list_fn)(void *table, unsigned id, const char *label, const char *const *names, unsigned count,
                            const char *xml);
typedef void (*reg_knob_fn)(void *table, unsigned id, unsigned type, const char *label, int min, int max,
                            const char *xml);
typedef void (*store_add_fn)(void *store, unsigned param, int value);
#define fw_post_note     ((post_fn)FN(0x080963cc))    /* [mk2] engine note queue (0x08094e28 / 0x08094e74 use it) */
#define fw_register_list ((reg_list_fn)FN(0x0806a27c))
#define fw_register_knob ((reg_knob_fn)FN(0x0806a2d4))
#define fw_store_add     ((store_add_fn)FN(0x080893d6))
int fast_get(const void *store, unsigned param);

#define APP       ((uint8_t *)0x24015170u)
#define SETTINGS  (APP + 0x7ed0u)
#define ENGINE    (*(uint8_t *const *)(0x24023cb8u + 0xa204u))   /* [mk2] the engine (0x08094e28: [r0 + 0xa204]) */
#define E_TEMPO   0x18                                /* float BPM */
#define E_TICKS   0x30                                /* song position, 960 per beat */
#define Q_WRITE   0xdcdc                              /* engine note queue: write / read counters (64 entries) */
#define Q_READ    0xdcd8
#define TPB       960
#define BAR       (4 * TPB)

#define NL 4
#define P_RUN    0x1d1
#define P_FILL   0x1d2
#define P_EPOCH  0x1d3
#define P_SEED   0x1d4
#define P_MUTATE 0x1d5
#define P_LANE0  0x1d6                                /* 9 per lane */
enum { L_ALGO, L_PAD, L_STEPS, L_HITS, L_ROT, L_RATE, L_COND, L_PROB, L_VEL, NLP };
#define LP(l, k) (P_LANE0 + (l) * NLP + (k))

enum { A_OFF, A_EUCLID, A_BREAK, A_CALL, A_EVOLVE, A_DRUM, A_RANDOM, NALGO };
static const char *const algo_names[] = {"Off", "Euclid", "Breakcut", "Call+Resp", "Evolve", "Drummap", "Random"};
static const char *const onoff_names[] = {"Off", "On"};
static const char *const epoch_names[] = {"Off", "4 bars", "8 bars", "16 bars", "32 bars"};
static const uint8_t epoch_bars[] = {0, 4, 8, 16, 32};
static const char *const rate_names[] = {"1/32", "1/16", "1/8", "1/4", "1/16T", "1/8T"};
static const uint16_t rate_ticks[] = {120, 240, 480, 960, 160, 320};
#define NRATE 6
static const char *const cond_names[] = {"Always", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4",
                                         "Fill", "!Fill", "First", "!First"};
static const uint8_t cond_a[] = {0, 1, 2, 1, 2, 3, 1, 2, 3, 4};
static const uint8_t cond_b[] = {0, 2, 2, 3, 3, 3, 4, 4, 4, 4};
enum { C_ALWAYS, C_FILL = 10, C_NFILL, C_FIRST, C_NFIRST, NCOND };
static const char *const pad_names[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14",
                                        "15", "16"};

struct gpdef { uint16_t id; uint8_t type; const char *label; int min, max, def; const char *xml; const char *const *names; };
static const struct gpdef gglobal[] = {
    {P_RUN, 0, "Run:", 0, 2, 0, "semigenrun", onoff_names},
    {P_FILL, 0, "Fill:", 0, 2, 0, "semigenfill", onoff_names},
    {P_EPOCH, 0, "Epoch:", 0, 5, 0, "semigenepoch", epoch_names},
    {P_SEED, 1, "Seed:", 0, 999, 1, "semigenseed", 0},
    {P_MUTATE, 1, "Mutate %:", 0, 100, 25, "semigenmutate", 0},
};
#define NGLOBAL (int)(sizeof gglobal / sizeof gglobal[0])
static const struct gpdef glane[NLP] = {
    {0, 0, "Algo:", 0, NALGO, A_OFF, "algo", algo_names},
    {0, 0, "Pad:", 0, 16, 0, "pad", pad_names},
    {0, 1, "Steps:", 1, 32, 16, "steps", 0},
    {0, 1, "Hits:", 0, 32, 4, "hits", 0},
    {0, 1, "Rotate:", 0, 31, 0, "rot", 0},
    {0, 0, "Rate:", 0, NRATE, 1, "rate", rate_names},
    {0, 0, "Cond:", 0, NCOND, C_ALWAYS, "cond", cond_names},
    {0, 1, "Prob %:", 0, 100, 100, "prob", 0},
    {0, 1, "Vel:", 1, 127, 100, "vel", 0},
};
static const int8_t lane_pad[NL] = {12, 13, 8, 4};    /* defaults: bottom row 1st / 2nd pad, row above ... */
static const int8_t lane_hits[NL] = {4, 2, 8, 3};
static const int8_t lane_rot[NL] = {0, 4, 0, 2};
static const char *const lane_xml[NL][NLP] = {
    {"semig1algo", "semig1pad", "semig1steps", "semig1hits", "semig1rot", "semig1rate", "semig1cond", "semig1prob", "semig1vel"},
    {"semig2algo", "semig2pad", "semig2steps", "semig2hits", "semig2rot", "semig2rate", "semig2cond", "semig2prob", "semig2vel"},
    {"semig3algo", "semig3pad", "semig3steps", "semig3hits", "semig3rot", "semig3rate", "semig3cond", "semig3prob", "semig3vel"},
    {"semig4algo", "semig4pad", "semig4steps", "semig4hits", "semig4rot", "semig4rate", "semig4cond", "semig4prob", "semig4vel"},
};

/* --- registration / settings store (from master_register / master_store_add) ------------------------------------ */
void gen_register(void *table)
{
    for (int k = 0; k < NGLOBAL; k++) {
        const struct gpdef *p = &gglobal[k];
        if (p->type == 0)
            fw_register_list(table, p->id, p->label, p->names, p->max, p->xml);
        else
            fw_register_knob(table, p->id, p->type, p->label, p->min, p->max, p->xml);
    }
    for (int l = 0; l < NL; l++)
        for (int k = 0; k < NLP; k++) {
            const struct gpdef *p = &glane[k];
            if (p->type == 0)
                fw_register_list(table, LP(l, k), p->label, p->names, p->max, lane_xml[l][k]);
            else
                fw_register_knob(table, LP(l, k), p->type, p->label, p->min, p->max, lane_xml[l][k]);
        }
}

static int lane_default(int l, int k)
{
    if (k == L_PAD)
        return lane_pad[l];
    if (k == L_HITS)
        return lane_hits[l];
    if (k == L_ROT)
        return lane_rot[l];
    if (k == L_ALGO)
        return l == 0 ? A_EUCLID : A_OFF;
    return glane[k].def;
}

void gen_store_add(void *store)
{
    for (int k = 0; k < NGLOBAL; k++)
        fw_store_add(store, gglobal[k].id, gglobal[k].def);
    for (int l = 0; l < NL; l++)
        for (int k = 0; k < NLP; k++)
            fw_store_add(store, LP(l, k), lane_default(l, k));
}

/* --- state ---------------------------------------------------------------------------------------------------------- */
struct lane {
    int32_t step;             /* last step index done (song position / step ticks) */
    int32_t first_cycle;      /* the cycle Run started in */
    int32_t evo_cycle;        /* cycle of evo */
    uint32_t evo;             /* Evolve's pattern */
    int32_t off_tick;         /* pending note off at this song position, 0 = none */
    uint32_t off_cell;
    uint8_t rate, playing, evo_ok, pad;
};
struct gen {
    uint32_t magic;
    int32_t last_ticks;
    uint32_t running;
    uint32_t sent;            /* notes sent (diagnostics) */
    struct lane lane[NL];
    int32_t log[16];          /* song position of the last 16 hits (lane << 28 | ticks; diagnostics) */
};
#define GS ((volatile struct gen *)0x2001c380u)
#define GMAGIC 0x47454e31u

static inline int setting(unsigned id) { return fast_get(SETTINGS, id); }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static uint32_t hash3(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u) * 0x85ebca77u ^ (c + 0x165667b1u) * 0xc2b2ae3du;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

/* --- patterns: bit i = step i hits; vel[i] (0 = the lane's Vel) --------------------------------------------------- */
static uint32_t euclid(int n, int k, int rot)
{
    uint32_t m = 0;
    if (k <= 0)
        return 0;
    if (k >= n)
        return n >= 32 ? 0xffffffffu : (1u << n) - 1;
    for (int i = 0; i < n; i++) {
        int j = (i + rot) % n;
        if ((j * k) % n < k)
            m |= 1u << i;
    }
    return m;
}

static int popcount(uint32_t m)
{
    int c = 0;
    while (m) {
        m &= m - 1;
        c++;
    }
    return c;
}

/* drum maps: 32 steps, levels 0..15 (own), two variations per part */
static const uint8_t dmap[4][2][32] = {
    {   /* kick */
        {15, 0, 0, 2, 0, 0, 6, 0, 13, 0, 3, 0, 0, 5, 0, 1, 15, 0, 0, 3, 0, 0, 7, 0, 12, 0, 4, 0, 0, 2, 9, 0},
        {15, 0, 4, 0, 0, 0, 11, 0, 0, 3, 13, 0, 0, 0, 6, 0, 14, 0, 0, 5, 0, 0, 10, 2, 0, 0, 12, 0, 4, 0, 0, 7},
    },
    {   /* snare */
        {0, 0, 1, 0, 15, 0, 0, 3, 0, 2, 0, 0, 15, 0, 5, 0, 0, 0, 2, 0, 15, 0, 0, 4, 0, 3, 0, 1, 14, 0, 8, 10},
        {0, 2, 0, 0, 14, 0, 4, 0, 0, 0, 7, 0, 15, 3, 0, 0, 0, 5, 0, 0, 13, 0, 0, 6, 2, 0, 9, 0, 15, 4, 11, 8},
    },
    {   /* closed hat */
        {12, 3, 9, 4, 13, 2, 10, 5, 12, 3, 9, 4, 14, 2, 10, 6, 12, 3, 9, 4, 13, 2, 10, 5, 12, 3, 9, 4, 14, 6, 11, 7},
        {14, 0, 8, 6, 10, 0, 13, 4, 14, 2, 8, 0, 11, 5, 12, 3, 14, 0, 9, 6, 10, 2, 13, 0, 14, 4, 8, 7, 11, 0, 12, 9},
    },
    {   /* percussion */
        {0, 0, 9, 0, 0, 6, 0, 0, 2, 0, 11, 0, 0, 4, 0, 8, 0, 0, 10, 0, 0, 5, 0, 3, 0, 0, 12, 0, 1, 0, 7, 0},
        {0, 7, 0, 0, 4, 0, 10, 0, 0, 3, 0, 12, 0, 0, 6, 0, 2, 0, 9, 0, 0, 11, 0, 0, 5, 0, 0, 8, 0, 13, 0, 1},
    },
};

struct pat { uint32_t m; uint8_t vel[32]; };

/* energy x100 (Epoch) for the song position */
static int energy(int32_t ticks, int *fill_bar)
{
    int e = setting(P_EPOCH);
    *fill_bar = 0;
    if (e <= 0 || e >= 5)
        return 100;
    int bars = epoch_bars[e];
    int bar = (int)((uint32_t)ticks / BAR) % bars;
    *fill_bar = bar == bars - 1;
    int q = bar * 4 / bars;
    return q == 0 ? 50 : q == 1 ? 75 : 100;
}

static int scaled_hits(int hits, int en)
{
    if (hits <= 0)
        return 0;
    int h = (hits * en + 50) / 100;
    return h < 1 ? 1 : h;
}

static void make_pattern(int l, int algo, int n, int hits, int rot, int32_t cycle, int en, int seed, struct pat *p)
{
    uint32_t m = 0;
    for (int i = 0; i < 32; i++)
        p->vel[i] = 0;
    int h = scaled_hits(hits, en);
    switch (algo) {
    case A_EUCLID:
        m = euclid(n, h, rot);
        break;
    case A_BREAK: {                                    /* cuts of 1..4 steps; more hits = shorter cuts */
        uint32_t r = hash3((uint32_t)seed, (uint32_t)l + 11u, (uint32_t)cycle);
        int maxcut = hits >= 12 ? 2 : hits >= 6 ? 3 : 4;
        int i = 0;
        while (i < n) {
            r = r * 1664525u + 1013904223u;
            int c = 1 + (int)((r >> 24) % (unsigned)maxcut);
            if (c == 1 && ((r >> 16) & 3))
                c = 2;                                 /* single steps are rare */
            m |= 1u << i;
            p->vel[i] = (i % 4 == 0) ? 127 : 0;
            i += c;
        }
        r = r * 1664525u + 1013904223u;
        int roll_p = en >= 100 ? 45 : en >= 75 ? 30 : 15;
        if ((int)((r >> 24) % 100u) < roll_p && n >= 4) {   /* last cut -> a roll */
            int len = n >= 8 ? 4 : 2;
            for (int j = 0; j < len; j++) {
                m |= 1u << (n - len + j);
                p->vel[n - len + j] = (uint8_t)(60 + j * 60 / len);
            }
        }
        break;
    }
    case A_CALL: {
        uint32_t call = euclid(n, h, rot);
        if ((cycle & 3) != 3) {
            m = call;
        } else {
            int sh = n / 4 ? n / 4 : 1;
            uint32_t resp = 0;
            for (int i = 0; i < n; i++)
                if (call >> i & 1)
                    resp |= 1u << ((i + sh) % n);
            uint32_t r = hash3((uint32_t)seed, (uint32_t)l + 23u, (uint32_t)(cycle >> 2));
            int extra = h / 2 + 1;
            for (int k = 0; k < extra; k++) {
                r = r * 1664525u + 1013904223u;
                resp |= 1u << ((r >> 24) % (unsigned)n);
            }
            m = resp;
        }
        break;
    }
    case A_EVOLVE: {
        volatile struct lane *ln = &GS->lane[l];
        if (!ln->evo_ok || cycle < ln->evo_cycle || cycle > ln->evo_cycle + 64) {
            ln->evo = euclid(n, h, rot);
            ln->evo_cycle = cycle;
            ln->evo_ok = 1;
        }
        while (ln->evo_cycle < cycle) {                /* one mutation per cycle */
            int32_t c = ln->evo_cycle + 1;
            uint32_t r = hash3((uint32_t)seed, (uint32_t)l + 37u, (uint32_t)c), e = ln->evo;
            int mut = setting(P_MUTATE);
            for (int i = 0; i < n; i++) {
                r = r * 1664525u + 1013904223u;
                if ((int)((r >> 24) % 100u) < mut / 4)
                    e ^= 1u << i;
            }
            int cnt = popcount(e & (n >= 32 ? 0xffffffffu : (1u << n) - 1));
            for (int guard = 0; guard < 64 && (cnt > h + h / 2 + 1 || cnt < h / 2); guard++) {
                r = r * 1664525u + 1013904223u;
                int i = (int)((r >> 24) % (unsigned)n);
                if (cnt > h && (e >> i & 1)) {
                    e &= ~(1u << i);
                    cnt--;
                } else if (cnt < h && !(e >> i & 1)) {
                    e |= 1u << i;
                    cnt++;
                }
            }
            ln->evo = e;
            ln->evo_cycle = c;
        }
        m = ln->evo;
        break;
    }
    case A_DRUM: {
        const uint8_t *a = dmap[l & 3][0], *b = dmap[l & 3][1];
        int x = clampi(rot, 0, 31);
        int thr = 15 - (h * 15 + 16) / 32;            /* density 32 -> every level >= 0 */
        for (int i = 0; i < n; i++) {
            int s = i * 32 / n;
            int lev = (a[s] * (31 - x) + b[s] * x + 15) / 31;
            if (lev > thr && lev > 0) {
                m |= 1u << i;
                p->vel[i] = (uint8_t)(40 + lev * 87 / 15);
            }
        }
        break;
    }
    case A_RANDOM: {
        uint32_t r = hash3((uint32_t)seed, (uint32_t)l + 51u, (uint32_t)cycle);
        for (int i = 0; i < n; i++) {
            r = r * 1664525u + 1013904223u;
            if ((int)((r >> 24) % (unsigned)n) < h)
                m |= 1u << i;
        }
        break;
    }
    default:
        break;
    }
    p->m = m;
}

/* --- notes ---------------------------------------------------------------------------------------------------------- */
static uint32_t pad_cell(int pad)                    /* 0 = top left .. 15 = bottom right; rows count from the bottom */
{
    int row = 3 - pad / 4, col = pad % 4;
    return ((uint32_t)row << 8) | (uint32_t)col;
}

static int queue_ok(uint8_t *eng)
{
    uint32_t w = *(volatile uint32_t *)(eng + Q_WRITE), r = *(volatile uint32_t *)(eng + Q_READ);
    return w - r < 24;
}

static void note(uint8_t *eng, int on, uint32_t cell, int vel)
{
    int32_t now = *(volatile int32_t *)(eng + E_TICKS);
    uint32_t msg[6] = {0};
    if (!queue_ok(eng))
        return;
    msg[0] = on ? 0x43 : 0x42;
    msg[2] = cell;
    msg[3] = 0;                                        /* note 0: as the grid (the pad's own pitch) */
    msg[4] = (uint32_t)vel;
    msg[5] = 1;                                        /* +0x14 = 1 as 0x08094e28 */
    fw_post_note(eng, msg);
    if (on)
        GS->log[GS->sent++ & 15] = now;
}

/* --- per audio block (master_dist) --------------------------------------------------------------------------------- */
void gen_tick(void)
{
    volatile struct gen *g = GS;
    uint8_t *eng = ENGINE;
    if (!eng)
        return;
    if (g->magic != GMAGIC) {
        volatile uint8_t *p = (volatile uint8_t *)g;
        for (unsigned k = 0; k < sizeof(struct gen); k++)
            p[k] = 0;
        g->magic = GMAGIC;
    }
    int32_t ticks = *(volatile int32_t *)(eng + E_TICKS);
    int run = setting(P_RUN) == 1;
    int jumped = ticks < g->last_ticks || ticks - g->last_ticks > 4 * TPB;
    g->last_ticks = ticks;

    for (int l = 0; l < NL; l++) {                     /* pending note offs */
        volatile struct lane *ln = &g->lane[l];
        if (ln->off_tick && (ticks - ln->off_tick >= 0 || !run || jumped)) {
            note(eng, 0, ln->off_cell, 0);
            ln->off_tick = 0;
        }
    }
    if (!run) {
        g->running = 0;
        return;
    }
    int seed = setting(P_SEED), fill_bar;
    int en = energy(ticks, &fill_bar);
    int fill = setting(P_FILL) == 1 || fill_bar;

    for (int l = 0; l < NL; l++) {
        volatile struct lane *ln = &g->lane[l];
        int algo = setting(LP(l, L_ALGO));
        int rate = clampi(setting(LP(l, L_RATE)), 0, NRATE - 1);
        int st = rate_ticks[rate];
        int32_t idx = (int32_t)((uint32_t)ticks / (uint32_t)st);
        if (!g->running || jumped || ln->rate != rate) {
            ln->step = idx - 1;
            ln->rate = (uint8_t)rate;
            ln->evo_ok = 0;
        }
        int n = clampi(setting(LP(l, L_STEPS)), 1, 32);
        if (!g->running)
            ln->first_cycle = idx / n;
        if (algo <= A_OFF || algo >= NALGO) {
            ln->step = idx;
            continue;
        }
        if (idx - ln->step > 4)
            ln->step = idx - 1;                         /* never a burst */
        while (ln->step < idx) {
            int32_t s = ++ln->step;
            int32_t cycle = s / n;
            int pos = s % n;
            struct pat p;
            make_pattern(l, algo, n, clampi(setting(LP(l, L_HITS)), 0, 32), clampi(setting(LP(l, L_ROT)), 0, 31),
                         cycle, en, seed, &p);
            if (!(p.m >> pos & 1))
                continue;
            int c = setting(LP(l, L_COND)), ok = 1;
            if (c > C_ALWAYS && c < C_FILL)
                ok = (int)(cycle % cond_b[c]) == cond_a[c] - 1;
            else if (c == C_FILL)
                ok = fill;
            else if (c == C_NFILL)
                ok = !fill;
            else if (c == C_FIRST)
                ok = cycle == ln->first_cycle;
            else if (c == C_NFIRST)
                ok = cycle != ln->first_cycle;
            int prob = setting(LP(l, L_PROB));
            if (ok && prob < 100)
                ok = (int)(hash3((uint32_t)seed + 977u, (uint32_t)l, (uint32_t)s) % 100u) < prob;
            if (!ok)
                continue;
            int vel = p.vel[pos] ? p.vel[pos] * setting(LP(l, L_VEL)) / 127 : setting(LP(l, L_VEL));
            uint32_t cell = pad_cell(clampi(setting(LP(l, L_PAD)), 0, 15));
            if (ln->off_tick)
                note(eng, 0, ln->off_cell, 0);
            note(eng, 1, cell, clampi(vel, 1, 127));
            ln->off_cell = cell;
            ln->off_tick = s * st + st / 2;
            if (!ln->off_tick)
                ln->off_tick = 1;
        }
    }
    g->running = 1;
}
