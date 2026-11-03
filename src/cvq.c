/*
 * CV input scale / offset and a pitch quantizer (bitbox-mk2-semi, own code). bitbox MK2 2.3.4.
 *
 * Settings: Tools -> More, page "CV In": CV Scale % (0 .. 400, default 100), CV Offset % (-100 .. 100 of the
 * range), Quantize (Off, Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Harmonic Minor, Pentatonic Major /
 * Minor, Blues, Whole tone) and Root (C .. B).
 *
 * The CV inputs are scanned on the engine side (0x08046600, ADC averages, hysteresis) and posted as 0x35 events:
 * +8 the input (group 0..3, channel 0..4), +0xc the value (ADC counts x 1000 / 2048: +-1000 = full scale), +0x10 the
 * pitch in semitones (1 V/oct). The engine queue reader passes them to 0x080967e8(engine, group, channel, value,
 * semitones) at 0x08095c22: the value becomes the input's modulation value (engine +0xd4ac, value / 1000, also the
 * CV-as-gate detection), the semitones the pitch of the pads played from that input. Here that call is wrapped:
 *   value'     = value x scale / 100 + offset x 10   (so a 0..5 V source can be spread over -1..1: 200 %, -100 %)
 *   semitones' = the nearest note of the scale (ties: down), when Quantize is on
 * The pitch is never scaled (V/oct stays V/oct).
 */
#include <stdint.h>

#pragma GCC optimize("O2", "no-tree-loop-distribute-patterns")

#define FN(addr) ((addr) | 1u)
typedef void (*cv_fn)(void *engine, unsigned group, unsigned ch, int value, int semis);
#define fw_cv_event ((cv_fn)FN(0x080967e8))           /* [mk2] engine: a CV input's value and pitch */
int fast_get(const void *store, unsigned param);

#define APP       ((uint8_t *)0x24015170u)
#define SETTINGS  (APP + 0x7ed0u)
#define P_CVSCALE 0x1bb
#define P_CVOFFS  0x1bc
#define P_QSCALE  0x1bd
#define P_QROOT   0x1be

/* scale masks, bit n = semitone n above the root */
static const uint16_t qmask[] = {
    0xfff,                                              /* Off (chromatic) */
    0xab5,                                              /* Major      0 2 4 5 7 9 11 */
    0x5ad,                                              /* Minor      0 2 3 5 7 8 10 */
    0x6ad,                                              /* Dorian     0 2 3 5 7 9 10 */
    0x5ab,                                              /* Phrygian   0 1 3 5 7 8 10 */
    0xad5,                                              /* Lydian     0 2 4 6 7 9 11 */
    0x6b5,                                              /* Mixolydian 0 2 4 5 7 9 10 */
    0x9ad,                                              /* Harm Minor 0 2 3 5 7 8 11 */
    0x295,                                              /* Pent Maj   0 2 4 7 9 */
    0x4a9,                                              /* Pent Min   0 3 5 7 10 */
    0x4e9,                                              /* Blues      0 3 5 6 7 10 */
    0x555,                                              /* Whole      0 2 4 6 8 10 */
};
#define NQ (int)(sizeof qmask / sizeof qmask[0])

int cvq_quantize(int semis, int scale, int root)
{
    if (scale <= 0 || scale >= NQ)
        return semis;
    unsigned m = qmask[scale];
    root %= 12;
    if (root < 0)
        root += 12;
    for (int d = 0; d <= 6; d++) {                      /* nearest: down first */
        int lo = semis - d, hi = semis + d;
        int dl = ((lo - root) % 12 + 12) % 12, dh = ((hi - root) % 12 + 12) % 12;
        if (m >> dl & 1)
            return lo;
        if (m >> dh & 1)
            return hi;
    }
    return semis;
}

int cvq_value(int value)
{
    int sc = fast_get(SETTINGS, P_CVSCALE), of = fast_get(SETTINGS, P_CVOFFS);
    if (sc == 100 && of == 0)
        return value;
    if (sc < 0 || sc > 400)
        sc = 100;
    if (of < -100 || of > 100)
        of = 0;
    int v = value * sc / 100 + of * 10;
    if (v > 2000)
        v = 2000;
    if (v < -2000)
        v = -2000;
    return v;
}

void cvq_event(void *engine, unsigned group, unsigned ch, int value, int semis)
{
    fw_cv_event(engine, group, ch, cvq_value(value),
                cvq_quantize(semis, fast_get(SETTINGS, P_QSCALE), fast_get(SETTINGS, P_QROOT)));
}
