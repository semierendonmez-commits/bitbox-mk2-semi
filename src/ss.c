/*
 * Screen saver with a live monitor (bitbox-mk2-semi; idea from vwal's patcher, own code). bitbox MK2 2.3.4.
 *
 * - Settings (Tools -> More, page "Saver"): Saver (Off, 1, 2, 5, 10, 30 min without input), View (Scope: a sweeping
 *   oscilloscope of Out1/2 with level meters; Meters: the meters only; Dark: black).
 * - Display: the LTDC shows two ARGB1555 layers (320 x 240, layer 1 the backgrounds, layer 2 the text; HAL LTDC,
 *   the buffers are set up once). While the saver runs, layer 1 shows a buffer of ours (heap, 150 KB, allocated at
 *   boot) and layer 2 is switched off; on wake both are put back (registers only: the UI's own buffers are never
 *   touched, nothing has to be redrawn). Register reloads at vertical blanking (SRCR = 2).
 * - Drawing runs in the audio block (ss_audio, from master_dist: bus 0 = Out1/2 after the FX returns), one scope
 *   column per block (375 columns/s: a sweep every 0.8 s), the meters every 8 blocks; a few hundred pixel writes.
 * - Wake: a touch (the app dispatcher's touch commands: 3 / 4 on the grid, 0x14d on dial pages, knob, cell select; the
 *   waking touch is swallowed), the two keys (GPIOC
 *   IDR, PC2 / PC3 active low, polled per block), MIDI in (the UARTs' receive ISR, HAL RxISR pointer 0x0804e9c9 at
 *   its two set-up sites, replaced by ss_rx_isr). A MIDI byte also flashes a dot in the corner.
 */
#include <stdint.h>

#pragma GCC optimize("O2", "no-tree-loop-distribute-patterns")

#define FN(addr) ((addr) | 1u)
typedef void *(*alloc_fn)(unsigned size, unsigned n);
typedef unsigned (*free_fn)(void);
typedef int (*planes_fn)(void *buf, float **l, float **r);
typedef int (*frames_fn)(void *buf);
typedef void (*isr_fn)(void *huart);
typedef int (*dispatch_fn)(void *app, const uint16_t *msg);
#define fw_alloc      ((alloc_fn)FN(0x08043b4c))      /* [mk2] */
#define fw_heap_free  ((free_fn)FN(0x08043bd0))       /* [mk2] */
#define fw_planes     ((planes_fn)FN(0x080a4630))     /* [mk2] */
#define fw_frames     ((frames_fn)FN(0x080a4588))     /* [mk2] */
#define fw_rx_isr     ((isr_fn)FN(0x0804e9c8))        /* [mk2] HAL UART_RxISR_8BIT */
#define fw_dispatch   ((dispatch_fn)FN(0x0808073c))   /* [mk2] app message dispatcher */
int fast_get(const void *store, unsigned param);
#define fw_store_get fast_get

#define APP       ((uint8_t *)0x24015170u)            /* [mk2] */
#define SETTINGS  (APP + 0x7ed0u)
#define P_SAVER   0x1b8
#define P_SSVIEW  0x1b9

#define LTDC      0x50001000u
#define L1_CR     (*(volatile uint32_t *)(LTDC + 0x84u))
#define L1_CFBAR  (*(volatile uint32_t *)(LTDC + 0xacu))
#define L2_CR     (*(volatile uint32_t *)(LTDC + 0x104u))
#define SRCR      (*(volatile uint32_t *)(LTDC + 0x24u))
#define GPIOC_IDR (*(volatile uint32_t *)0x58020810u)

#define W 320
#define H 240
#define SCOPE_W 296                                     /* scope columns; the meters to the right */
#define BLOCKS_PER_MIN (375u * 60u)

static const uint16_t saver_min[] = {0, 1, 2, 5, 10, 30};
#define SAVERS 6
enum { V_SCOPE, V_METERS, V_DARK, VIEWS };

struct ss {
    uint32_t magic;
    uint16_t *fb;              /* our screen (ARGB1555, opaque) */
    uint32_t idle;             /* audio blocks since the last input */
    uint32_t saved_cfbar, saved_l2;
    uint8_t on, swallow, keys, view;
    uint32_t col, blk;
    float peak_l, peak_r;
    volatile uint32_t rx;      /* MIDI bytes (ISR) */
    uint32_t rx_seen;
    uint32_t flash;
};
#define SS ((volatile struct ss *)0x2001c0c0u)         /* [mk2] patch RAM, 0x40 (tests/test_ram.py) */
_Static_assert(sizeof(struct ss) <= 0x40, "saver state fits its slot");
#define SSMAGIC 0x53415652u

