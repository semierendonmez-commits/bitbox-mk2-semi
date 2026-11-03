/*
 * GEN: a generative step sequencer with step conditions for the bitbox MK2 (bitbox-mk2-semi, own code). 2.3.4.
 *
 * The MK2 has no sequencer of its own (the framework's sequencer code from the blackbox is there but has no MK2
 * screen; 1010music's comparison chart lists none). GEN plays the pads from four generator lanes, locked to the
 * MK2's clock: the engine's song position (engine +0x30, 960 ticks per beat, follows the internal tempo and MIDI
 * clock; tempo at engine +0x18). A hit is the same note message the grid sends when a pad is touched (0x43 note on,
 * 0x42 note off, engine queue 0x080963cc; velocity at +0x16), so the pad plays exactly as from a touch (launch mode,
 * choke, voices).
 *
 * Settings (Tools -> More, pages "Gen", "Gen 2", "Gen 3", "L1 Gen" / "L1 Shape" / "L1 Cond" .. "L4 ..."):
 *   Gen:     Run (Off / On), Fill (Off / On: the Fill conditions), Epoch (Off, 4 / 8 / 16 / 32 bars)
 *   Gen 2:   Seed (0..999), Mutate % (Evolve's change per cycle, Breakcut's variation and jumps)
 *   Gen 3:   Cut (Breakcut rearranges the sound: Off / On), Scale and Range (0..24 semitones) of Call+Resp's melody
 *   Ln Gen:  Algo (Off, Euclid, Breakcut, Call+Resp, Evolve, Drummap, Random), Pad (Sel = the pad selected on the
 *            grid, 1..16: 1 = bottom left, 5 = the row above), Steps (1..32)
 *   Ln Shape: Hits (density 0..32), Rotate (0..31; Drummap: the map's X), Rate (1/32, 1/16, 1/8, 1/4, 1/16T, 1/8T)
 *   Ln Cond: Cond (Always, 1:2, 2:2, 1:3, 2:3, 3:3, 1:4, 2:4, 3:4, 4:4, Fill, !Fill, First, !First), Prob %, Vel
 *
 * Algorithms (all deterministic from Seed and the song position, so they repeat when the song restarts):
 *   Euclid     Hits spread as evenly as possible over Steps, rotated.
 *   Breakcut   BBCut-style: each cycle the steps are cut into segments of 1..4 steps (Hits: more hits = shorter
 *              cuts); every cut starts with a hit, the first of a beat accented; sometimes the last cut becomes a roll
 *              (every step, rising velocity). A new cut each cycle. With Cut On every cut plays a segment of the
 *              sound: in place, a jump to a block of the cut's size, or the last cut again (a stutter); the roll
 *              repeats one segment. Sample pads: segment k of Steps = Start at k / Steps of the sample (the engine's
 *              Start, in frames, as a type 0x39 message; the record keeps the knob, the old Start comes back when
 *              Cut / Run is off). Slicer pads: note 36 + k plays slice k (tested in Renode; note 0 = the selected one).
 *   Call+Resp  3 + 1: three cycles of the call (Euclid of Hits), the fourth the response (the call moved by a
 *              quarter, with a few hits more from Seed). With Range > 0 the call is a melody (a walk over Scale from
 *              the pad's pitch, note 48 + semitones) and the response answers it upside down, ending on the pad's
 *              pitch; on a Slicer pad the semitones pick slices.
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
#define P_CUT    0x1fa                                /* Gen 3: Breakcut rearranges the sample (Start) */
#define P_SCALE  0x1fb                                /* Gen 3: Call+Resp melody scale */
#define P_RANGE  0x1fc                                /* Gen 3: Call+Resp melody range, semitones (0 = no pitch) */
#define P_PROC   0x1fd                                /* Gen 4: Breakcut's cut procedure (BBCut) */
#define P_ROLLS  0x1fe                                /* Gen 4: rolls Off / Normal / Dense / Fade */
#define P_RECUT  0x1ff                                /* Gen 4: new cuts every cycle / 2 / 4 / 8 / locked */
#define P_ACCENT 0x200                                /* Gen 5: Acid accents % */
#define P_SLIDE  0x201                                /* Gen 5: Acid slides % */
#define P_LOCK   0x202                                /* Gen 5: Turing lock -100..100 */
#define P_MAPY   0x203                                /* Gen 6: Drummap Y (X = Rotate) */
#define P_LEARN  0x204                                /* Gen 6: Learn: grid hits teach the Learn lanes */
#define P_TURN   0x205                                /* Gen 6: phrase-end turns every 8 / 16 / 32 bars */
#define P_RESP0  0x206                                /* Ln Resp: Call+Resp's response pad, one per lane */
enum { L_ALGO, L_PAD, L_STEPS, L_HITS, L_ROT, L_RATE, L_COND, L_PROB, L_VEL, NLP };
#define LP(l, k) (P_LANE0 + (l) * NLP + (k))

enum { A_OFF, A_EUCLID, A_BREAK, A_CALL, A_EVOLVE, A_DRUM, A_RANDOM, A_ACID, A_TURING, A_LEARN, NALGO };
static const char *const algo_names[] = {"Off", "Euclid", "Breakcut", "Call+Resp", "Evolve", "Drummap", "Random",
                                         "Acid", "Turing", "Learn"};
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
static const char *const pad_names[] = {"Sel", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12",
                                        "13", "14", "15", "16"};
static const char *const scale_names[] = {"Chromatic", "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolyd",
                                          "Harm Min", "Pent Maj", "Pent Min", "Blues", "Whole"};
static const uint16_t scale_mask[] = {0xfff, 0xab5, 0x5ad, 0x6ad, 0x5ab, 0xad5, 0x6b5, 0x9ad, 0x295, 0x4a9, 0x4e9, 0x555};
#define NSCALE 12
static const char *const cut_names[] = {"Off", "On"};
enum { PR_BASIC, PR_CLASSIC, PR_CHOOSE, PR_JUNGLE, PR_WARP, PR_SQPUSH, PR_CAGE, PR_TIMELINE, PR_PERMUTE, PR_THRASH, NPROC };
static const char *const proc_names[] = {"Basic", "Classic", "ChooseCut", "Jungle", "WarpCut", "SQPusher", "CageCut",
                                         "Timeline", "Permute", "ThrashCut"};
static const char *const rolls_names[] = {"Off", "Normal", "Dense", "Fade"};
enum { RO_OFF, RO_NORMAL, RO_DENSE, RO_FADE };
static const char *const recut_names[] = {"Every", "2 cycles", "4 cycles", "8 cycles", "Locked"};
static const char *const turn_names[] = {"Off", "8 bars", "16 bars", "32 bars"};
static const uint8_t turn_bars[] = {0, 8, 16, 32};
static const char *const resp_names[] = {"Same", "Sel", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12",
                                         "13", "14", "15", "16"};

