// ported to the bitbox MK2 2.3.4 by tools/port_mk2.py
/*
 * Per-voice filter call sites on the bitbox micro 2.3.4. r0 = filter object, r1 = audio buffer; r2 = the pad object
 * the voice belongs to (re/symbols.md).
 * Sample / granular pads (class A, 0x080ae9bc): filter object at voice + 0xa4, the pad object is in r5
 *   (the Blackbox has it in r6).
 * Clip / slicer pads (class B, 0x080b6df4): filter object at sub + 0x14c, where sub = pad + 0x260 or pad + 0x670 (two
 *   voices per pad), so the Blackbox's "r0 - 0x3ac" is wrong for the second one; the pad object is in r6 (mov r6, r0
 *   at 0x080b6bfc, unchanged up to the call).
 */
    .syntax unified
    .thumb
    .text

    .global filter_voice_a
    .thumb_func
    .type filter_voice_a, %function
filter_voice_a:
    mov     r2, r5
    b.w     filter_process

    .global filter_voice_b
    .thumb_func
    .type filter_voice_b, %function
filter_voice_b:
    mov     r2, r6
    b.w     filter_process
