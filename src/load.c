/* ported to the bitbox MK2 2.3.4 by tools/port_mk2.py */
/*
 * Audio load in the status bar (portkit). The audio task (0x08040e94 -> 0x080422c8, FreeRTOS "audioTask") waits for
 * each DMA half and then runs the input stage 0x080952d8 and the engine + output 0x08095364 (call sites 0x08042354 /
 * 0x08042360 and 0x08042370 / 0x0804237c). Load = cycles spent in both / cycles between two block starts (DWT cycle
 * counter), smoothed, with a peak that decays. Shown in the status bar's recording text (+0x8c: "%dMB" / "FULL" while
 * recording memory is in use, empty otherwise, 0x080714f0 with the value at app +0xdcc8 = -1); its empty case shows the
 * load instead. That text sits at x 80, w 48 under the title, so it moves to the right end of the bar (x 280, w 39;
 * the stock text at x 80, w 200 ends at 280).
 */
#include <stdint.h>

#define FN(addr) ((addr) | 1u)

typedef void (*in_fn)(void *engine, void *buf, int frames);
typedef void (*out_fn)(void *engine, void *out, void *in, int frames);
typedef int (*text_fn)(void *widget, const char *text);
typedef int (*snprintf_fn)(char *buf, unsigned size, const char *fmt, ...);
#define fw_audio_in  ((in_fn)FN(0x080952d8))
#define fw_audio_out ((out_fn)FN(0x08095364))
#define fw_set_text  ((text_fn)FN(0x0805ff6e))
#define fw_snprintf  ((snprintf_fn)FN(0x080bde58))

#define DEMCR      (*(volatile uint32_t *)0xe000edfcu)
#define DWT_CTRL   (*(volatile uint32_t *)0xe0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xe0001004u)
#define DWT_LAR    (*(volatile uint32_t *)0xe0001fb0u)

struct load {
    uint32_t magic;
    uint32_t start;             /* cycle count at this block's start */
    uint32_t period;            /* cycles between the last two block starts */
    uint32_t busy;              /* cycles spent in this block so far */
    float avg, peak;            /* load 0..1 */
};
#define LMAGIC 0x4c4f4144u      /* "LOAD" */
#define LOAD ((volatile struct load *)0x2406bdc0u)        /* patch RAM, device memory, aligned */
_Static_assert(sizeof(struct load) <= 0x40, "load meter state: 0x40 bytes");

void load_in(void *engine, void *buf, int frames)
{
    volatile struct load *l = LOAD;
    if (l->magic != LMAGIC) {
        DEMCR |= 1u << 24;                              /* TRCENA */
        DWT_LAR = 0xc5acce55u;
        DWT_CTRL |= 1u;                                 /* CYCCNTENA */
        l->magic = LMAGIC;
        l->start = DWT_CYCCNT;
        l->period = 0;
        l->busy = 0;
        l->avg = l->peak = 0.0f;
    }
    uint32_t now = DWT_CYCCNT;
    uint32_t period = now - l->start;
    if (period && l->period) {                          /* close the last block */
        float x = (float)l->busy / (float)period;
        l->avg += 0.02f * (x - l->avg);
        float pk = l->peak * 0.995f;
        l->peak = x > pk ? x : pk;
    }
    l->period = period;
    l->start = now;
    fw_audio_in(engine, buf, frames);
    l->busy = DWT_CYCCNT - now;
}

void load_out(void *engine, void *out, void *in, int frames)
{
    uint32_t t0 = DWT_CYCCNT;
    fw_audio_out(engine, out, in, frames);
    LOAD->busy += DWT_CYCCNT - t0;
}

/* status bar, recording text, stock empty case: the load instead ("34%"; peak above 90 % marked "!") */
#define LOAD_X 280
#define LOAD_W 39
int load_text(void *widget, const char *text)
{
    (void)text;
    int32_t *rect = (int32_t *)((uint8_t *)widget + 4);  /* x, y, w, h */
    if (rect[0] != LOAD_X) {
        rect[0] = LOAD_X;
        rect[2] = LOAD_W;
    }
    volatile struct load *l = LOAD;
    char buf[12];
    if (l->magic != LMAGIC)
        return fw_set_text(widget, "");
    int a = (int)(l->avg * 100.0f + 0.5f);
    a = a < 0 ? 0 : a > 999 ? 999 : a;
    fw_snprintf(buf, sizeof buf, l->peak > 0.9f ? "%d%%!" : "%d%%", a);
    return fw_set_text(widget, buf);
}

/*
 * bitbox MK2: the recording text shows the free recording memory ("%dMB" -> "245M" in its 48 px) whenever a card is
 * in, so the idle case above is rare there. Its "%dMB" and "FULL" calls (0x080714c6, 0x080714e4) go here: the text
 * keeps its place (x 80) and gets the load after it, in a wider rect (the stock idle text is x 80, w 200).
 */
#define MB_W 120
int load_text_mb(void *widget, const char *text)
{
    int32_t *rect = (int32_t *)((uint8_t *)widget + 4);  /* x, y, w, h */
    if (rect[2] < MB_W)
        rect[2] = MB_W;
    volatile struct load *l = LOAD;
    char buf[32];
    if (l->magic != LMAGIC)
        return fw_set_text(widget, text);
    int a = (int)(l->avg * 100.0f + 0.5f);
    a = a < 0 ? 0 : a > 999 ? 999 : a;
    char mb[8];
    unsigned k = 0;
    while (text[k] && k < sizeof mb - 1) {               /* "245MB" -> "245M", as the stock 48 px showed it */
        mb[k] = text[k];
        k++;
    }
    mb[k] = 0;
    if (k >= 2 && mb[k - 1] == 'B' && mb[k - 2] == 'M')
        mb[k - 1] = 0;
    fw_snprintf(buf, sizeof buf, l->peak > 0.9f ? "%s  %d%%!" : "%s  %d%%", mb, a);
    return fw_set_text(widget, buf);
}