struct gpdef { uint16_t id; uint8_t type; const char *label; int min, max, def; const char *xml; const char *const *names; };
static const struct gpdef gglobal[] = {
    {P_RUN, 0, "Run:", 0, 2, 0, "semigenrun", onoff_names},
    {P_FILL, 0, "Fill:", 0, 2, 0, "semigenfill", onoff_names},
    {P_EPOCH, 0, "Epoch:", 0, 5, 0, "semigenepoch", epoch_names},
    {P_SEED, 1, "Seed:", 0, 999, 1, "semigenseed", 0},
    {P_MUTATE, 1, "Mutate %:", 0, 100, 25, "semigenmutate", 0},
    {P_CUT, 0, "Cut:", 0, 2, 1, "semigencut", cut_names},
    {P_SCALE, 0, "Scale:", 0, NSCALE, 9, "semigenscale", scale_names},
    {P_RANGE, 1, "Range:", 0, 24, 12, "semigenrange", 0},
    {P_PROC, 0, "Proc:", 0, NPROC, PR_BASIC, "semigenproc", proc_names},
    {P_ROLLS, 0, "Rolls:", 0, 4, RO_NORMAL, "semigenrolls", rolls_names},
    {P_RECUT, 0, "Recut:", 0, 5, 0, "semigenrecut", recut_names},
    {P_ACCENT, 1, "Accent %:", 0, 100, 30, "semigenaccent", 0},
    {P_SLIDE, 1, "Slide %:", 0, 100, 20, "semigenslide", 0},
    {P_LOCK, 1, "Lock:", -100, 100, 70, "semigenlock", 0},
    {P_MAPY, 1, "Map Y:", 0, 31, 0, "semigenmapy", 0},
    {P_LEARN, 0, "Learn:", 0, 2, 0, "semigenlearn", onoff_names},
    {P_TURN, 0, "Turn:", 0, 4, 0, "semigenturn", turn_names},
    {P_RESP0 + 0, 0, "Resp Pad:", 0, 18, 0, "semig1resp", resp_names},
    {P_RESP0 + 1, 0, "Resp Pad:", 0, 18, 0, "semig2resp", resp_names},
    {P_RESP0 + 2, 0, "Resp Pad:", 0, 18, 0, "semig3resp", resp_names},
    {P_RESP0 + 3, 0, "Resp Pad:", 0, 18, 0, "semig4resp", resp_names},
};
#define NGLOBAL (int)(sizeof gglobal / sizeof gglobal[0])
static const struct gpdef glane[NLP] = {
    {0, 0, "Algo:", 0, NALGO, A_OFF, "algo", algo_names},
    {0, 0, "Pad:", 0, 17, 1, "pad", pad_names},
    {0, 1, "Steps:", 1, 32, 16, "steps", 0},
    {0, 1, "Hits:", 0, 32, 4, "hits", 0},
    {0, 1, "Rotate:", 0, 31, 0, "rot", 0},
    {0, 0, "Rate:", 0, NRATE, 1, "rate", rate_names},
    {0, 0, "Cond:", 0, NCOND, C_ALWAYS, "cond", cond_names},
    {0, 1, "Prob %:", 0, 100, 100, "prob", 0},
    {0, 1, "Vel:", 1, 127, 100, "vel", 0},
};
static const int8_t lane_pad[NL] = {1, 2, 5, 9};     /* defaults: pad 1 = bottom left, 2 its right, 5 / 9 above */
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
    uint8_t rate, playing, evo_ok, off_note;
    uint32_t sel_cell;        /* Pad Sel: the last pad cell selected (+0x80000000 = valid) */
    uint32_t cut_cell;        /* Cut: the pad whose Start we moved (+0x80000000 = moved) */
    int32_t cut_orig;         /* its Start (frames) before */
    int32_t sub_next, sub_dt; /* a roll's further hits inside the step: next song position, spacing */
    uint32_t sub_cell;
    uint8_t sub_left, sub_note, sub_vel, slide;   /* hits left; slide: this note ties into the next */
    uint16_t reg;             /* Turing's shift register */
    uint8_t reg_ok, pad1;
    uint32_t shown;           /* this cycle's hits (the saver's Pulse) */
};
struct gen {
    uint32_t magic;
    int32_t last_ticks;
    uint32_t running;
    uint32_t sent;            /* notes sent (diagnostics) */
    struct lane lane[NL];
    int32_t log[16];          /* song position of the last 16 hits (lane << 28 | ticks; diagnostics) */
};
_Static_assert(sizeof(struct gen) <= 0x200, "GEN state fits its slot");
#define GS ((volatile struct gen *)0x2001e940u)

/* Learn: what the grid taught each lane (RAM only): transitions between the gaps (steps) of successive hits, and how
   often each step of the cycle was hit */
struct learn {
    uint8_t ioi[16][16];      /* [previous gap - 1][next gap - 1], counts (halved when one reaches 255) */
    uint8_t phase[32];
    int32_t last_step;        /* the last taught hit (lane steps), -1 none */
    uint8_t last_ioi, any, pad[2];
};
#define GL ((volatile struct learn *)0x2001eb40u)
#define LMAGIC 0x4c524e31u
#define GL_MAGIC (*(volatile uint32_t *)0x2001efe0u)
_Static_assert(4 * sizeof(struct learn) <= 0x4a0, "Learn tables fit their slot");
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

/* Call+Resp melody: degree d of the scale (0 = the pad's pitch) -> semitones */
static int degree_semis(int d, int scale)
{
    unsigned m = scale_mask[scale >= 0 && scale < NSCALE ? scale : 0];
    int notes[12], n = 0;
    for (int k = 0; k < 12; k++)
        if (m >> k & 1)
            notes[n++] = k;
    int oct = d >= 0 ? d / n : -((-d + n - 1) / n);
    return oct * 12 + notes[d - oct * n];
}

static int degrees_in(int range, int scale)            /* scale degrees from 0 within `range` semitones */
{
    int d = 0;
    while (d < 48 && degree_semis(d + 1, scale) <= range)
        d++;
    return d;
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

/* Map Y's far side (own): kick, snare, hat, percussion in a broken / garage feel and a sparse half-time feel */
static const uint8_t dmap2[4][2][32] = {
    {   /* kick */
        {15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0},
        {15, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 13, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    },
    {   /* snare */
        {0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 2, 0, 0, 15, 0, 0, 4, 0, 0, 6, 0},
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0},
    },
    {   /* closed hat */
        {0, 0, 12, 4, 0, 0, 13, 5, 0, 0, 12, 3, 0, 0, 14, 6, 0, 0, 12, 4, 0, 0, 13, 5, 0, 0, 12, 3, 0, 7, 14, 6},
        {13, 0, 0, 0, 9, 0, 0, 0, 13, 0, 0, 0, 9, 0, 0, 0, 13, 0, 0, 0, 9, 0, 0, 0, 13, 0, 0, 0, 9, 0, 4, 0},
    },
    {   /* percussion */
        {0, 0, 0, 11, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 12, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 6, 0, 0, 0, 0, 13, 0},
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0},
    },
};

/* m: bit i = step i hits; vel[i] (0 = the lane's Vel); semi[i]: semitones from the pad's pitch (Call+Resp);
   seg[i]: Breakcut's source segment for the hit at step i (-1 = none) */
struct pat { uint32_t m, slide; uint8_t vel[32]; int8_t semi[32]; int8_t seg[32]; uint8_t roll[32]; };
#define ROLLRUN 0x80                                  /* roll[i]: hits in the step (1..4), | ROLLRUN inside a roll */

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

/* --- Breakcut procedures: Nick Collins' BBCut as in BBenCut / the OpenTracker fork (src/core/gen/gen_bbcut.cpp, our
   own code there), on GEN's step grid: a beat = 4 steps, a bar = 16. A cut is a hit whose sound comes from a segment
   (seg = the step of the source it starts at); a block repeats one segment; cuts shorter than a step become hits
   inside the step (roll = hits per step). Mutate % is each procedure's amount (repeats, rolls, edits, shuffle ...). */
static uint32_t rnd(uint32_t *r)
{
    *r = *r * 1664525u + 1013904223u;
    return *r >> 8;
}
static int below(uint32_t *r, int k) { return k > 0 ? (int)(rnd(r) % (unsigned)k) : 0; }
static int coin(uint32_t *r, int pct) { return (int)(rnd(r) % 100u) < pct; }

