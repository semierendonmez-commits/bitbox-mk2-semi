/*
 * FX section (bitbox-mk2-semi, new; bitbox MK2 2.3.4): FX return routing and level, delay types with modulation and
 * tape / BBD colour in the feedback loop, two-page FX cell pages, compressor bus choice.
 *
 * - FX returns: the block driver mixes the FX1 (delay, bus 0xa) and FX2 (reverb, bus 0xb) returns into bus 0
 *   (Out1/2) with 0x080a46d0 (bl at 0x0809816a / 0x0809817a; engine in sl). fx_return (fx_thunk.S passes the engine
 *   and which return) mixes them with a gain (Delay Level; the reverb has its own Level) into the bus the global
 *   "FX Out:" setting picks: Out1/2 (stock), or the FX1/2 jacks (bus 3, mono FX1 = bus 4, FX2 = bus 5; the output
 *   stage 0x080981f2 copies them out after the compressor). Buses: 20 bytes at engine +0x150e4 (0x080a9498).
 * - Delay (object 0x224 bytes, ctor 0x080a85ec, vtable 0x080dee40, core 0x080a89d4 called at 0x080a905e): three
 *   delay lines (struct at obj +0x30, +0x4c, +0x68: +0 buffer, +4 write, +8 read, +0xc fraction, +0x10 target
 *   length, +0x14 size). Reads go through 0x080a91f4(line, bus, left), which glides the read speed (0.5 .. 2) towards
 *   the target length; writes through 0x080a915c(line, bus, left). Ping-pong (obj +0x220) chains 0x30 -> 0x4c (left)
 *   and 0x68 (right); otherwise 0x30 (left), 0x68 (right).
 *   delay_read moves the target by a modulation offset for the read (wow / flutter / chorus: the repeats are pitched,
 *   cumulative through the feedback), then puts it back; delay_write saturates and low-passes what goes onto the
 *   "tape" (the input plus the feedback), so every repeat is darker and softer. Types: Digital (stock, plus the
 *   modulation if Depth / Flutter are up), Tape (saturation, head loss, slow drift), BBD (darker, softer clip).
 * - Pages: the FX cell page (page builder 0x08078ec8, code layer 3) shows the cell record's first 8 entries, 2 x 2 x 2.
 *   The Type list carries a second page: "Tape >" = Tape, page 2. fx_page (from clip_page) removes the other page's
 *   entries; the page is rebuilt live when a list value changes. Reverb: Type list Plate, Room, Room >, Hall, Hall >,
 *   Chamber, Chamber > (reverb.c maps it).
 * - Settings: delay record (app +0x2200, row 0): new entries after its last stock default ("Filter", bl 0x08089952);
 *   FX Out: settings store (app +0x7ed0), Tools -> Rec tab (its free fourth slot).
 */
#include <stdint.h>

#pragma GCC optimize("O2", "no-tree-loop-distribute-patterns")

#define FN(addr) ((addr) | 1u)

typedef void (*register_list_fn)(void *table, int param, const char *label, const char *const *names, int count,
                                 const char *xml);
typedef void (*register_knob_fn)(void *table, int param, int type, const char *label, int min, int max,
                                 const char *xml);
typedef void (*store_add_fn)(void *store, unsigned param, int value);
typedef void *(*bus_fn)(void *engine, unsigned id);
typedef void (*mix_fn)(void *dst, const void *src);
typedef void (*mix_gain_fn)(void *dst, const void *src, float gain);
typedef int (*planes_fn)(void *buf, float **l, float **r);
typedef int (*frames_fn)(void *buf);
typedef void (*line_fn)(void *line, void *bus, int left);
typedef int (*core_fn)(void *obj, void *arg);
typedef void (*list_remove_fn)(void *list, unsigned param);
typedef float (*curve_fn)(float x);
#define fw_register_list ((register_list_fn)FN(0x0806a27c))
#define fw_register_knob ((register_knob_fn)FN(0x0806a2d4))
#define fw_store_add     ((store_add_fn)FN(0x080893d6))
int fast_get(const void *store, unsigned param);       /* fastget.c: 0x08089388 with a cache */
#define fw_store_get fast_get
#define fw_bus           ((bus_fn)FN(0x080a9498))      /* [mk2] */
#define fw_mix           ((mix_fn)FN(0x080a46d0))      /* [mk2] dst += src (silent dst: copy) */
#define fw_mix_gain      ((mix_gain_fn)FN(0x080a4794)) /* [mk2] dst += src x gain (micro 0x080a33ec) */
#define fw_planes        ((planes_fn)FN(0x080a4630))   /* [mk2] */
#define fw_frames        ((frames_fn)FN(0x080a4588))   /* [mk2] */
#define fw_line_read     ((line_fn)FN(0x080a91f4))     /* [mk2] */
#define fw_line_write    ((line_fn)FN(0x080a915c))     /* [mk2] */
#define fw_delay_core    ((core_fn)FN(0x080a89d4))     /* [mk2] */
#define fw_list_remove   ((list_remove_fn)FN(0x080884c8)) /* [mk2] */
#define fw_cutoff_hz     ((curve_fn)FN(0x080a1564))    /* [mk2] knob 0..1 -> Hz */

