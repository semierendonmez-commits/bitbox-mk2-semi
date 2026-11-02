/* [mk2]
 * FX3 (fx3.c) hooks in the pad block processes (bitbox MK2 2.3.4; micro sites in portkit):
 * - the "any send on" checks, `vldr s15, [rX]` (FX1 Send gain) at 0x080b2438 (sample pads, rX = r6, pad r4) and
 *   0x080b8eb4 (clip pads, rX = fp, pad r4): s15 = FX1 Send; if that is 0 and FX3 Send is on, 1.0 (flags are set by
 *   the vcmp after). Everything else kept.
 * - the final mix 0x080a46d0(out, scratch) at 0x080b3658 (engine sb) and 0x080b916e (engine r8):
 *   fx3_padmix(out, scratch, pad r4, engine).
 */
    .syntax unified
    .thumb
    .text

    .macro CHECK name, reg
    .global \name
    .thumb_func
    .type \name, %function
\name:
    vldr    s15, [\reg]
    vcmp.f32 s15, #0
    vmrs    APSR_nzcv, fpscr
    it      ne
    bxne    lr
    push    {r0, r1, r2, r3, ip, lr}
    vpush   {s0-s14}
    sub     sp, sp, #4
    mov     r0, r4
    bl      fx3_send_on
    add     sp, sp, #4
    vpop    {s0-s14}
    cmp     r0, #0
    pop     {r0, r1, r2, r3, ip, lr}
    it      ne
    vmovne.f32 s15, #1.0
    bx      lr
    .endm

    CHECK fx3_check_a, r6
    CHECK fx3_check_b, fp

    .global fx3_mix_a
    .thumb_func
    .type fx3_mix_a, %function
fx3_mix_a:
    mov     r2, r4
    mov     r3, sb
    b.w     fx3_padmix

    .global fx3_mix_b
    .thumb_func
    .type fx3_mix_b, %function
fx3_mix_b:
    mov     r2, r4
    mov     r3, r8
    b.w     fx3_padmix