/* a cut starting at quarter-step q (4 per step) playing segment src; a second cut in the same step makes it a roll */
static void cut_q(struct pat *p, int n, int q, int src, int run)
{
    int i = q >> 2;
    if (i < 0 || i >= n)
        return;
    src %= n;
    if (src < 0)
        src += n;
    if (p->m >> i & 1) {
        int k = p->roll[i] & 0x7f;
        p->roll[i] = (uint8_t)((k < 4 ? k + 1 : 4) | ROLLRUN);
        return;
    }
    p->m |= 1u << i;
    p->seg[i] = (int8_t)src;
    p->roll[i] = (uint8_t)(1 | (run ? ROLLRUN : 0));
    if (i % 4 == 0)
        p->vel[i] = 127;
}

/* `reps` cuts of `len` steps from step `at`, all from the segment `src` */
static void block(struct pat *p, int n, int at, int reps, int len, int src)
{
    for (int k = 0; k < reps; k++)
        cut_q(p, n, 4 * (at + k * len), src, 0);
}

/* a roll over `len` steps from `at`: `per` hits a step, one segment */
static void rollrun(struct pat *p, int n, int at, int len, int per, int src)
{
    per = per < 1 ? 1 : per > 4 ? 4 : per;
    for (int j = 0; j < len; j++) {
        int i = at + j;
        if (i < 0 || i >= n)
            continue;
        p->m |= 1u << i;
        p->seg[i] = (int8_t)(src % n);
        p->roll[i] = (uint8_t)(per | ROLLRUN);
        p->vel[i] = (uint8_t)(70 + j * 50 / (len > 1 ? len - 1 : 1));
    }
}

static const uint16_t sq_fills[13][6][6] = {          /* SQPusher1's fills: blocks of cut lengths, 1000 = a beat */
    {{750, 750, 750, 750}, {1000}},
    {{500, 1000}, {1000}, {1000, 500}},
    {{500}, {1000, 1000, 1000}, {500}},
    {{571}, {571, 571}, {571, 571}, {571}, {286, 286}},
    {{1000, 500}, {1000, 500}, {500, 500}},
    {{500, 500}, {660, 670, 670}, {1010}},
    {{340}, {330, 330}, {340, 330}, {2330}},
    {{1400}, {400, 400}, {600, 200}, {1000}},
    {{167, 167, 166}, {1000, 1000, 1000}, {500}},
    {{1500, 500, 1000}, {250, 250, 250, 250}},
    {{200, 200}, {400, 400}, {400, 400}, {2000}},
    {{750, 750, 1000}, {250, 250, 250, 250, 250, 250}},
    {{500, 1000}, {500}, {125, 125, 125, 125}, {1000}, {167, 167, 166}},
};
static const uint8_t sq_w[8] = {0, 30, 0, 50, 70, 80, 90, 60};
static const uint8_t cage_forms[4][3] = {{50, 25, 25}, {25, 50, 25}, {25, 25, 50}, {34, 33, 33}};
static const uint8_t timelines[5][5] = {{3, 3, 2}, {3, 2, 3}, {2, 3, 3}, {5, 3}, {2, 2, 3, 1}};
static const uint8_t perms[8][8] = {
    {0, 1, 2, 3, 4, 5, 6, 7}, {0, 1, 2, 3, 0, 1, 6, 7}, {0, 1, 2, 3, 4, 5, 2, 3}, {0, 3, 2, 1, 4, 7, 6, 5},
    {0, 1, 0, 1, 2, 3, 6, 7}, {4, 5, 6, 7, 0, 1, 2, 3}, {0, 1, 6, 7, 4, 5, 2, 3}, {0, 1, 2, 2, 4, 5, 6, 6}};
/* ThrashCut riffs: {steps, type}: 0 kick, 1 snare, 2 kick roll, 3 snare roll, 4 snare roll x2 */
static const uint8_t thrash_db[4][8][2] = {
    {{4, 2}, {8, 1}, {4, 2}, {16, 4}},
    {{2, 2}, {4, 1}, {2, 2}, {2, 2}, {2, 2}, {1, 1}, {3, 1}},
    {{2, 2}, {2, 2}, {2, 2}, {2, 2}, {2, 1}, {2, 2}, {2, 1}, {2, 2}},
    {{2, 2}, {4, 1}, {2, 2}, {2, 1}, {2, 2}, {4, 1}},
};
static const uint8_t thrash_len[4] = {4, 7, 8, 6};

