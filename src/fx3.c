/* ported to the bitbox MK2 2.3.4 by hand (from portkit's micro fx3.c; MK2 addresses are tagged [mk2]) */
/*
 * FX3: a third send effect (portkit, MK2 placement by bitbox-mk2-semi). A second reverb with every reverb type
 * (src/reverb.c) next to the stock FX1 (delay) and FX2 (reverb).
 *
 * - Cell: tap FX2 on the pad grid once to select it, again to turn it into FX3 (label "FX 3", cell 0x330 = FX row 3);
 *   once more back to FX2. INFO opens the FX3 cell page (the stock FX cell page, screen 0x23, record app +0x2248, a
 *   reverb record: its eight stock inits are made type 0x44 instead of an empty type 0). The FX grid widgets are made
 *   once at boot (four of 0x1a0 bytes, label "FX %d", cell id at +0x38); fx3_label (bl at 0x08072130) notes the FX2
 *   widget, fx3_set_cell (the dispatcher's set-cell call, bl 0x0807d0c8 at 0x08080ac8) swaps its cell id and label.
 * - Send: FX3 Send (0x1c8, pad setting, Pos list). Every pad mixes its voices into a scratch buffer and from there
 *   into the send buses, then into its output, but only when one of its sends is on (sample pads: check at
 *   0x080b2438, clip pads 0x080b8eb4); the checks also look at FX3 Send now, and the final mix (0x080b3658,
 *   0x080b916e) first adds scratch x FX3 gain to FX3's own bus (patch RAM, not an engine bus: the MK2 has 14).
 * - Effect: from fx_return (fx.c) after the reverb's return, into the same destination (FX Out). Plate state and a
 *   pre-delay ring from the firmware's heap (bump allocator 0x08043b4c) at boot, only if more than 4.5 MB stay free.
 */
#include <stdint.h>

#pragma GCC optimize("O2", "no-tree-loop-distribute-patterns")

#define FN(addr) ((addr) | 1u)

typedef void (*mix_fn)(void *dst, const void *src);
typedef void (*mix_gain_fn)(void *dst, const void *src, float gain);
typedef int (*frames_fn)(void *buf);
typedef void *(*alloc_fn)(unsigned size, unsigned n);
typedef unsigned (*free_fn)(void);
typedef void (*init_fn)(float *state);
typedef float (*curve_fn)(float x);
typedef void (*cell_fn)(void *app, const void *msg);
typedef void (*text_fn)(void *widget, const char *text);
typedef void (*store_init_fn)(void *store, const uint8_t *type);
#define fw_mix        ((mix_fn)FN(0x080a46d0))        /* [mk2] */
#define fw_mix_gain   ((mix_gain_fn)FN(0x080a4794))   /* [mk2] */
#define fw_frames     ((frames_fn)FN(0x080a4588))     /* [mk2] */
#define fw_alloc      ((alloc_fn)FN(0x08043b4c))      /* [mk2] heap (bump) */
#define fw_heap_free  ((free_fn)FN(0x08043bd0))       /* [mk2] */
#define fw_plate_init ((init_fn)FN(0x080aae74))       /* [mk2] */
#define fw_send_curve ((curve_fn)FN(0x080a1868))      /* [mk2] send value / 1000 -> gain */
#define fw_set_cell   ((cell_fn)FN(0x0807d0c8))       /* [mk2] app command 0x111: app +0x28 = msg +4 */
#define fw_set_text   ((text_fn)FN(0x0805d8cc))       /* [mk2] */
int fast_get(const void *store, unsigned param);
#define fw_store_get fast_get
#define fw_store_init ((store_init_fn)FN(0x08089e7c)) /* [mk2] */

unsigned pad_setting(const uint8_t *pad, unsigned param);                                   /* filter.c */
void reverb_run_record(float *state, const void *rec, const float *in, float *outl, float *outr, int n);  /* reverb.c */

#define P_FX3SEND 0x1c8
#define APP       ((uint8_t *)0x24015170u)            /* [mk2] */
#define FX3_REC   ((void *)(APP + 0x2200u + 3u * 0x18u))
#define CELL_FX2  0x310
#define CELL_FX3  0x330
#define REC_COUNT 0xa
#define P_DECAY   0x1a
#define P_DAMP    0x1d
#define P_PREDLY  0x1e
#define FS_IN     48000.0f
#define FS_RVB    29800.0f
#define PRE_LEN   16384
#define MAXN      128
#define MAXM      (MAXN * 2 / 3 + 4)
#define RUN_ON    (30u * 48000u / 128u)                 /* blocks the tail runs on after the last input (~30 s) */

