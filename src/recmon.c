/*
 * Recording and the FX sends (portkit).
 *
 * 1. Sends survive a recording. 1-2 s into a recording the firmware rebuilds the pad's record as a full clip pad
 *    (0x0807b400, call 0x0807b59e -> store init 0x08089fc4, type 3): every setting goes back to its default, the
 *    FX1 / FX2 / FX3 sends too. rec_reinit (hooked at that call) keeps the three sends across it; the pad effects
 *    (filter, pitch, drive, ...) are reset as before.
 * 2. The input monitor through the sends. The input is monitored by one global node (vtable 0x080ddfb4 -> process
 *    0x080a4d1c(obj, engine); id 0x400, not a pad): it mixes the input into the bus in obj +0x68 (Mon Output, bus 0 =
 *    Out 1/2, so the master section already applies). mon_node wraps it: the monitor bus before, the node, the
 *    difference = what the node added = the monitor signal; that goes to FX1 (bus 0xd), FX2 (0xe) and FX3 (0xc) with
 *    the sends of the selected pad (app +0x14), as a pad's scratch does (0x080a33ec with the stock send curve). So an
 *    empty pad with sends lets you hear its effects while recording into it. Nothing to do while all three are 0.
 */
#include <stdint.h>

#pragma GCC optimize("O2", "no-tree-loop-distribute-patterns")

#define FN(addr) ((addr) | 1u)

typedef void (*store_init_fn)(void *store, const uint8_t *type);
typedef void (*store_set_fn)(void *store, unsigned param, int value);
typedef int (*node_fn)(void *obj, void *engine);
typedef void *(*bus_fn)(void *engine, unsigned id);
typedef int (*planes_fn)(void *buf, float **l, float **r);
typedef int (*frames_fn)(void *buf);
typedef void (*mix_gain_fn)(void *dst, const void *src, float gain);
typedef float (*curve_fn)(float x);
#define fw_store_init ((store_init_fn)FN(0x08089fc4))
#define fw_store_set  ((store_set_fn)FN(0x080894fa))
#define fw_input_node ((node_fn)FN(0x080a4d1c))
#define fw_bus        ((bus_fn)FN(0x080a82b8))
#define fw_planes     ((planes_fn)FN(0x080a3288))     /* read access: zero-fills a silent buffer */
#define fw_frames     ((frames_fn)FN(0x080a31e0))
#define fw_mix_gain   ((mix_gain_fn)FN(0x080a33ec))
#define fw_send_curve ((curve_fn)FN(0x080a04c0))      /* send value / 1000 -> gain (the stock send setters) */

int fast_get(const void *store, unsigned param);        /* fastget.c */

#define APP        ((uint8_t *)0x24013cb0u)
#define APP_PADS   (APP + 0x1840u)
#define APP_SEL    0x14u                                 /* selected pad (code: row << 4 | col, layer bits 8-12) */
#define P_FX1SEND  0xbb
#define P_FX2SEND  0xbc
#define P_FX3SEND  0x1c8
#define BUS_FX3    0xc
#define BUS_FX1    0xd
#define BUS_FX2    0xe
#define MON_BUS    0x68                                  /* input node: Mon Output bus */
#define MAXN       512

/* --- 1. sends across the rebuild ----------------------------------------------------------------------------------- */
void rec_reinit(void *store, const uint8_t *type)
{
    int s1 = fast_get(store, P_FX1SEND), s2 = fast_get(store, P_FX2SEND), s3 = fast_get(store, P_FX3SEND);
    fw_store_init(store, type);
    if (s1)
        fw_store_set(store, P_FX1SEND, s1);
    if (s2)
        fw_store_set(store, P_FX2SEND, s2);
    if (s3)
        fw_store_set(store, P_FX3SEND, s3);
}

/* --- 2. the monitor through the sends ------------------------------------------------------------------------------ */
/* the monitor signal: 2 x 512 floats, shared with master.c's Mix copy (used later in the block, never at the same
   time), and a buffer struct around it for 0x080a33ec (+0 capacity, +4 frames, +8 L, +0xc R, +0x10 silent, +0x11
   stereo) */
#define MON_L  ((float *)0x2001d000u)
#define MON_R  ((float *)0x2001d800u)
struct buf { uint32_t cap, frames; float *l, *r; uint8_t silent, stereo, pad[2]; };
#define MONBUF ((volatile struct buf *)0x2406ab00u)

static float send_gain(int v)
{
    if (v <= 0)
        return 0.0f;
    return fw_send_curve((float)(v > 1000 ? 1000 : v) * 0.001f);
}

static const uint8_t *selected_pad(void)
{
    unsigned code = *(const uint16_t *)(APP + APP_SEL);
    unsigned row = (code >> 4) & 0xfu, col = code & 0xfu;
    if ((code & 0x1f00u) || row >= 4 || col >= 4)
        return 0;
    return APP_PADS + row * 0xf0u + col * 0x30u;
}

int mon_node(uint8_t *obj, void *engine)
{
    const uint8_t *pad = selected_pad();
    float g1 = 0.0f, g2 = 0.0f, g3 = 0.0f;
    if (pad) {
        g1 = send_gain(fast_get(pad, P_FX1SEND));
        g2 = send_gain(fast_get(pad, P_FX2SEND));
        g3 = send_gain(fast_get(pad, P_FX3SEND));
    }
    if (g1 == 0.0f && g2 == 0.0f && g3 == 0.0f)
        return fw_input_node(obj, engine);

    void *bus = fw_bus(engine, *(const uint32_t *)(obj + MON_BUS));
    int n = fw_frames(bus);
    int was_silent = *((const uint8_t *)bus + 0x10);
    float *l, *r;
    if (n > 0 && n <= MAXN && !was_silent) {
        fw_planes(bus, &l, &r);
        for (int k = 0; k < n; k++) {
            MON_L[k] = l[k];
            MON_R[k] = r && r != l ? r[k] : l[k];
        }
    } else {
        for (int k = 0; k < MAXN; k++)
            MON_L[k] = MON_R[k] = 0.0f;
    }
    int ret = fw_input_node(obj, engine);

    n = fw_frames(bus);
    if (n <= 0 || n > MAXN || *((const uint8_t *)bus + 0x10))
        return ret;                                      /* nothing monitored */
    fw_planes(bus, &l, &r);
    float peak = 0.0f;
    for (int k = 0; k < n; k++) {
        float dl = l[k] - MON_L[k], dr = (r && r != l ? r[k] : l[k]) - MON_R[k];
        MON_L[k] = dl;
        MON_R[k] = dr;
        float a = (dl < 0.0f ? -dl : dl) + (dr < 0.0f ? -dr : dr);
        peak = a > peak ? a : peak;
    }
    if (peak == 0.0f)
        return ret;
    volatile struct buf *b = MONBUF;
    b->cap = MAXN;
    b->frames = (uint32_t)n;
    b->l = MON_L;
    b->r = MON_R;
    b->silent = 0;
    b->stereo = 1;
    if (g1 > 0.0f)
        fw_mix_gain(fw_bus(engine, BUS_FX1), (const void *)b, g1);
    if (g2 > 0.0f)
        fw_mix_gain(fw_bus(engine, BUS_FX2), (const void *)b, g2);
    if (g3 > 0.0f)
        fw_mix_gain(fw_bus(engine, BUS_FX3), (const void *)b, g3);
    return ret;
}