#define BLACK  0x8000u
#define DIM    0x8000u | (2u << 10) | (3u << 5) | 3u
#define GRID   0x8000u | (4u << 10) | (5u << 5) | 6u
#define TRACE  0x8000u | (6u << 10) | (28u << 5) | 31u      /* cyan */
#define TRACE2 0x8000u | (31u << 10) | (12u << 5) | 28u     /* pink */
#define MET    0x8000u | (8u << 10) | (30u << 5) | 10u
#define HOT    0x8000u | (31u << 10) | (8u << 5) | 4u
#define DOT    0x8000u | (31u << 10) | (31u << 5) | 31u

/* boot (UI thread, from fx3_label): the screen buffer */
void ss_init(void)
{
    volatile struct ss *s = SS;
    if (s->magic == SSMAGIC)
        return;
    s->fb = 0;
    s->idle = 0;
    s->on = 0;
    s->swallow = 0;
    s->col = 0;
    s->blk = 0;
    s->rx = s->rx_seen = 0;
    s->flash = 0;
    if (fw_heap_free() >= 2u * 1024u * 1024u)
        s->fb = fw_alloc(W * H * 2, 0);
    s->magic = SSMAGIC;
}

static void fill(uint16_t *fb, uint16_t c)
{
    uint32_t v = (uint32_t)c << 16 | c, *p = (uint32_t *)fb;
    for (int k = 0; k < W * H / 2; k++)
        p[k] = v;
}

static void rect(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t c)
{
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            fb[y * W + x] = c;
}

static void wake(void)
{
    volatile struct ss *s = SS;
    s->idle = 0;
    if (!s->on)
        return;
    L1_CFBAR = s->saved_cfbar;
    L2_CR = s->saved_l2;
    SRCR = 2;
    s->on = 0;
}

static void start(void)
{
    volatile struct ss *s = SS;
    uint16_t *fb = s->fb;
    if (!fb || s->on)
        return;
    s->view = (uint8_t)fw_store_get(SETTINGS, P_SSVIEW);
    fill(fb, BLACK);
    if (s->view == V_SCOPE) {
        rect(fb, 0, H / 2, SCOPE_W, H / 2 + 1, GRID);
        rect(fb, 0, H / 4, SCOPE_W, H / 4 + 1, DIM);
        rect(fb, 0, 3 * H / 4, SCOPE_W, 3 * H / 4 + 1, DIM);
    }
    s->saved_cfbar = L1_CFBAR;
    s->saved_l2 = L2_CR;
    L1_CFBAR = (uint32_t)fb;
    L2_CR = s->saved_l2 & ~1u;
    SRCR = 2;
    s->col = 0;
    s->on = 1;
}

static int clampy(float v)
{
    int y = (int)(H / 2 - v * (H / 2 - 4));
    return y < 2 ? 2 : y > H - 3 ? H - 3 : y;
}

static void column(uint16_t *fb, int x, float lo, float hi, uint16_t c)
{
    int a = clampy(hi), b = clampy(lo);
    for (int y = a; y <= b; y++)
        fb[y * W + x] = c;
}

static void meters(uint16_t *fb, float l, float r)
{
    for (int m = 0; m < 2; m++) {
        float v = m ? r : l;
        int x0 = SCOPE_W + 4 + m * 10, h = (int)(v * (H - 8));
        h = h < 0 ? 0 : h > H - 8 ? H - 8 : h;
        for (int y = 4; y < H - 4; y++) {
            int lit = (H - 4 - y) <= h;
            uint16_t c = !lit ? BLACK : y < H / 6 ? HOT : MET;
            for (int x = x0; x < x0 + 8; x++)
                fb[y * W + x] = c;
        }
    }
}

