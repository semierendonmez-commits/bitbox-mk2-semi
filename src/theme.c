/*
 * Colour themes for the MK2's screen (bitbox-mk2-semi, own code; the theme names from the OpenTracker fork's pack).
 *
 * The UI draws with 8-bit palette indices (its DMA2D fills pass e.g. 0x01010101 = colour 1) and the Chrom-ART DMA2D
 * converts them through its CLUTs into the ARGB1555 layers. The palette is 33 ARGB8888 entries at RAM 0x24000000
 * (initialised from .data 0x080e4b58), loaded once at start into the DMA2D's foreground and background CLUTs
 * (0x0804336c: HAL CLUT config 0x0804a9c4 + start 0x0804a614). A theme replaces the palette's UI colours by role
 * (pad grey, background, selection pink, accent cyan, and the greys / dark cyan mixed from them; white and the
 * meter colours stay) and reloads both CLUTs, from the UI's own DMA2D use (HAL_DMA2D_Start -> DMA2D_SetConfig,
 * hooked at 0x0804a5fc): the DMA2D is idle there and nothing else uses it. The screen takes the colours as it redraws.
 * Setting: Tools -> More -> Saver: Theme (Stock, Iznik, Acetate, Pirate FM, Oscillo, Riso, Bauhaus, Aurora,
 * Concrete, Soundsys).
 */
#include <stdint.h>

#pragma GCC optimize("O2", "no-tree-loop-distribute-patterns")

#define FN(addr) ((addr) | 1u)
typedef void (*setcfg_fn)(void *h, uint32_t color, uint32_t dst, uint32_t w, uint32_t hgt);
#define fw_dma2d_setconfig ((setcfg_fn)FN(0x0804a4c0))   /* [mk2] DMA2D_SetConfig */
int fast_get(const void *store, unsigned param);
#define SETTINGS ((uint8_t *)0x24015170u + 0x7ed0u)
#define P_THEME  0x20a

/* the stock colours the fills use, by role */
enum { R_PAD, R_BG, R_SEL, R_ACC, R_MID, R_DIM, R_LIGHT, R_DARK, R_ACC2, ROLES };
static const uint32_t stock[ROLES] = {0x56565a, 0x141414, 0xff6af3, 0x09d7f5, 0x666666, 0x444444, 0x707070,
                                      0x2b2b2b, 0x046b7a};

/* per theme: pad, background, selection, accent (the rest mixed from them) */
static const uint32_t themes[][4] = {
    {0x2b5f8a, 0x0b1626, 0xc8102e, 0x2ac1b5},     /* Iznik: cobalt tiles, tile red, turquoise */
    {0x3a3632, 0x0e0d0c, 0xd8c9a3, 0xb08d57},     /* Acetate: lacquer black, aluminium, brass */
    {0x3d2b56, 0x0b0910, 0xff2d55, 0xffd60a},     /* Pirate FM */
    {0x0f3d1a, 0x020a03, 0x39ff14, 0x00ff9c},     /* Oscillo: phosphor green */
    {0x2f5d9f, 0x161616, 0xff48b0, 0xffe800},     /* Riso: riso blue, fluorescent pink, yellow */
    {0x1f3a93, 0x111111, 0xe63323, 0xf2c12e},     /* Bauhaus */
    {0x16324f, 0x050b1a, 0x7cff6b, 0xb36bff},     /* Aurora */
    {0x5b5b58, 0x1c1c1b, 0xd0d0c8, 0xff6a00},     /* Concrete: grey, safety orange */
    {0x1d5c2b, 0x0a0a0a, 0xffd200, 0xd7261e},     /* Soundsys: green, gold, red */
};
#define NTHEMES (int)(sizeof themes / sizeof themes[0])
const char *const theme_names[NTHEMES + 1] = {"Stock", "Iznik", "Acetate", "Pirate FM", "Oscillo", "Riso",
                                              "Bauhaus", "Aurora", "Concrete", "Soundsys"};

static uint32_t mix(uint32_t a, uint32_t b, int t)          /* a .. b, t / 16 */
{
    uint32_t r = 0;
    for (int s = 0; s < 24; s += 8) {
        int x = (int)(a >> s & 0xff), y = (int)(b >> s & 0xff);
        r |= (uint32_t)(x + (y - x) * t / 16) << s;
    }
    return r;
}

uint32_t theme_color(uint32_t argb, int t)
{
    if (t <= 0 || t > NTHEMES)
        return argb;
    uint32_t rgb = argb & 0xffffffu;
    int role = -1;
    for (int k = 0; k < ROLES; k++)
        if (stock[k] == rgb) {
            role = k;
            break;
        }
    if (role < 0)
        return argb;
    const uint32_t *th = themes[t - 1];
    uint32_t c;
    switch (role) {
    case R_PAD: c = th[0]; break;
    case R_BG: c = th[1]; break;
    case R_SEL: c = th[2]; break;
    case R_ACC: c = th[3]; break;
    case R_MID: c = mix(th[0], 0xffffff, 3); break;
    case R_DIM: c = mix(th[1], th[0], 10); break;
    case R_LIGHT: c = mix(th[0], 0xffffff, 5); break;
    case R_DARK: c = mix(th[1], th[0], 5); break;
    default: c = mix(th[1], th[3], 7); break;
    }
    return (argb & 0xff000000u) | c;
}

#define PALETTE   ((volatile uint32_t *)0x24000000u)   /* [mk2] the UI palette (RAM) */
#define STOCK_PAL ((const uint32_t *)0x080e4b58u)      /* [mk2] its .data image in flash */
#define NPAL      33
#define DMA2D     0x52001000u
#define FGPFCCR   (*(volatile uint32_t *)(DMA2D + 0x1cu))
#define BGPFCCR   (*(volatile uint32_t *)(DMA2D + 0x24u))
#define FGCMAR    (*(volatile uint32_t *)(DMA2D + 0x2cu))
#define BGCMAR    (*(volatile uint32_t *)(DMA2D + 0x30u))
#define APPLIED   (*(volatile uint32_t *)0x2001f3f8u)  /* [mk2] 0x5448xxxx | theme applied (tests/test_ram.py) */

static void clut_load(volatile uint32_t *pfccr, volatile uint32_t *cmar)
{
    *cmar = (uint32_t)PALETTE;
    uint32_t v = *pfccr & ~((0xffu << 8) | (1u << 4));  /* CS = 32 (33 entries), CCM = ARGB8888 */
    *pfccr = v | (32u << 8) | (1u << 5);                /* START: load the CLUT */
    for (int guard = 0; guard < 100000 && (*pfccr & (1u << 5)); guard++)
        ;
}

static void theme_apply(int t)
{
    for (int k = 0; k < NPAL; k++)
        PALETTE[k] = theme_color(STOCK_PAL[k], t);
    clut_load(&FGPFCCR, &FGCMAR);
    clut_load(&BGPFCCR, &BGCMAR);
    APPLIED = 0x54480000u | (uint32_t)t;
}

/* hook at 0x0804a5fc (HAL_DMA2D_Start -> DMA2D_SetConfig): a new theme first, then the stock call */
void theme_setconfig(void *h, uint32_t color, uint32_t dst, uint32_t w, uint32_t hgt)
{
    int t = fast_get(SETTINGS, P_THEME);
    t = t < 0 || t > NTHEMES ? 0 : t;
    if (APPLIED != (0x54480000u | (uint32_t)t) && !(APPLIED >> 16 != 0x5448 && t == 0))
        theme_apply(t);
    fw_dma2d_setconfig(h, color, dst, w, hgt);
}
