/*
 * More modulation sources (bitbox-mk2-semi). bitbox MK2 2.3.4.
 *
 * Pad modulation: the MOD page's Source list (0x36, 19 names at 0x080c5e0c) is turned into an engine source code by
 * the table at 0x080c5de4 (none 0, EXT1..8 15..22, IN1/2 23/24, TRIG 27, VEL 13, LFO 4, PTCH 25, MODW 26, MVOL 28,
 * MPAN 29, CC 0x8000; 0x0807a830) and back by two searches (0x08083d8a: names, 0x0809182c: the MOD page). The voice
 * start (0x080af360) routes the per-voice codes 4 (the voice's LFO, voice +0x518), 13 (velocity, +0x80) and 14 (+0x88:
 * (note - 60) / 60, set at note on, 0x080b148c) into the voice's parameters; code 14 has no list entry stock.
 * KEY: a 20th list entry for code 14, keyboard tracking (MIDI note relative to C3; e.g. Cutoff follows the keys).
 * Data only: the two tables in the cave, the three literals and the three counts 19 -> 20 (patches/modsrc.py).
 */
#include <stdint.h>

const uint16_t mod_codes[20] = {0, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 27, 13, 4, 25, 26, 28, 29, 0x8000, 14};
const char *const mod_names[20] = {
    "none", "EXT1", "EXT2", "EXT3", "EXT4", "EXT5", "EXT6", "EXT7", "EXT8", "IN1", "IN2", "TRIG", "VEL", "LFO",
    "PTCH", "MODW", "MVOL", "MPAN", "CC", "KEY"};
