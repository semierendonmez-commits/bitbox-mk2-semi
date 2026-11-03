/*
 * Vertical waveform zoom (bitbox-mk2-semi, own code; idea from vwal's patcher). bitbox MK2 2.3.4.
 *
 * The sample waveform widget (vtable 0x080c1494: touch down [4] 0x0806240a, move [5] 0x08062510, up [6] 0x08062664)
 * already handles a two-finger pinch: state +0x1e0 = 6, and per move it keeps the start / current finger distance
 * ratios, x at +0x220 (used for the stock horizontal zoom) and y at +0x224 (computed, never used; 1.0 unless the
 * fingers started > 29 px apart vertically). Here a vertical pinch sets a display gain for the waveform: spread the
 * fingers vertically = taller (x1 .. x32), pinch = back down. The move handler is wrapped (vtable slot); when the gain
 * changes by more than 3 % the widget's tiles are invalidated (0x080628fc) so the next paint redraws them, and the
 * tile painter's three amplitude scales (vcvt s16 at 0x08062a60 / 0x08062b16 / 0x08062c18; the painter clips to the
 * tile height already) are multiplied by the gain (vzoom_thunk.S). Only the waveform display changes, never the
 * sample. The gain belongs to one widget (the last one pinched) and stays until pinched again.
 */
#include <stdint.h>

#pragma GCC optimize("O2", "no-tree-loop-distribute-patterns")

#define FN(addr) ((addr) | 1u)
typedef void (*move_fn)(void *w, const void *pos, int id);
typedef void (*inval_fn)(void *w);
#define fw_wave_move  ((move_fn)FN(0x08062510))       /* [mk2] waveform widget touch move */
#define fw_wave_inval ((inval_fn)FN(0x080628fc))      /* [mk2] invalidate the widget's 7 tiles */

struct vz { uint8_t *widget; float gain, base; uint32_t pinching; };   /* layout used by vzoom_thunk.S */
#define VZ ((volatile struct vz *)0x2406d340u)

#define GMIN 1.0f
#define GMAX 32.0f

void vzoom_move(uint8_t *w, const void *pos, int id)
{
    fw_wave_move(w, pos, id);
    if (VZ->widget != w || !(VZ->gain >= GMIN && VZ->gain <= GMAX)) {
        VZ->widget = w;
        VZ->gain = 1.0f;
        VZ->pinching = 0;
    }
    if (w[0x1e0] != 6) {
        VZ->pinching = 0;
        return;
    }
    if (!VZ->pinching) {
        VZ->pinching = 1;
        VZ->base = VZ->gain;
    }
    float r = *(const float *)(w + 0x224);            /* start / current vertical distance */
    if (r < 0.0f)
        r = -r;
    if (!(r > 0.01f))
        return;
    float g = VZ->base / r;
    if (g < GMIN)
        g = GMIN;
    if (g > GMAX)
        g = GMAX;
    float d = g - VZ->gain;
    if (d < 0.0f)
        d = -d;
    if (d > 0.03f * VZ->gain || ((g == GMIN || g == GMAX) && g != VZ->gain)) {
        VZ->gain = g;
        fw_wave_inval(w);
    }
}