#define APP        ((uint8_t *)0x24015170u)            /* [mk2] */
#define SETTINGS   (APP + 0x7ed0u)
#define DLY_REC    (APP + 0x2200u)                      /* FX cell record row 0: the delay */
#define FS         48000.0f

/* new parameter ids (free on the MK2: tools/emu_params_mk2.py) */
#define P_DTYPE    0x1ad
#define P_DRATE    0x1ae
#define P_DDEPTH   0x1af
#define P_DFLUT    0x1b0
#define P_DDRIVE   0x1b1
#define P_DTONE    0x1b2
#define P_DLEVEL   0x1b3
#define P_DSPREAD  0x1b4
#define P_FX_OUT   0x1b5

/* stock delay record entries (defaults at 0x08089900): time (0x15 ms / 0x16 beats), feedback, cutoff, width, beat
   sync, ping-pong, filter */
static const uint16_t dly_stock[] = {0x15, 0x16, 0x1b, 0x0e, 0xac, 0x17, 0xab, 0x18, 0};
static const uint16_t dly_ours[] = {P_DRATE, P_DDEPTH, P_DFLUT, P_DDRIVE, P_DTONE, P_DLEVEL, P_DSPREAD, 0};

enum { T_DIGITAL, T_TAPE, T_BBD, TYPES };
static const char *const dtype_names[] = {"Digital", "Dig >", "Tape", "Tape >", "BBD", "BBD >"};
static const char *const fxout_names[] = {"Out1/2", "Dly FX1/2", "Rvb FX1/2", "FX1/2", "Dly1 Rvb2"};
enum { O_MAIN, O_DLY, O_RVB, O_BOTH, O_SPLIT, OUTS };

struct pdef { uint16_t id, def; const char *label, *xml; };
static const struct pdef dly_pdefs[] = {
    {P_DRATE, 300, "Mod Rate:", "dlymodrate"},
    {P_DDEPTH, 0, "Mod Depth:", "dlymoddepth"},
    {P_DFLUT, 0, "Flutter:", "dlyflutter"},
    {P_DDRIVE, 0, "Drive:", "dlydrive"},
    {P_DTONE, 1000, "Tone:", "dlytone"},
    {P_DLEVEL, 500, "Level:", "dlylevel"},
    {P_DSPREAD, 500, "Mod Phase:", "dlymodphase"},
};
#define NDLY ((int)(sizeof dly_pdefs / sizeof dly_pdefs[0]))

/* from master_register (filter.c's registration hook) */
void fx_register(void *table)
{
    fw_register_list(table, P_DTYPE, "Type:", dtype_names, 2 * TYPES, "dlytype");
    for (int k = 0; k < NDLY; k++)
        fw_register_knob(table, dly_pdefs[k].id, 8, dly_pdefs[k].label, 0, 1000, dly_pdefs[k].xml);
    fw_register_list(table, P_FX_OUT, "FX Out:", fxout_names, OUTS, "fxout");
}

/* settings store: FX Out (from master_store_add) */
void fx_store_add(void *store)
{
    fw_store_add(store, P_FX_OUT, O_MAIN);
}

