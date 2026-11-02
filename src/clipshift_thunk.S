// ported to the bitbox MK2 2.3.4 by tools/port_mk2.py
/*
 * Clip shift (clipshift.c): the clip reader's stream calls with the pad (r4 at every site) added as the last argument.
 * 0x080a58a8 (stream, dst, n, pos, [sp] ch)  at 0x080b6758, 0x080b6848 -> clip_read1(..., ch, pad)
 * 0x080a5870 (stream, l, r, n, [sp] pos)     at 0x080b67a6, 0x080b681a -> clip_read2(..., pos, pad)
 * 0x080a59cc (stream, pos)                   at 0x080b58fa, 0x080b5c74 -> clip_seek(stream, pos, pad)
 * Play length: `ldrd r1, r2, [r6, #0x240]` (0x080b7056, 0x080b7242; pad in r6, `add r2, r1; cmp` follow) ->
 *   r1 = pad[0x240], r2 = clip_segments(pad) - r1, everything else kept (flags are set by the cmp after).
 * Stretch (segment start 0x080b5a34, pad in r4):
 *   0x080b5b90 `vmul.f32 s16, s15, s16` -> s16 = s15 x s16 x clip_speed(pad); everything else kept
 *   0x080b5adc `ldr.w r3, [r4, #0x240]` -> r3 = clip_last_count(pad); everything else kept (flags: subs / cmp follow)
 */
    .syntax unified
    .thumb
    .text

    .macro STACK5 name, target
    .global \name
    .thumb_func
    .type \name, %function
\name:
    push    {r4, lr}
    ldr     ip, [sp, #8]          /* the caller's fifth argument */
    sub     sp, sp, #8
    str     ip, [sp]
    str     r4, [sp, #4]          /* sixth: the pad */
    bl      \target
    add     sp, sp, #8
    pop     {r4, pc}
    .endm

    STACK5 clip_read1_thunk, clip_read1
    STACK5 clip_read2_thunk, clip_read2

    .global clip_seek_thunk
    .thumb_func
    .type clip_seek_thunk, %function
clip_seek_thunk:
    mov     r2, r4
    b.w     clip_seek

    .global clip_segments_thunk
    .thumb_func
    .type clip_segments_thunk, %function
clip_segments_thunk:
    push    {r0, r3, ip, lr}
    mov     r0, r6
    bl      clip_segments
    ldr     r1, [r6, #0x240]
    sub     r2, r0, r1
    pop     {r0, r3, ip, lr}
    bx      lr

    .global clip_pos_thunk
    .thumb_func
    .type clip_pos_thunk, %function
clip_pos_thunk:
    push    {r0, r1, r2, r3, ip, lr}
    vpush   {s0-s15}
    mov     r0, r4
    bl      clip_speed
    vmov    r0, s0
    vpop    {s0-s15}
    vmul.f32 s16, s15, s16
    vpush   {s14}
    vmov    s14, r0
    vmul.f32 s16, s16, s14
    vpop    {s14}
    pop     {r0, r1, r2, r3, ip, lr}
    bx      lr

    .global clip_last_thunk
    .thumb_func
    .type clip_last_thunk, %function
clip_last_thunk:
    push    {r0, r1, r2, ip, lr}
    sub     sp, sp, #4
    vpush   {s0-s15}
    mov     r0, r4
    bl      clip_last_count
    mov     r3, r0
    vpop    {s0-s15}
    add     sp, sp, #4
    pop     {r0, r1, r2, ip, lr}
    bx      lr

    .ltorg