static void bbcut(int proc, int l, int n, int32_t cycle, int seed, struct pat *p)
{
    uint32_t r = hash3((uint32_t)seed + 0x6262u, (uint32_t)l * 16u + (uint32_t)proc, (uint32_t)cycle);
    int A = clampi(setting(P_MUTATE), 0, 100);
    int bars = n >= 16 ? n / 16 : 1, bar_len = n >= 16 ? 16 : n;
    switch (proc) {
    case PR_CLASSIC: {                                 /* BBCutProc11: odd units (1 / 3 eighths) repeated, a roll */
        int unit = n >= 8 ? 2 : 1, total = n / unit, done = 0;
        while (done < total) {
            int left = total - done;
            if (coin(&r, A) && left < 4) {
                rollrun(p, n, done * unit, left * unit, unit == 2 ? 1 : 2, done * unit);
                break;
            }
            int u = coin(&r, 50) ? 1 : 3;
            while (u > left)
                u -= 2;
            if (u < 1)
                u = 1;
            int reps = 1 + below(&r, 2);
            while (reps * u > left) {
                if (--reps <= 1) {
                    reps = 1;
                    u = left;
                }
            }
            block(p, n, done * unit, reps, u * unit, done * unit);
            done += reps * u;
        }
        break;
    }
    case PR_CHOOSE: {                                  /* ChooseCutProc: 1 / 1.5 beat cuts, repeated, an end roll */
        int pos = 0;
        while (pos < n) {
            int left = n - pos;
            if (coin(&r, A) && left < 8) {
                rollrun(p, n, pos, left, 1, pos);
                break;
            }
            int size = coin(&r, 50) ? 4 : 6;
            if (size > left)
                size = left;
            int reps = 1 + below(&r, 2);
            while (reps * size > left) {
                if (--reps <= 1) {
                    reps = 1;
                    size = left;
                }
            }
            block(p, n, pos, reps, size, pos);
            pos += reps * size;
        }
        break;
    }
    case PR_JUNGLE: {                                  /* amen-style edits of each bar in eighths, a fill every 2 bars */
        int amt = A * 2 > 100 ? 100 : A * 2;
        for (int b = 0; b < bars; b++) {
            int base = b * bar_len, src[8];
            for (int k = 0; k < 8; k++)
                src[k] = k;
            if (coin(&r, amt / 2))
                src[3] = 2;                            /* double snare */
            if (coin(&r, amt * 4 / 10))
                src[1] = 0;                            /* double kick */
            if (coin(&r, amt * 35 / 100))
                src[5] = 4;                            /* the kick again on 3-and */
            if (coin(&r, amt * 3 / 10))
                for (int k = 4; k < 8; k++)
                    src[k] = src[k - 4];               /* the first half again */
            if (coin(&r, amt / 4)) {
                src[6] = 2;                            /* a snare pair at the end */
                src[7] = 3;
            }
            if (coin(&r, amt / 5)) {
                int a = 4 + below(&r, 4), c = 4 + below(&r, 4), t = src[a];
                src[a] = src[c];
                src[c] = t;
            }
            int fill = ((cycle * bars + b) & 1) == 1;
            int snare = base + (coin(&r, 50) ? 4 : 12);
            int e = bar_len / 8 ? bar_len / 8 : 1;     /* steps per eighth */
            for (int k = 0; k < 8; k++) {
                if (fill && k >= 6)
                    rollrun(p, n, base + k * e, e, k == 6 ? 1 : 2, snare);
                else
                    block(p, n, base + k * e, 1, e, base + src[k] * e);
            }
        }
        break;
    }
    case PR_WARP: {                                    /* WarpCutProc: straight cuts or fast rolls of 2 / 4 / 8 steps */
        int pos = 0;
        while (pos < n) {
            int x = below(&r, 100), len = x < 50 ? 2 : x < 90 ? 4 : 8;
            if (len > n - pos)
                len = n - pos;
            if (coin(&r, 100 - A)) {
                block(p, n, pos, 1, len, pos);
            } else {
                int reps = len < 4 ? (4 << below(&r, 3)) : (8 << below(&r, 3));
                rollrun(p, n, pos, len, reps / len, pos);
            }
            pos += len;
        }
        break;
    }
    case PR_SQPUSH: {                                  /* SQPusher1: quavers / semiquaver bursts, a fill every 4 bars */
        for (int b = 0; b < bars; b++) {
            int base = 4 * b * bar_len, qlen = 4 * bar_len, at = 0;   /* in quarter steps */
            if (((cycle * bars + b) & 3) == 3) {
                const uint16_t (*f)[6] = sq_fills[below(&r, 13)];
                for (int bl = 0; bl < 6 && f[bl][0]; bl++) {
                    int src = (base + at) >> 2;
                    for (int k = 0; k < 6 && f[bl][k]; k++) {
                        cut_q(p, n, base + at, src, 0);
                        at += (f[bl][k] * 16 + 500) / 1000;
                    }
                }
            } else {
                while (at < qlen) {
                    int quaver = (at / 8) % 8, chance = sq_w[quaver] * A / 50;
                    int group = at % 16 == 8 ? 1 : 1 + below(&r, 2), src = (base + at) >> 2;
                    if (coin(&r, chance))
                        for (int k = 0; k < 2 * group; k++, at += 4)
                            cut_q(p, n, base + at, src, 0);
                    else
                        for (int k = 0; k < group; k++, at += 8)
                            cut_q(p, n, base + at, src, 0);
                }
            }
        }
        break;
    }
    case PR_CAGE: {                                    /* CageCut: 3 blocks x 3 cuts in proportions */
        const uint8_t *f = cage_forms[below(&r, 4)], *g = cage_forms[below(&r, 4)];
        int q = 0, total = 4 * n;
        for (int bl = 0; bl < 3; bl++)
            for (int c = 0; c < 3; c++) {
                int d = total * f[bl] * g[c] / 10000;
                int src = coin(&r, A * 2 > 100 ? 100 : A * 2) ? 2 * below(&r, n / 2 ? n / 2 : 1) : q >> 2;
                cut_q(p, n, q, src, 0);
                q += d > 0 ? d : 1;
            }
        break;
    }
    case PR_TIMELINE: {                                /* TimelineCut: 3+3+2, 3+2+3, 2+3+3, 5+3, 2+2+3+1 */
        const uint8_t *t = timelines[below(&r, 5)];
        int cnt = 0, sum = 0;
        while (cnt < 5 && t[cnt])
            sum += t[cnt++];
        int rot = coin(&r, A * 2 > 100 ? 100 : A * 2) ? below(&r, cnt) : 0, q = 0;
        for (int b = 0; b < bars; b++)
            for (int i = 0; i < cnt; i++) {
                int d = 4 * bar_len * t[(i + rot) % cnt] / sum;
                int src = coin(&r, A) ? 2 * below(&r, n / 2 ? n / 2 : 1) : q >> 2;
                cut_q(p, n, q, src, 0);
                q += d;
            }
        break;
    }
    case PR_PERMUTE: {                                 /* PermuteCut: the eighths in a musical permutation */
        int unit = n / 8 ? n / 8 : 1, riff = coin(&r, A * 2 > 100 ? 100 : A * 2) ? 1 + below(&r, 7) : 0;
        for (int k = 0; k * unit < n; k++) {
            int idx = perms[riff][k % 8];
            if (coin(&r, A / 4))
                idx ^= 1;
            block(p, n, k * unit, 1, unit, (idx + (k / 8) * 8) * unit);
        }
        break;
    }
    case PR_THRASH: {                                  /* ThrashCut: kick / snare riffs, rolls, a stop */
        for (int b = 0; b < bars; b++) {
            int riff = below(&r, 4), len = thrash_len[riff], idx = 0, pos = 0, stopped = 0;
            int sw_a = below(&r, len), sw_b = below(&r, len), fill = coin(&r, 13);
            while (pos < bar_len) {
                int e = idx % len;
                e = e == sw_a ? sw_b : e == sw_b ? sw_a : e;
                int st = thrash_db[riff][e][0], type = thrash_db[riff][e][1];
                idx++;
                if (fill)
                    type = type == 0 ? 1 : type == 1 ? 0 : type == 2 ? 3 : type == 3 ? 2 : type;
                int at = b * bar_len + pos, kick = b * bar_len, snare = b * bar_len + 4;
                if (st > bar_len - pos)
                    st = bar_len - pos;
                if (!stopped && coin(&r, A / 2)) {
                    stopped = 1;                       /* a stop: this block silent */
                } else if (type == 0 || type == 1) {
                    block(p, n, at, 1, st, type ? snare : kick);
                } else {
                    rollrun(p, n, at, st, type == 4 ? 2 : 1, type == 2 ? kick : snare);
                }
                pos += st;
            }
        }
        break;
    }
    default:
        break;
    }
}

/* Rolls: Off = only the first hit of a roll; Dense = twice the hits; Fade = falling velocity through a roll */
static void apply_rolls(struct pat *p, int n)
{
    int mode = setting(P_ROLLS), run = 0;
    for (int i = 0; i < n; i++) {
        if (!(p->m >> i & 1) || !(p->roll[i] & ROLLRUN)) {
            run = 0;
            continue;
        }
        int k = p->roll[i] & 0x7f;
        if (mode == RO_OFF) {
            if (run)
                p->m &= ~(1u << i);
            p->roll[i] = 1;
        } else if (mode == RO_DENSE) {
            p->roll[i] = (uint8_t)((k * 2 > 4 ? 4 : k * 2) | ROLLRUN);
        } else if (mode == RO_FADE) {
            int v = 120 - run * 22;
            p->vel[i] = (uint8_t)(v < 30 ? 30 : v);
        }
        run++;
    }
}

/* the cycle the cuts are drawn for (Recut) */
static int32_t recut_cycle(int32_t cycle)
{
    static const uint8_t every[] = {1, 2, 4, 8};
    int rc = setting(P_RECUT);
    if (rc >= 4)
        return 0;
    return cycle / every[rc < 0 ? 0 : rc];
}

/* --- Acid: a 303 line after TB-3PO (Logarhythm, O_C Hemisphere; the fork's gen_acid.cpp): Hits = density (0..32 ->
   -7..+7: gates 10 + 14 |d| %, the pitch set grows from the root on the negative side, the whole scale from +1, a
   repeat of the last pitch 50 - 6 (d + 7) %), octave jumps 20 % up / down when Range >= 12, Accent % and Slide %.
   Every step keeps its draws; Mutate % re-rolls a step's draws at each cycle with that chance. */
