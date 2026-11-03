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
#define P_SSTR  0x1b7                                   /* Slice Str: 0 Cut (stock), 1 Stretch (WSOLA), 2 Tape */
static const char *const str_names[] = {"Cut", "Stretch", "Tape"};
static const char *const grid_names[] = {"Free", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32"};
static const float grid_beats[] = {0.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f};
#define GRIDS 7

void slicer_register(void *table)
{
    fw_register_list(table, P_SGRID, "Sync Grid:", grid_names, GRIDS, "slicersyncgrid");
    fw_register_list(table, P_SSTR, "Slice Str:", str_names, 3, "slicerstretch");
}

/* --- Slice Str: play the slice's source range over the synced duration ---------------------------------------------
 * Stock Sync On plays a slice at its pitch rate and only cuts it to the tempo-scaled duration (0x080b576c: the slot's
 * play length = duration; slower tempo -> a gap, faster -> cut). Here the slice start (bl 0x080b576c at 0x080b5ace,
 * the Sync path; args: pad, .., start, duration, .., 0, length, flag; returns the slot 0..3) notes per slot the speed
 * c = length / duration (file frames per output frame). clip_render (stretch.c) then reads the slot: Stretch = WSOLA
 * grains at the pitch rate, the analysis position moving by c (the pitch stays, the slice fills the duration exactly);
 * Tape = the stock read at rate c (pitch follows the tempo, like a turntable). Cut = stock. */
typedef int (*start_fn)(uint8_t *pad, int a1, int a2, int a3, int start, int dur, int a6, int end, int len, int flag);
#define fw_slice_start ((start_fn)FN(0x080b576c))         /* [mk2] */
#define SPEEDS ((volatile float *)0x2406f840u)            /* [mk2] 16 pads x 4 slots */

int slicer_start(uint8_t *pad, int a1, int a2, int a3, int start, int dur, int a6, int end, int len, int flag)
{
    int slot = fw_slice_start(pad, a1, a2, a3, start, dur, a6, end, len, flag);
    uint32_t id = *(const uint32_t *)(pad + 0x18);
    unsigned row = (id >> 8) & 0xff, col = id & 0xff;
    if (slot >= 0 && slot < 4 && row < 4 && col < 4) {
        SPEEDS[(row * 4 + col) * 4 + slot] = dur > 0 && len > 0 ? (float)len / (float)dur : 0.0f;
        /* the stock cuts the slot at min(duration, length / pitch rate) output frames (0x080b58c0: play length at
           slot +0x58 = that - 0x200, between 0x100-frame fades at +0x54 / +0x5c); Stretch / Tape fill the duration */
        unsigned m = pad_setting(pad, P_SSTR);
        if ((m == 1 || m == 2) && pad[0x254] && pad[0x256] && dur > 0x200)
            *(volatile int32_t *)(pad + 0x70 * slot + 0x58) = dur - 0x200;
    }
    return slot;
}

/* for clip_render: 0 = stock; 1 = Stretch, 2 = Tape with *speed (file frames per output frame) */
int slicer_stretch(const uint8_t *pad, int slot_index, float *speed)
{
    if (!pad[0x254] || !pad[0x256])                     /* sliced, Sync On */
        return 0;
    unsigned m = pad_setting(pad, P_SSTR);
    if (m != 1 && m != 2)
        return 0;
    uint32_t id = *(const uint32_t *)(pad + 0x18);
    unsigned row = (id >> 8) & 0xff, col = id & 0xff;
    if (slot_index < 0 || slot_index > 3 || row > 3 || col > 3)
        return 0;
    float c = SPEEDS[(row * 4 + col) * 4 + slot_index];
    if (!(c > 0.05f && c < 8.0f))
        return 0;
    *speed = c;
    return (int)m;
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
