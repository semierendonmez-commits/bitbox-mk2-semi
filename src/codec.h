/* codec.c: G.711 mu-law and IMA ADPCM round trips (bitbox-mk2-semi) */
#ifndef CODEC_H
#define CODEC_H
#include <stdint.h>

struct adpcm {
    int32_t pred, index;
};
float adpcm_q(struct adpcm *a, float x);
float ulaw_q(float x);

#endif
