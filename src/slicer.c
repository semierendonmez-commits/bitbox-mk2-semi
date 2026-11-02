/*
 * Slicer Sync Grid (bitbox-mk2-semi; forum wish "Slicer mode with the sync features of Clip mode"). bitbox MK2 2.3.4.
 *
 * Stock Slicer Sync (0xd5, On / Off, pad +0x256) plays each slice in a duration scaled by the tempo ratio: the slice
 * voice start 0x080b5a34 takes (slice length) x (pad tempo +0x258 / current tempo, transport +0x18) + 0.5 and turns it
 * into an integer with __aeabi_d2iz (bl at 0x080b5aaa); 0x080b576c then plays the slice in that many output samples.
 * Sample lengths rarely fit the grid exactly, so slices sequenced back to back (Slice Seq) drift against the clock.
 * Sync Grid (new list, after Sync in the Slicer's Pos list): Free (stock) or 1 bar .. 1/32: the duration is rounded to
 * the nearest whole number of grid steps at the current tempo (at least one), so every slice ends on the grid, as Clip
 * mode's Sync keeps a clip on the bar. Only with Sync On (the stock path).
 */
#include <stdint.h>

#pragma GCC optimize("O2")

#define FN(addr) ((addr) | 1u)
typedef void (*register_list_fn)(void *table, int param, const char *label, const char *const *names, int count,
                                 const char *xml);
#define fw_register_list ((register_list_fn)FN(0x0806a27c))   /* [mk2] */

unsigned pad_setting(const uint8_t *pad, unsigned param);   /* filter.c */

#define P_SGRID 0x1b6
static const char *const grid_names[] = {"Free", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32"};
static const float grid_beats[] = {0.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f};
#define GRIDS 7

void slicer_register(void *table)
{
    fw_register_list(table, P_SGRID, "Sync Grid:", grid_names, GRIDS, "slicersyncgrid");
}

/* thunk slicer_len_thunk (bl at 0x080b5aaa): r0:r1 = the duration as a double, r2 = pad, r3 = the transport */
int slicer_len(uint32_t lo, uint32_t hi, const uint8_t *pad, const uint8_t *transport)
{
    union { uint64_t u; double d; } v = {((uint64_t)hi << 32) | lo};
    double x = v.d;
    if (!(x > -2147483648.0 && x < 2147483647.0))
        return x > 0.0 ? 0x7fffffff : (int)0x80000000;
    int n = (int)x;                                     /* as __aeabi_d2iz: toward zero */
    unsigned g = pad_setting(pad, P_SGRID);
    if (g == 0 || g >= GRIDS || !transport || n <= 0)
        return n;
    float bpm = *(const float *)(transport + 0x18);
    if (!(bpm >= 20.0f && bpm <= 400.0f))
        return n;
    float unit = 48000.0f * 60.0f / bpm * grid_beats[g];
    float q = (float)n / unit;
    int steps = (int)(q + 0.5f);
    if (steps < 1)
        steps = 1;
    return (int)((float)steps * unit + 0.5f);
}