struct fx3 {
    uint32_t magic;
    float *plate;
    float *pre;
    uint32_t pre_w;
    float ph, lp1, lp2, px;
    float ol, or_, pl, pr;
    uint32_t idle;
    uint8_t *fx2_widget;         /* the FX grid's FX2 widget (cell id at +0x38) */
};
#define F3 ((volatile struct fx3 *)0x2406ce00u)        /* [mk2] */
_Static_assert(sizeof(struct fx3) <= 0x100, "FX3 state must fit its slot (tests/test_ram.py)");
#define F3MAGIC 0x46583321u

/* two bus records of our own (20 bytes as the engine's, 128-frame planes) in patch RAM: the send (pads mix into it)
   and the return (mixed into the destination bus with the stock mix, which handles a silent destination) */
#define F3IN    ((uint8_t *)0x2406f000u)              /* [mk2] */
#define F3OUT   ((uint8_t *)0x2406f018u)              /* [mk2] */
#define IN_L    ((float *)0x2406f040u)                /* [mk2] 4 planes x 0x200 */
#define IN_R    ((float *)0x2406f240u)                /* [mk2] */
#define OUT_L   ((float *)0x2406f440u)                /* [mk2] */
#define OUT_R   ((float *)0x2406f640u)                /* [mk2] */

static void bus_set(uint8_t *b, float *l, float *r, int frames)
{
    *(uint32_t *)(b + 0) = MAXN;
    *(uint32_t *)(b + 4) = (uint32_t)frames;
    *(float **)(b + 8) = l;
    *(float **)(b + 0xc) = r;
    b[0x10] = frames ? 0 : 1;                           /* silent */
    b[0x11] = 1;                                        /* stereo */
}

static int ready(void)
{
    return F3->magic == F3MAGIC && F3->plate;
}

void fx3_record_ensure(void)
{
    if (*(const uint16_t *)((const uint8_t *)FX3_REC + REC_COUNT) == 0) {
        static const uint8_t type = 0x44;
        fw_store_init(FX3_REC, &type);
    }
}

/* boot (UI thread): memory, the buses and the record */
void fx3_init(void)
{
    fx3_record_ensure();
    if (F3->magic == F3MAGIC)
        return;
    F3->plate = 0;
    bus_set(F3IN, IN_L, IN_R, 0);
    bus_set(F3OUT, OUT_L, OUT_R, 0);
    if (fw_heap_free() < 4u * 1024u * 1024u + 512u * 1024u)
        return;
    float *st = fw_alloc(0xb0, 0);
    float *pre = fw_alloc(PRE_LEN * 4, 0);
    if (!st || !pre)
        return;
    for (unsigned k = 0; k < 0xb0 / 4; k++)
        st[k] = 0.0f;
    fw_plate_init(st);
    for (unsigned k = 0; k < PRE_LEN; k++)
        pre[k] = 0.0f;
    F3->pre = pre;
    F3->pre_w = 0;
    F3->ph = F3->lp1 = F3->lp2 = F3->px = 0.0f;
    F3->ol = F3->or_ = F3->pl = F3->pr = 0.0f;
    F3->idle = RUN_ON;
    F3->plate = st;
    F3->magic = F3MAGIC;
}

/* --- the FX cell on the grid ------------------------------------------------------------------------------------ */
/* the boot-time labels of the FX grid widgets (bl 0x0805d8cc at 0x08072130): note FX2's widget; FX3 set up */
void ss_init(void);                                     /* ss.c */

void fx3_label(uint8_t *widget, const char *text)
{
    fw_set_text(widget, text);
    if (text[0] == 'F' && text[1] == 'X' && text[2] == ' ' && text[3] == '2' && !text[4])
        F3->fx2_widget = widget;
    fx3_init();
    ss_init();
}

/* the dispatcher's set-cell (command 0x111): a second touch on the selected FX2 / FX3 cell switches between them */
void fx3_set_cell(uint8_t *app, const uint16_t *msg)
{
    unsigned cell = msg[2], cur = *(volatile uint16_t *)(app + 0x28);
    uint8_t *w = F3->fx2_widget;
    if (w && (cell == CELL_FX2 || cell == CELL_FX3) && cell == cur) {
        uint16_t m[14];
        for (int k = 0; k < 14; k++)
            m[k] = msg[k];
        unsigned to = cell == CELL_FX2 ? CELL_FX3 : CELL_FX2;
        m[2] = (uint16_t)to;
        if (to == CELL_FX3)
            fx3_record_ensure();
        *(volatile uint16_t *)(w + 0x38) = (uint16_t)to;
        fw_set_text(w, to == CELL_FX3 ? "FX 3" : "FX 2");
        fw_set_cell(app, m);
        return;
    }
    fw_set_cell(app, msg);
}