/* every audio block (master_dist, bus 0 = Out1/2) */
void ss_audio(void *bus0)
{
    volatile struct ss *s = SS;
    if (s->magic != SSMAGIC || !s->fb)
        return;
    int t = fw_store_get(SETTINGS, P_SAVER);
    uint32_t keys = GPIOC_IDR & 0xcu;
    if (keys != 0xcu || s->rx != s->rx_seen || t <= 0 || t >= SAVERS) {
        s->rx_seen = s->rx;
        if (s->on && keys != 0xcu)
            s->swallow = 0;
        wake();
        if (t <= 0 || t >= SAVERS)
            return;
    }
    if (!s->on) {
        if (++s->idle >= saver_min[t] * BLOCKS_PER_MIN)
            start();
        return;
    }
    /* the monitor */
    float lo = 0.0f, hi = 0.0f, lo2 = 0.0f, hi2 = 0.0f, pl = 0.0f, pr = 0.0f;
    if (!*((const uint8_t *)bus0 + 0x10)) {
        int n = fw_frames(bus0);
        float *l = 0, *r = 0;
        fw_planes(bus0, &l, &r);
        if (l && n > 0 && n <= 512) {
            if (!r)
                r = l;
            lo = hi = l[0];
            lo2 = hi2 = r[0];
            for (int k = 0; k < n; k++) {
                float a = l[k], b = r[k];
                lo = a < lo ? a : lo;
                hi = a > hi ? a : hi;
                lo2 = b < lo2 ? b : lo2;
                hi2 = b > hi2 ? b : hi2;
                a = a < 0.0f ? -a : a;
                b = b < 0.0f ? -b : b;
                pl = a > pl ? a : pl;
                pr = b > pr ? b : pr;
            }
        }
    }
    uint16_t *fb = s->fb;
    s->peak_l = pl > s->peak_l ? pl : s->peak_l * 0.97f;
    s->peak_r = pr > s->peak_r ? pr : s->peak_r * 0.97f;
    if (s->view == V_SCOPE) {
        int x = (int)s->col;
        for (int y = 2; y < H - 2; y++)                 /* erase this column (grid lines kept) */
            fb[y * W + x] = y == H / 2 ? GRID : (y == H / 4 || y == 3 * H / 4) ? DIM : BLACK;
        column(fb, x, lo2, hi2, TRACE2);
        column(fb, x, lo, hi, TRACE);
        int nx = (x + 1) % SCOPE_W;                     /* a gap ahead of the sweep */
        for (int y = 2; y < H - 2; y++)
            fb[y * W + nx] = BLACK;
        s->col = (uint32_t)nx;
    }
    if (s->view != V_DARK && (++s->blk & 7u) == 0)
        meters(fb, s->peak_l, s->peak_r);
    if (s->rx != s->rx_seen) {
        s->rx_seen = s->rx;
        s->flash = 40;
    }
    if (s->flash) {                                     /* MIDI dot, top left (only while the saver runs) */
        s->flash--;
        rect(fb, 4, 4, 10, 10, s->flash ? DOT : BLACK);
    }
}

/* UART receive ISR (both MIDI ports): the stock handler, then count the byte unless it is a real-time byte */
void ss_rx_isr(void *huart)
{
    fw_rx_isr(huart);
    const uint8_t *p = *(const uint8_t *const *)((const uint8_t *)huart + 0x58);   /* pRxBuffPtr, past the byte */
    if (p && p[-1] < 0xf8)                              /* not MIDI clock / real time: a clock alone keeps it asleep */
        SS->rx++;
}

/* HAL_LTDC_SetAddress(hltdc, address, layer 0 / 1) (0x0804c03a, its two callers: the UI flips layer 2's buffers
   every frame and the HAL rewrites the whole layer, enable bit too): while the saver runs, our screen stays */
typedef int (*setaddr_fn)(void *h, uint32_t addr, unsigned layer);
#define fw_set_address ((setaddr_fn)FN(0x0804c03a))    /* [mk2] */

int ss_set_address(void *h, uint32_t addr, unsigned layer)
{
    int r = fw_set_address(h, addr, layer);
    volatile struct ss *s = SS;
    if (s->magic == SSMAGIC && s->on) {
        if (layer == 0) {
            s->saved_cfbar = addr;
            L1_CFBAR = (uint32_t)s->fb;
        }
        L2_CR &= ~1u;
        SRCR = 1;
    }
    return r;
}

/* the dispatcher calls (bl 0x0808073c at its three callers): a touch wakes the saver and is swallowed */
/* commands a touch produces: 3 / 4 (press / release on the pad grid), 0x14d (a dial page's touch), 0x6e (knob),
   0x111 (cell select), 0x16a (step edit). Other messages (the sequencer, timers) neither wake nor reset the timer. */
static int touch_cmd(unsigned cmd)
{
    return cmd == 3 || cmd == 4 || cmd == 0x14d || cmd == 0x6e || cmd == 0x111 || cmd == 0x16a;
}

int ss_dispatch(void *app, const uint16_t *msg)
{
    volatile struct ss *s = SS;
    unsigned cmd = msg[0];
    if (s->magic == SSMAGIC && touch_cmd(cmd)) {
        if (s->on) {                                    /* the waking touch does nothing else */
            wake();
            s->swallow = cmd != 4;
            return 0;
        }
        if (s->swallow && cmd == 4) {
            s->swallow = 0;
            return 0;
        }
        s->swallow = 0;
        s->idle = 0;
    }
    return fw_dispatch(app, msg);
}