static uint32_t acid_draws(int seed, int l, int i, int32_t cycle)
{
    int mut = clampi(setting(P_MUTATE), 0, 100);
    int32_t g = 0;
    for (int32_t c = cycle; c > 0 && c > cycle - 64; c--)
        if ((int)(hash3((uint32_t)seed + 0x3030u, (uint32_t)(l * 32 + i), (uint32_t)c) % 100u) < mut) {
            g = c;
            break;
        }
    return hash3((uint32_t)seed + 0x303u, (uint32_t)(l * 32 + i), (uint32_t)g);
}

static void acid(int l, int n, int hits, int32_t cycle, int seed, struct pat *p)
{
    int d = hits * 14 / 32 - 7, pd = d + 7 > 8 ? 8 : d + 7, scale = setting(P_SCALE), range = setting(P_RANGE);
    unsigned msk = scale_mask[scale >= 0 && scale < NSCALE ? scale : 0];
    int nd = 0;
    for (int k = 0; k < 12; k++)
        nd += msk >> k & 1;
    int avail = nd <= 1 ? 0 : pd > 7 ? nd - 1 : pd < 2 ? (pd < nd - 1 ? pd : nd - 1) : 0;
    if (nd > 1 && pd >= 2 && pd <= 7) {
        int rg = nd - 3 < 4 ? 4 : nd - 3, a = 3 + (pd - 3) * rg / 4;
        avail = a < 1 ? 1 : a > nd - 1 ? nd - 1 : a;
    }
    int gate = 10 + 14 * (d < 0 ? -d : d), rep = 50 - 6 * pd;
    int accent = setting(P_ACCENT), slide = setting(P_SLIDE), prev = 0, lowest = -1;
    uint32_t lowest_thr = 0xffffffffu;
    for (int i = 0; i < n; i++) {
        uint32_t x = acid_draws(seed, l, i, cycle), y = hash3(x, 0x5151u, 7u);
        int thr = (int)(x & 0xffu) * 100 / 256, semi;
        if (i > 0 && (int)((x >> 8) & 0xffu) * 100 < rep * 256) {
            semi = prev;
        } else {
            int deg = (int)((x >> 16) & 0xffu) * (avail + 1) / 256, o = (int)(x >> 24);
            semi = degree_semis(deg, scale);
            if (range >= 12)
                semi += o < 51 ? 12 : o < 102 ? -12 : 0;
            while (semi > range && semi >= 12)
                semi -= 12;
        }
        prev = semi;
        p->semi[i] = (int8_t)semi;
        if (thr < gate)
            p->m |= 1u << i;
        if ((x & 0xffu) < lowest_thr) {
            lowest_thr = x & 0xffu;
            lowest = i;
        }
        p->vel[i] = (int)(y & 0xffu) * 100 < accent * 256 ? 127 : 88;
        if ((int)((y >> 8) & 0xffu) * 100 < slide * 256)
            p->slide |= 1u << i;
    }
    if (!p->m && lowest >= 0)
        p->m = 1u << lowest;                           /* at least one note */
    for (int i = 0; i < n; i++)                        /* a slide only into a note that plays */
        if (!(p->m >> i & 1) || !(p->m >> ((i + 1) % n) & 1))
            p->slide &= ~(1u << i);
}

/* --- Learn: a Markov chain over the gaps between the hits the grid taught (MarkovDrummer / Pinball 2 / Genotype):
   the next gap is drawn from what followed the last gap, weighted by how often the landing step was hit */
static void learned(int l, int n, int32_t cycle, int seed, struct pat *p, int h, int rot)
{
    volatile struct learn *L = &GL[l];
    if (GL_MAGIC != LMAGIC || !L->any) {
        p->m = euclid(n, h, rot);                      /* nothing learnt yet */
        return;
    }
    uint32_t r = hash3((uint32_t)seed + 0x4c4cu, (uint32_t)l, (uint32_t)cycle);
    int best = 0, bw = -1, total = 0;
    for (int i = 0; i < n && i < 32; i++)              /* start: the most hit of the first beat's steps, or a draw */
        total += L->phase[i];
    int pos = 0;
    if (total) {
        int x = below(&r, total);
        for (int i = 0; i < n && i < 32; i++) {
            x -= L->phase[i];
            if (x < 0) {
                pos = i;
                break;
            }
        }
        for (int i = 0; i < 4 && i < n; i++)
            if (L->phase[i] > bw) {
                bw = L->phase[i];
                best = i;
            }
        if (coin(&r, 60))
            pos = best;
    }
    int prev = L->last_ioi >= 1 && L->last_ioi <= 16 ? L->last_ioi : 4, guard = 0;
    while (pos < n && guard++ < 40) {
        p->m |= 1u << pos;
        p->vel[pos] = (uint8_t)(70 + (L->phase[pos & 31] > 7 ? 57 : L->phase[pos & 31] * 8));
        int w[16], sum = 0;
        for (int g = 1; g <= 16; g++) {
            int c = L->ioi[prev - 1][g - 1];
            w[g - 1] = c ? c * (1 + L->phase[(pos + g) % n & 31]) : 0;
            sum += w[g - 1];
        }
        int gap = prev;
        if (sum) {
            int x = below(&r, sum);
            for (int g = 0; g < 16; g++) {
                x -= w[g];
                if (x < 0) {
                    gap = g + 1;
                    break;
                }
            }
        }
        pos += gap;
        prev = gap;
    }
}

