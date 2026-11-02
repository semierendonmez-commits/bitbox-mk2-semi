/*
 * Master section (master.c) hook, bitbox MK2 2.3.4:
 * - 0x08098190 `ldrb.w r3, [r8, r3]` (the block driver's compressor flag, after the FX returns 0xa / 0xb and the
 *   master EQ; bus 0 = r4; micro 0x08096d1c used sl): master_dist(bus 0) first, then the load. Flags are set by the
 *   cmp after it; lr is not live.
 * The micro's Tools-button and right-arrow thunks are gone: the MK2 has Tools tabs instead (patches/master.py).
 */
    .syntax unified
    .thumb
    .text

    .global master_dist_thunk
    .thumb_func
    .type master_dist_thunk, %function
master_dist_thunk:
    push    {r0, r1, r2, r3, ip, lr}
    vpush   {s0-s15}
    mov     r0, r4
    bl      master_dist
    vpop    {s0-s15}
    pop     {r0, r1, r2, r3, ip, lr}
    ldrb.w  r3, [r8, r3]
    bx      lr
