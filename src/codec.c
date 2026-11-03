/*
 * Small speech / telephone codecs for the lo-fi modes (bitbox-mk2-semi, own code; the idea from TRANSCODE /
 * FRAMELOSS, which run Frieve-A's EffeTune codecs (MIT); these are written from the public standards):
 *   ulaw_q   G.711 mu-law, 8 bits: the sample through the logarithmic 8-bit code and back.
 *   adpcm_q  IMA / DVI ADPCM, 4 bits per sample: a step-size-adapting predictor; the decoder's output.
 * Used by the Fidelity modes Phone / ADPCM / u-Law (filter.c) and the delay type Codec (fx.c).
 */
#include <stdint.h>

#pragma GCC optimize("O2", "no-tree-loop-distribute-patterns")

#include "codec.h"

static const int16_t step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767};
static const int8_t index_table[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

static int to16(float x)
{
    float s = x * 32767.0f;
    s = s > 32767.0f ? 32767.0f : s < -32768.0f ? -32768.0f : s;
    return (int)(s + (s >= 0.0f ? 0.5f : -0.5f));
}

float adpcm_q(struct adpcm *a, float x)
{
    int sample = to16(x), pred = a->pred, index = a->index;
    if (index < 0 || index > 88)
        index = 0;
    int step = step_table[index], diff = sample - pred, code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    int vpdiff = step >> 3;
    if (diff >= step) {
        code |= 4;
        diff -= step;
        vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
        code |= 2;
        diff -= step;
        vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
        code |= 1;
        vpdiff += step;
    }
    pred += code & 8 ? -vpdiff : vpdiff;
    pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
    index += index_table[code & 7];
    a->index = index < 0 ? 0 : index > 88 ? 88 : index;
    a->pred = pred;
    return (float)pred * (1.0f / 32768.0f);
}

/* G.711 mu-law: 14-bit magnitude with bias 33, 8 segments of 16 steps */
float ulaw_q(float x)
{
    int pcm = to16(x) >> 2, sign = 0;
    if (pcm < 0) {
        pcm = -pcm;
        sign = 1;
    }
    if (pcm > 8158)
        pcm = 8158;                                      /* + bias: at most 0x1fff, segment 7 */
    pcm += 33;
    int seg = 0;
    while (seg < 7 && pcm > (0x3f << seg))
        seg++;
    int mant = (pcm >> (seg + 1)) & 0xf;
    int t = ((mant << 3) + 0x84) << seg;               /* decode (16-bit scale) */
    t -= 0x84;
    return (float)(sign ? -t : t) * (1.0f / 32768.0f);
}