static void make_pattern(int l, int algo, int n, int hits, int rot, int32_t cycle, int en, int seed, struct pat *p)
{
    uint32_t m = 0;
    p->slide = 0;
    for (int i = 0; i < 32; i++) {
        p->vel[i] = 0;
        p->semi[i] = 0;
        p->seg[i] = -1;
        p->roll[i] = 1;
    }
    int h = scaled_hits(hits, en);
    switch (algo) {
    case A_EUCLID:
        m = euclid(n, h, rot);
        break;
    case A_BREAK: {                                    /* cuts of 1..4 steps; more hits = shorter cuts */
        int proc = setting(P_PROC);
        int32_t cr = recut_cycle(cycle);
        if (proc > PR_BASIC && proc < NPROC) {
            p->m = 0;
            bbcut(proc, l, n, cr, seed, p);
            m = p->m;
            break;
        }
        uint32_t r = hash3((uint32_t)seed, (uint32_t)l + 11u, (uint32_t)cr);
        int maxcut = hits >= 12 ? 2 : hits >= 6 ? 3 : 4;
        int mut = clampi(setting(P_MUTATE), 0, 100);
        int stut = mut / 3, jump = stut + mut * 3 / 2;  /* Mutate 25 %: 8 % stutters, 37 % jumps, the rest in place */
        int i = 0, prev = -1;
        while (i < n) {
            r = r * 1664525u + 1013904223u;
            int c = 1 + (int)((r >> 24) % (unsigned)maxcut);
            if (c == 1 && ((r >> 16) & 3))
                c = 2;                                 /* single steps are rare */
            m |= 1u << i;
            p->vel[i] = (i % 4 == 0) ? 127 : 0;
            r = r * 1664525u + 1013904223u;
            int x = (int)((r >> 24) % 100u), src = i;  /* the cut plays the sample from this segment */
            if (i == 0)
                src = 0;                               /* each cycle starts at the top */
            else if (prev >= 0 && x < stut)
                src = prev;                            /* a stutter: the last cut again */
            else if (x < jump)
                src = (int)((r >> 8) % (unsigned)((n + c - 1) / c)) * c;   /* a jump to a block of the cut's size */
            p->seg[i] = (int8_t)(src < n ? src : 0);
            prev = p->seg[i];
            i += c;
        }
        r = r * 1664525u + 1013904223u;
        int roll_p = en >= 100 ? 45 : en >= 75 ? 30 : 15;
        if ((int)((r >> 24) % 100u) < roll_p && n >= 4) {   /* last cut -> a roll: one segment repeated */
            int len = n >= 8 ? 4 : 2;
            int src = (r >> 8) & 1 ? n - len : (int)((r >> 12) % (unsigned)(n / len)) * len;
            for (int j = 0; j < len; j++) {
                m |= 1u << (n - len + j);
                p->vel[n - len + j] = (uint8_t)(60 + j * 60 / len);
                p->seg[n - len + j] = (int8_t)src;
                p->roll[n - len + j] = 1 | ROLLRUN;
            }
        }
        break;
    }
    case A_CALL: {
        uint32_t call = euclid(n, h, rot);
        int range = clampi(setting(P_RANGE), 0, 24), scale = setting(P_SCALE);
        int top = range ? degrees_in(range, scale) : 0;
        /* the call's melody: a walk over the scale (0 .. top degrees) from Seed, the same for the 3 calls; the
           response answers it upside down and comes home to the pad's pitch on its last hit */
        int8_t deg[32];
        uint32_t r = hash3((uint32_t)seed, (uint32_t)l + 29u, (uint32_t)(cycle >> 2));
        int d = 0;
        for (int i = 0; i < 32; i++) {
            r = r * 1664525u + 1013904223u;
            int st = (int)((r >> 24) % 5u) - 2;       /* -2 .. +2 degrees */
            if (i == 0)
                st = (int)((r >> 20) % (unsigned)(top / 2 + 1));
            d = clampi(d + st, 0, top);
            deg[i] = (int8_t)d;
        }
        if ((cycle & 3) != 3) {
            m = call;
            if (top)
                for (int i = 0; i < n; i++)
                    if (call >> i & 1)
                        p->semi[i] = (int8_t)degree_semis(deg[i], scale);
        } else {
            int sh = n / 4 ? n / 4 : 1;
            uint32_t resp = 0;
            for (int i = 0; i < n; i++)
                if (call >> i & 1)
                    resp |= 1u << ((i + sh) % n);
            r = hash3((uint32_t)seed, (uint32_t)l + 23u, (uint32_t)(cycle >> 2));
            int extra = h / 2 + 1;
            for (int k = 0; k < extra; k++) {
                r = r * 1664525u + 1013904223u;
                resp |= 1u << ((r >> 24) % (unsigned)n);
            }
            m = resp;
            if (top) {
                int last = -1;
                for (int i = 0; i < n; i++)
                    if (resp >> i & 1) {
                        int src = (i - sh + n) % n;    /* the call's hit it answers */
                        p->semi[i] = (int8_t)degree_semis(top - deg[src], scale);
                        last = i;
                    }
                if (last >= 0)
                    p->semi[last] = 0;
            }
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
    case A_DRUM: {                                     /* X = Rotate, Y = Map Y: four maps per part, bilinear */
        const uint8_t *a = dmap[l & 3][0], *b = dmap[l & 3][1], *c = dmap2[l & 3][0], *e = dmap2[l & 3][1];
        int x = clampi(rot, 0, 31), y = clampi(setting(P_MAPY), 0, 31);
        int thr = 15 - (h * 15 + 16) / 32;            /* density 32 -> every level >= 0 */
        for (int i = 0; i < n; i++) {
            int s = i * 32 / n;
            int top = a[s] * (31 - x) + b[s] * x, bot = c[s] * (31 - x) + e[s] * x;
            int lev = (top * (31 - y) + bot * y + 480) / 961;
            if (lev > thr && lev > 0) {
                m |= 1u << i;
                p->vel[i] = (uint8_t)(40 + lev * 87 / 15);
            }
        }
        break;
    }
    case A_ACID:
        p->m = 0;
        acid(l, n, hits, cycle, seed, p);
        m = p->m;
        break;
    case A_LEARN:
        p->m = 0;
        learned(l, n, cycle, seed, p, h, rot);
        m = p->m;
        break;
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
    if (algo == A_BREAK)
        apply_rolls(p, n);
}

/* --- notes ---------------------------------------------------------------------------------------------------------- */
/* Pad 1..16: 1 = bottom left, 4 = bottom right, 5 = the row above ... (engine cell: row << 8 | col, row 0 = bottom);
   0 (Sel): the pad selected on the grid (app +0x28: kind << 8 | row << 4 | col), or the last one selected */
#define VALID 0x80000000u
static int lane_cell(volatile struct lane *ln, int pad, uint32_t *cell)
{
    if (pad >= 1 && pad <= 16) {
        *cell = ((uint32_t)((pad - 1) / 4) << 8) | (uint32_t)((pad - 1) % 4);
        return 1;
    }
    unsigned c = *(volatile uint16_t *)(APP + 0x28);
    if (((c >> 8) & 0x1f) == 0 && ((c >> 4) & 0xf) < 4 && (c & 0xf) < 4)
        ln->sel_cell = VALID | (((c >> 4) & 0xf) << 8) | (c & 0xf);
    if (!(ln->sel_cell & VALID))
        return 0;
    *cell = ln->sel_cell & 0xffff;
    return 1;
}

static uint8_t *pad_record(uint32_t cell)
{
    return APP + 0x1840u + ((cell >> 8) & 3) * 0xf0u + (cell & 3) * 0x30u;
}

/* the grid taught a hit on `cell` (gen_learn_note): every Learn lane on that pad notes the gap since its last one */
static void teach(uint32_t cell, int32_t ticks)
{
    if (GL_MAGIC != LMAGIC) {
        volatile uint8_t *b = (volatile uint8_t *)GL;
        for (unsigned k = 0; k < 4 * sizeof(struct learn); k++)
            b[k] = 0;
        for (int l = 0; l < NL; l++)
            GL[l].last_step = -1;
        GL_MAGIC = LMAGIC;
    }
    for (int l = 0; l < NL; l++) {
        uint32_t lc;
        if (setting(LP(l, L_ALGO)) != A_LEARN || !lane_cell(&GS->lane[l], clampi(setting(LP(l, L_PAD)), 0, 16), &lc)
            || lc != cell)
            continue;
        volatile struct learn *L = &GL[l];
        int st = rate_ticks[clampi(setting(LP(l, L_RATE)), 0, NRATE - 1)], n = clampi(setting(LP(l, L_STEPS)), 1, 32);
        int32_t s = (ticks + st / 2) / st;             /* the nearest step */
        L->phase[(s % n) & 31] = L->phase[(s % n) & 31] < 255 ? L->phase[(s % n) & 31] + 1 : 255;
        if (L->last_step >= 0 && s > L->last_step && s - L->last_step <= 16) {
            int gap = (int)(s - L->last_step);
            if (L->last_ioi >= 1 && L->last_ioi <= 16) {
                volatile uint8_t *c = &L->ioi[L->last_ioi - 1][gap - 1];
                if (*c == 255)
                    for (int a = 0; a < 16; a++)
                        for (int b = 0; b < 16; b++)
                            L->ioi[a][b] >>= 1;
                *c = *c + 1;
            }
            L->last_ioi = (uint8_t)gap;
            L->any = 1;
        } else if (L->last_step >= 0 && s > L->last_step) {
            L->last_ioi = 0;                           /* a long pause: a new phrase */
        }
        if (s != L->last_step)
            L->last_step = s;
    }
}



static int queue_ok(uint8_t *eng)
{
    uint32_t w = *(volatile uint32_t *)(eng + Q_WRITE), r = *(volatile uint32_t *)(eng + Q_READ);
    return w - r < 24;
}

/* note message as 0x08094e28 / 0x08094e74: +8 cell, +0xc note (0 = the pad's own pitch as the grid sends it,
   48 + n = n semitones from it), +0x14 u16 1, +0x16 u16 velocity */
static void note(uint8_t *eng, int on, uint32_t cell, int nt, int vel)
{
    int32_t now = *(volatile int32_t *)(eng + E_TICKS);
    uint32_t msg[6] = {0};
    if (!queue_ok(eng))
        return;
    msg[0] = on ? 0x43 : 0x42;
    msg[2] = cell;
    msg[3] = (uint32_t)nt;
    msg[4] = 0;
    msg[5] = 1u | (uint32_t)vel << 16;
    fw_post_note(eng, msg);
    if (on)
        GS->log[GS->sent++ & 15] = now;
}

/* a pad parameter to the engine only (type 0x39 as the knob handler sends it, 0x08094de4): the pad's record (what
   the screen shows and the preset saves) keeps its value */
static void pad_param(uint8_t *eng, uint32_t cell, unsigned param, int value)
{
    uint32_t msg[6] = {0};
    if (!queue_ok(eng))
        return;
    msg[0] = 0x39;
    msg[2] = cell;
    msg[3] = param;
    msg[4] = (uint32_t)value;
    fw_post_note(eng, msg);
}

#define P_START   0x71                                /* the pads' Start, 0..1000 of the sample */
#define P_PADMODE 0x6d                                /* Pad Mode: 0 Sample, 1 Clip, 2 Slicer, 3 Granular */
#define SLICE0    36                                  /* a Slicer pad plays slice k for note 36 + k (0: the selected one) */
/* the engine's Sample pad object for a cell (engine +0xfaf8: list head +8, next +0x10; cell +0x18; the Sample /
   Granular class, vtable 0x080df054); its Start (frames) at +0xf8, the sample's frames at +0x100 */
#define PADS_LIST  0xfaf8
#define VT_SAMPLE  0x080df054u
#define PAD_START  0xf8
#define PAD_FRAMES 0x100
static uint8_t *sample_pad(uint8_t *eng, uint32_t cell)
{
    uint8_t *p = *(uint8_t *const *)(eng + PADS_LIST + 8);
    for (int guard = 0; p && guard < 128; guard++, p = *(uint8_t *const *)(p + 0x10))
        if (*(const uint32_t *)p == VT_SAMPLE && *(const uint32_t *)(p + 0x18) == cell)
            return p;
    return 0;
}

static void cut_restore(uint8_t *eng, volatile struct lane *ln)
{
    if (!(ln->cut_cell & VALID))
        return;
    uint32_t cell = ln->cut_cell & 0xffff;
    ln->cut_cell = 0;
    for (int l = 0; l < NL; l++)                        /* another lane still cutting the same pad: it restores */
        if (GS->lane[l].cut_cell == (VALID | cell)) {
            GS->lane[l].cut_orig = ln->cut_orig;
            return;
        }
    pad_param(eng, cell, P_START, ln->cut_orig);
}


/* --- per audio block (master_dist) --------------------------------------------------------------------------------- */
/* Turing (after the Music Thing Turing Machine; the fork's gen_turing.cpp): a 16-bit shift register clocked once a
   step; bit Steps - 1 comes back into bit 0, flipped with a chance set by Lock (+100 never: a loop of Steps steps,
   0 half the time, -100 always: twice the length, inverted). The low byte is the pitch (Scale, Range); hashes of the
   register give the gate (Hits / Steps), the velocity and the slides, so a locked loop repeats all of them. */
static uint32_t reg_byte(uint16_t reg, uint32_t salt) { return (((uint32_t)reg ^ salt) * 0x9e3779b1u) >> 24; }

static void turing_step(int l, volatile struct lane *ln, int n, int hits, int32_t s, int seed, struct pat *p, int pos)
{
    int len = n > 16 ? 16 : n;
    int flip = (100 - clampi(setting(P_LOCK), -100, 100)) * 5;      /* per mille */
    if (!ln->reg_ok) {
        uint16_t r = (uint16_t)(hash3((uint32_t)seed + 0x7457u, (uint32_t)l, 1u) >> 7);
        ln->reg = r ? r : 0xa5a5u;
        ln->reg_ok = 1;
    }
    uint16_t reg = ln->reg;
    uint16_t bit = (uint16_t)((reg >> (len - 1)) & 1u);
    if (flip >= 1000 || (flip > 0 && (int)(hash3((uint32_t)seed + 0x7458u, (uint32_t)l, (uint32_t)s) % 1000u) < flip))
        bit ^= 1u;
    reg = (uint16_t)((reg << 1) | bit);
    int prev_hit = reg_byte(ln->reg, 0x5bd1u) * 100u < (uint32_t)(hits * 100 / n) * 256u;
    ln->reg = reg;
    int density = hits * 100 / n;
    if (reg_byte(reg, 0x5bd1u) * 100u >= (uint32_t)density * 256u)
        return;
    p->m = 1u << pos;
    int range = clampi(setting(P_RANGE), 0, 24), scale = setting(P_SCALE);
    int top = range ? degrees_in(range, scale) : 0;
    p->semi[pos] = (int8_t)(top ? degree_semis((int)(reg & 0xffu) * (top + 1) / 256, scale) : 0);
    p->vel[pos] = (uint8_t)(127 - (int)(reg_byte(reg, 0x3a6bu) >> 3) * 50 / 31);
    if (prev_hit && (int)reg_byte(reg, 0x1d87u) * 100 < setting(P_SLIDE) * 256)
        p->slide = 1u << pos;
}

/* Turn (TURNAROUND / DROP II): the last beat of every 8 bars (the last two of 16 / 32) turns: a roll, a stutter of
   the beat before, a drop (silence) or a fill (Fill conditions on, Breakcut rolls more); which one from Seed. */
enum { T_NONE, T_ROLL, T_STUTTER, T_DROP, T_FILL };
static int turn_at(int32_t tick, int seed)
{
    int t = setting(P_TURN);
    if (t <= 0 || t >= 4)
        return T_NONE;
    int32_t phrase = turn_bars[t] * BAR, win = t == 1 ? TPB : 2 * TPB;
    if (tick % phrase < phrase - win)
        return T_NONE;
    return T_ROLL + (int)(hash3((uint32_t)seed + 0x7475u, (uint32_t)(tick / phrase), 3u) % 4u);
}

/* hook at the grid's note-on calls (0x08094e28, patches/gen.py): Learn notes the hit, then the stock call */
typedef void (*grid_note_fn)(void *ctx, uint32_t cell, unsigned nt, unsigned x, unsigned vel);
#define fw_grid_note ((grid_note_fn)FN(0x08094e28))
void gen_grid_note(void *ctx, uint32_t cell, unsigned nt, unsigned x, unsigned vel)
{
    uint8_t *eng = ENGINE;
    if (eng && GS->magic == GMAGIC && (cell >> 16) == 0 && ((cell >> 8) & 0xff) < 4 && (cell & 0xff) < 4
        && setting(P_LEARN) == 1)
        teach(cell, *(volatile int32_t *)(eng + E_TICKS));
    fw_grid_note(ctx, cell, nt, x, vel);
}

/* GEN's view for the screen saver's Pulse (ss.c): lane l's steps, position and this cycle's hits */
int gen_lane_view(int l, int *n, int *pos, uint32_t *hits)
{
    volatile struct gen *g = GS;
    uint8_t *eng = ENGINE;
    if (l < 0 || l >= NL || g->magic != GMAGIC || !eng || setting(P_RUN) != 1)
        return 0;
    int algo = setting(LP(l, L_ALGO));
    if (algo <= A_OFF || algo >= NALGO)
        return 0;
    *n = clampi(setting(LP(l, L_STEPS)), 1, 32);
    int32_t s = g->lane[l].step;
    *pos = (int)(s % *n);
    *hits = g->lane[l].shown;
    return 1;
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

    for (int l = 0; l < NL; l++) {                     /* pending note offs, a roll's further hits */
        volatile struct lane *ln = &g->lane[l];
        if (!run || jumped)
            ln->sub_left = 0;
        while (ln->sub_left && ticks - ln->sub_next >= 0) {
            if (ln->off_tick)
                note(eng, 0, ln->off_cell, ln->off_note, 0);
            note(eng, 1, ln->sub_cell, ln->sub_note, ln->sub_vel);
            ln->off_cell = ln->sub_cell;
            ln->off_note = ln->sub_note;
            ln->sub_next += ln->sub_dt;
            ln->sub_left--;
        }
        if (ln->off_tick && (ticks - ln->off_tick >= 0 || !run || jumped)) {
            note(eng, 0, ln->off_cell, ln->off_note, 0);
            ln->off_tick = 0;
            ln->slide = 0;
        }
        if (ln->cut_cell && (!run || setting(P_CUT) != 1 || setting(LP(l, L_ALGO)) != A_BREAK))
            cut_restore(eng, ln);                       /* Cut over: the pad's own Start back */
    }
    if (!run) {
        g->running = 0;
        return;
    }
    int seed = setting(P_SEED), fill_bar;
    int en = energy(ticks, &fill_bar);
    int fill0 = setting(P_FILL) == 1 || fill_bar;

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
            ln->reg_ok = 0;
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
            int pos = s % n, hits = clampi(setting(LP(l, L_HITS)), 0, 32), rot = clampi(setting(LP(l, L_ROT)), 0, 31);
            int turn = turn_at(s * st, seed), fill = fill0 || turn == T_FILL;
            struct pat p;
            if (algo == A_TURING) {
                p.m = p.slide = 0;
                p.vel[pos] = 0;
                p.semi[pos] = 0;
                p.seg[pos] = -1;
                p.roll[pos] = 1;
                turing_step(l, ln, n, hits, s, seed, &p, pos);
            } else {
                int32_t ps = s;
                if (turn == T_STUTTER) {                /* the beat before, again */
                    int32_t beat = TPB / st > 0 ? TPB / st : 1;
                    ps = s - beat >= 0 ? s - beat : s;
                }
                int ppos = (int)(ps % n);
                make_pattern(l, algo, n, hits, rot, ps / n, turn == T_FILL ? 100 : en, seed, &p);
                if (ppos != pos) {                      /* move the stuttered step's hit here */
                    uint32_t b = p.m >> ppos & 1;
                    p.m = b << pos;
                    p.vel[pos] = p.vel[ppos];
                    p.semi[pos] = p.semi[ppos];
                    p.seg[pos] = p.seg[ppos];
                    p.roll[pos] = p.roll[ppos];
                    p.slide = 0;
                }
                if (pos == 0)
                    ln->shown = p.m;
            }
            if (turn == T_DROP)
                continue;                               /* the drop: silence till the phrase turns */
            if (turn == T_ROLL) {
                p.m |= 1u << pos;
                p.roll[pos] = 2;
                p.vel[pos] = (uint8_t)(70 + ((s * st) % TPB) * 57 / TPB);
            }
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
            uint32_t cell;
            int resp = setting(P_RESP0 + l);
            if (algo == A_CALL && (cycle & 3) == 3 && resp >= 1 && resp <= 17) {   /* the response on its own pad */
                if (!lane_cell(ln, resp == 1 ? 0 : resp - 1, &cell))
                    continue;
            } else if (!lane_cell(ln, clampi(setting(LP(l, L_PAD)), 0, 16), &cell)) {
                continue;
            }
            int tie = ln->slide && ln->off_tick;        /* the last note slides into this one: its off after */
            uint32_t tie_cell = ln->off_cell;
            uint8_t tie_note = ln->off_note;
            if (ln->off_tick && !tie)
                note(eng, 0, ln->off_cell, ln->off_note, 0);
            int mode = fast_get(pad_record(cell), P_PADMODE);
            int slicer = mode == 2;
            int nt = p.semi[pos] ? 48 + p.semi[pos] : 0;
            if (slicer && nt)
                nt = SLICE0 + p.semi[pos];              /* a Slicer: the melody picks slices */
            if (algo == A_BREAK && p.seg[pos] >= 0 && setting(P_CUT) == 1 && (mode == 0 || slicer)) {   /* not Clip / Granular */
                if (slicer) {
                    nt = SLICE0 + p.seg[pos];           /* the cut's slice (step k = slice k) */
                } else {
                    uint8_t *sp = sample_pad(eng, cell);
                    int frames = sp ? *(volatile int32_t *)(sp + PAD_FRAMES) : 0;
                    if (frames > 0) {
                        if (ln->cut_cell && ln->cut_cell != (VALID | cell))
                            cut_restore(eng, ln);
                        if (!ln->cut_cell) {
                            int other = 0;              /* the pad's own Start (or what another lane saved) */
                            for (int k = 0; k < NL; k++)
                                if (k != l && g->lane[k].cut_cell == (VALID | cell)) {
                                    ln->cut_orig = g->lane[k].cut_orig;
                                    other = 1;
                                }
                            if (!other)
                                ln->cut_orig = *(volatile int32_t *)(sp + PAD_START);
                        }
                        /* the cut's segment: Start in frames (the engine's unit; the screen's Start stays) */
                        pad_param(eng, cell, P_START, frames / n * p.seg[pos] + frames % n * p.seg[pos] / n);
                        ln->cut_cell = VALID | cell;
                    }
                }
            }
            vel = clampi(vel, 1, 127);
            note(eng, 1, cell, nt, vel);
            if (tie && !(tie_cell == cell && tie_note == nt))
                note(eng, 0, tie_cell, tie_note, 0);
            ln->off_note = (uint8_t)nt;
            ln->off_cell = cell;
            ln->slide = (uint8_t)(p.slide >> pos & 1);
            int len = 1;                                /* note off half a step on; a Breakcut cut: at its end */
            if (algo == A_BREAK)
                while (pos + len < n && !(p.m >> (pos + len) & 1))
                    len++;
            int k = p.roll[pos] & 0x7f;
            k = k < 1 ? 1 : k > 4 ? 4 : k;
            ln->off_tick = s * st + (ln->slide ? st + st / 4 : algo == A_BREAK ? len * st - st / 8 : st / 2);
            if (k > 1) {                                /* a roll: k hits in this step */
                ln->sub_left = (uint8_t)(k - 1);
                ln->sub_dt = st / k;
                ln->sub_next = s * st + st / k;
                ln->sub_cell = cell;
                ln->sub_note = (uint8_t)nt;
                ln->sub_vel = (uint8_t)vel;
                if (algo != A_BREAK)
                    ln->off_tick = s * st + st - st / 8;
            }
            if (!ln->off_tick)
                ln->off_tick = 1;
        }
    }
    g->running = 1;
}