/* --- pad sends ------------------------------------------------------------------------------------------------------ */
static float send_gain(const uint8_t *pad)
{
    int v = (int)pad_setting(pad, P_FX3SEND);
    if (v <= 0)
        return 0.0f;
    return fw_send_curve((float)(v > 1000 ? 1000 : v) * 0.001f);
}

/* thunks fx3_check_a / _b: 1 if FX3 Send is on */
int fx3_send_on(const uint8_t *pad)
{
    return ready() && pad_setting(pad, P_FX3SEND) > 0;
}

/* thunks fx3_mix_a / _b: the pad's final mix (scratch -> its output): first scratch x FX3 gain -> FX3's send bus */
void fx3_padmix(void *out, const void *scratch, const uint8_t *pad, void *engine)
{
    (void)engine;
    float g = send_gain(pad);
    if (g > 0.0f && ready())
        fw_mix_gain(F3IN, scratch, g);
    fw_mix(out, scratch);
}

/* --- the effect: from fx_return (fx.c) after the reverb's return, into dst ------------------------------------------- */
void fx3_process(void *dst)
{
    volatile struct fx3 *f = F3;
    if (f->magic != F3MAGIC || !f->plate)
        return;
    uint8_t *in = F3IN;
    int silent = in[0x10];
    int n = silent ? MAXN : fw_frames(in);
    if (n <= 0 || n > MAXN) {
        bus_set(F3IN, IN_L, IN_R, 0);
        return;
    }
    if (silent) {
        if (f->idle >= RUN_ON)
            return;                                     /* no input and the tail has run out */
        f->idle++;
    } else
        f->idle = 0;
    const float *il = IN_L, *ir = in[0x11] ? IN_R : IN_L;

    const void *rec = FX3_REC;
    float *st = f->plate;
    st[0] = (float)fw_store_get(rec, P_DECAY) * (0.001f * 0.93f);
    st[1] = (float)fw_store_get(rec, P_DAMP) * 0.001f;
    int pre_d = (int)((float)fw_store_get(rec, P_PREDLY) * 0.001f * 12000.0f) + 256;
    if (pre_d > PRE_LEN - 2)
        pre_d = PRE_LEN - 2;

    /* decimate 48 -> 29.8 kHz: two one-pole low-passes, linear interpolation at the 29.8 kHz instants */
    const float r = FS_RVB / FS_IN, a = 0.6f;
    float tin[MAXM], tl[MAXM], tr[MAXM];
    float ph0 = f->ph, ph = ph0, l1 = f->lp1, l2 = f->lp2, px = f->px;
    float *pre = f->pre;
    uint32_t w = f->pre_w;
    int m = 0;
    for (int k = 0; k < n; k++) {
        float x = silent ? 0.0f : 0.5f * (il[k] + ir[k]);
        l1 += a * (x - l1);
        l2 += a * (l1 - l2);
        float cur = l2;
        ph += r;
        if (ph >= 1.0f && m < MAXM) {
            ph -= 1.0f;
            float back = ph / r;
            float v = cur + (px - cur) * back;
            pre[w & (PRE_LEN - 1)] = v;
            tin[m++] = pre[(w - (uint32_t)pre_d) & (PRE_LEN - 1)];
            w++;
        }
        px = cur;
    }
    f->lp1 = l1;
    f->lp2 = l2;
    f->px = px;
    f->pre_w = w;
    if (m > 0)
        reverb_run_record(st, rec, tin, tl, tr, m);

    /* back to 48 kHz: the same instants, linear between the previous and the current reverb output */
    float cl = f->ol, cr = f->or_, pl = f->pl, pr = f->pr;
    int j = 0;
    ph = ph0;
    for (int k = 0; k < n; k++) {
        ph += r;
        if (ph >= 1.0f && j < m) {
            ph -= 1.0f;
            pl = cl;
            pr = cr;
            cl = tl[j];
            cr = tr[j];
            j++;
        }
        OUT_L[k] = pl + (cl - pl) * ph;
        OUT_R[k] = pr + (cr - pr) * ph;
    }
    f->ph = ph;
    f->ol = cl;
    f->or_ = cr;
    f->pl = pl;
    f->pr = pr;
    bus_set(F3OUT, OUT_L, OUT_R, n);
    fw_mix(dst, F3OUT);
    bus_set(F3IN, IN_L, IN_R, 0);                       /* consumed: silent until the pads mix into it again */
}