/* delay record: the last stock default (Filter, bl at 0x08089952), then Type and ours */
void delay_defaults_last(void *store, unsigned param, int value)
{
    fw_store_add(store, param, value);
    fw_store_add(store, P_DTYPE, 0);
    for (int k = 0; k < NDLY; k++)
        fw_store_add(store, dly_pdefs[k].id, dly_pdefs[k].def);
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

static inline __attribute__((always_inline)) float sin32(uint32_t ph)
{
    float x = (float)(int32_t)ph * (1.0f / 2147483648.0f);
    float y = 4.0f * x * (1.0f - (x < 0.0f ? -x : x));
    return y * (0.775f + 0.225f * (y < 0.0f ? -y : y));
}

static inline __attribute__((always_inline)) float tanh_r(float x)
{
    x = x > 3.0f ? 3.0f : x < -3.0f ? -3.0f : x;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static int rec_get(unsigned param, int def, int max)
{
    int v = fw_store_get(DLY_REC, param);
    return v < 0 || v > max ? def : v;
}

/* Level knob 0..1000, 500 = 0 dB, +-12 dB (as the reverb's Level) */
static float level_gain(int v)
{
    return exp2_fast(((float)v - 500.0f) * (12.0f / 500.0f / 6.0206f));
}

/* --- FX returns ---------------------------------------------------------------------------------------------------- */
void fx3_process(void *dst);                             /* fx3.c */

/* which: 0 = FX1 (delay, bus 0xa), 1 = FX2 (reverb, bus 0xb); bus0 = bus 0 (as the stock call) */
void fx_return(void *bus0, void *src, void *engine, int which)
{
    int route = fw_store_get(SETTINGS, P_FX_OUT);
    unsigned dest = 0;
    switch (route) {
    case O_DLY:
        dest = which == 0 ? 3 : 0;
        break;
    case O_RVB:
        dest = which == 1 ? 3 : 0;
        break;
    case O_BOTH:
        dest = 3;
        break;
    case O_SPLIT:
        dest = which == 0 ? 4 : 5;
        break;
    default:
        break;
    }
    void *dst = dest ? fw_bus(engine, dest) : bus0;
    float g = which == 0 ? level_gain(rec_get(P_DLEVEL, 500, 1000)) : 1.0f;
    fw_mix_gain(dst, src, g);
    if (which == 1)
        fx3_process(dst);                                /* FX3 (fx3.c) goes where the reverb goes */
}

/* --- delay: modulation and tape colour ---------------------------------------------------------------------------- */
struct dmod {
    uint32_t magic;
    uint8_t *obj;                       /* the delay object of this block (delay_core) */
    uint32_t ph_wow, ph_flut, ph_drift; /* LFO phases */
    float off[3];                       /* this block's read offsets per line (samples) */
    float lp[3], lp2[3];                /* loop filter states per line */
    int ready;
};
#define DM ((struct dmod *)0x2406d040u)  /* [mk2] patch RAM, 0x80 (tests/test_ram.py) */
#define DMAGIC 0x444d4f44u               /* "DMOD" */
_Static_assert(sizeof(struct dmod) <= 0x80, "delay mod state must fit its slot");

static int dly_type(void)
{
    return rec_get(P_DTYPE, 0, 2 * TYPES - 1) >> 1;
}

/* the delay object's process (bl at 0x080a905e): note the object, compute this block's modulation */
int delay_core(uint8_t *obj, void *arg)
{
    struct dmod *m = DM;
    if (m->magic != DMAGIC) {
        uint32_t *w = (uint32_t *)m;
        for (unsigned k = 0; k < sizeof *m / 4; k++)
            w[k] = 0;
        m->magic = DMAGIC;
    }
    m->obj = obj;
    int type = dly_type();
    float rate = 0.05f * exp2_fast((float)rec_get(P_DRATE, 300, 1000) * 0.001f * 7.64f);    /* 0.05 .. 10 Hz */
    float depth = (float)rec_get(P_DDEPTH, 0, 1000) * 0.001f;
    float flut = (float)rec_get(P_DFLUT, 0, 1000) * 0.001f;
    float phase = (float)rec_get(P_DSPREAD, 500, 1000) * 0.001f;                          /* 0 .. 180 degrees */
    const float block = 128.0f;                                                           /* frames per call */
    m->ph_wow += (uint32_t)(rate * block / FS * 4294967296.0f);
    m->ph_flut += (uint32_t)(6.7f * block / FS * 4294967296.0f);
    m->ph_drift += (uint32_t)(0.23f * block / FS * 4294967296.0f);
    float wow_l = depth * depth * 480.0f;                        /* up to 10 ms, squared for a fine low end */
    float flut_d = flut * flut * 24.0f;                          /* up to 0.5 ms at 6.7 Hz */
    float drift = type == T_TAPE ? 6.0f : type == T_BBD ? 3.0f : 0.0f;
    uint32_t sp = (uint32_t)(phase * 2147483648.0f);
    for (int s = 0; s < 2; s++) {
        uint32_t o = s ? sp : 0;
        float v = wow_l * (1.0f + sin32(m->ph_wow + o)) + flut_d * (1.0f + sin32(m->ph_flut + o * 3u))
                  + drift * (1.0f + sin32(m->ph_drift + o / 2u));
        if (s == 0) {
            m->off[0] = v;
            m->off[1] = v;
        } else
            m->off[2] = v;
    }
    m->ready = 1;
    return fw_delay_core(obj, arg);
}

static int line_index(const uint8_t *line)
{
    const uint8_t *obj = DM->obj;
    if (DM->magic != DMAGIC || !obj)
        return -1;
    int d = (int)(line - obj);
    return d == 0x30 ? 0 : d == 0x4c ? 1 : d == 0x68 ? 2 : -1;
}

/* reads (bl at 0x080a8ce4, 0x080a8cf2, 0x080a8dd4, 0x080a8eb6) */
void delay_read(uint8_t *line, void *bus, int left)
{
    int i = line_index(line);
    int pp = i >= 0 && DM->obj[0x220];
    if (i < 0 || !DM->ready || (pp && i == 0)) {          /* ping-pong: modulate the outer lines only */
        fw_line_read(line, bus, left);
        return;
    }
    uint32_t *target = (uint32_t *)(line + 0x10);
    uint32_t size = *(const uint32_t *)(line + 0x14), t = *target;
    float off = DM->off[i];
    if (off < 0.5f) {
        fw_line_read(line, bus, left);
        return;
    }
    uint32_t mt = t + (uint32_t)off;
    if (size > 0x200 && mt > size - 0x100)
        mt = size - 0x100;
    *target = mt;
    fw_line_read(line, bus, left);
    *target = t;
}

/* writes (bl at 0x080a8dc0, 0x080a8dca, 0x080a8dfc, 0x080a8ee8, 0x080a8ef2): colour what goes onto the line */
void delay_write(uint8_t *line, void *bus, int left)
{
    int i = line_index(line);
    int type = dly_type();
    int drive = rec_get(P_DDRIVE, 0, 1000), tone = rec_get(P_DTONE, 1000, 1000);
    int pp = i >= 0 && DM->obj[0x220];
    if (i < 0 || (pp && i == 1) || *((const uint8_t *)bus + 0x10)
        || (type == T_DIGITAL && drive == 0 && tone >= 990)) {           /* ping-pong: colour once (line 0x30) */
        fw_line_write(line, bus, left);
        return;
    }
    int n = fw_frames(bus);
    float *l = 0, *r = 0;
    fw_planes(bus, &l, &r);
    float *x = left ? l : r;
    if (!x || n <= 0 || n > 512) {
        fw_line_write(line, bus, left);
        return;
    }
    float hz = fw_cutoff_hz((float)tone * 0.001f);
    if (type == T_TAPE)
        hz *= 0.55f;
    else if (type == T_BBD)
        hz *= 0.3f;
    hz = hz < 200.0f ? 200.0f : hz > 20000.0f ? 20000.0f : hz;
    float w = 6.2831853f * hz / FS, a = w / (1.0f + w);
    int filt = hz < 19000.0f;
    float g = 1.0f + (float)drive * 0.007f;                  /* 1 .. 8 */
    if (type == T_TAPE)
        g *= 1.5f;
    float ig = 1.0f / g;
    float s1 = DM->lp[i], s2 = DM->lp2[i];
    for (int k = 0; k < n; k++) {
        float y = x[k];
        if (type != T_DIGITAL || drive)
            y = type == T_BBD ? (g * y) / (1.0f + (g * y < 0.0f ? -g * y : g * y)) * ig * 1.4f : tanh_r(g * y) * ig;
        if (filt) {
            s1 += a * (y - s1);
            s2 += a * (s1 - s2);                             /* two poles: 12 dB / oct per pass */
            y = s2;
        }
        x[k] = y;
    }
    DM->lp[i] = s1;
    DM->lp2[i] = s2;
    fw_line_write(line, bus, left);
}

/* --- FX cell pages ---------------------------------------------------------------------------------------------------
 * clip_page asks this for every page; 1 = handled. Delay: Type at the end of page 1 (its free slot), page 2 = ours. */
static int list_has(const uint8_t *list, unsigned param)
{
    unsigned n = *(const uint32_t *)list;
    for (unsigned i = 0; i < n && i < 0x30; i++)
        if (*(const uint16_t *)(list + 4 + 12 * i) == param)
            return 1;
    return 0;
}

/* move an entry to the end of the list (12-byte entries after the u32 count) */
static void list_to_end(uint8_t *list, unsigned param)
{
    unsigned n = *(const uint32_t *)list;
    if (n > 0x30)
        return;
    for (unsigned i = 0; i + 1 < n; i++) {
        uint8_t *e = list + 4 + 12 * i;
        if (*(const uint16_t *)e != param)
            continue;
        uint32_t tmp[3];
        for (int k = 0; k < 3; k++)
            tmp[k] = ((uint32_t *)e)[k];
        for (unsigned j = i; j + 1 < n; j++)
            for (int k = 0; k < 3; k++)
                ((uint32_t *)(list + 4 + 12 * j))[k] = ((uint32_t *)(list + 4 + 12 * (j + 1)))[k];
        for (int k = 0; k < 3; k++)
            ((uint32_t *)(list + 4 + 12 * (n - 1)))[k] = tmp[k];
        return;
    }
}

int reverb_page_filter(void *list, const void *rec);   /* reverb.c */

int fx_page(void *app, const void *msg, uint8_t *list)
{
    unsigned code = *(const uint16_t *)((const uint8_t *)msg + 4);
    unsigned row = (code >> 4) & 0xfu;
    if (((code >> 8) & 0x1fu) != 3 || row >= 5)
        return 0;
    const uint8_t *rec = (const uint8_t *)app + 0x2200u + row * 0x18u;
    if (reverb_page_filter(list, rec))
        return 1;
    if (!list_has(list, P_DTYPE))
        return 0;
    int v = fw_store_get(rec, P_DTYPE);
    if (v & 1) {
        for (const uint16_t *p = dly_stock; *p; p++)
            fw_list_remove(list, *p);
        list_to_end(list, P_DTYPE);
    } else
        for (const uint16_t *p = dly_ours; *p; p++)
            fw_list_remove(list, *p);
    return 1;
}

/* --- live page switch: the dispatcher's knob call (command 0x6e, bl 0x0807f7bc at 0x08080c00) ------------------------
 * A Type change rebuilds only the values of the cells already there (the delay's buttons would stay buttons), so a
 * Type change on the FX cell page shows the page again (screen show 0x0807e930, the back target kept). */
typedef void (*knob_fn)(void *app, const void *msg, unsigned param, int value, int extra);
typedef void (*screen_fn)(void *app, unsigned screen, unsigned a, unsigned b);
#define fw_knob   ((knob_fn)FN(0x0807f7bc))            /* [mk2] */
#define fw_screen ((screen_fn)FN(0x0807e930))          /* [mk2] */
#define SCREEN_ID 0x7ee4u                              /* app: current screen, +1 the previous one */
#define SCR_FXCELL 0x23
#define P_RVB_TYPE 0x178

void fx_knob(uint8_t *app, const void *msg, unsigned param, int value, int extra)
{
    fw_knob(app, msg, param, value, extra);
    if (param == 0x1ba) {                               /* Tools -> More: Page (master.c): show the tab again */
        uint8_t prev = app[SCREEN_ID + 1];
        fw_screen(app, app[SCREEN_ID], 0, 0);
        app[SCREEN_ID + 1] = prev;
        return;
    }
    if ((param == P_DTYPE || param == P_RVB_TYPE) && app[SCREEN_ID] == SCR_FXCELL) {
        uint8_t prev = app[SCREEN_ID + 1];
        fw_screen(app, SCR_FXCELL, 0, 0);
        app[SCREEN_ID + 1] = prev;
    }
}
