/* [mk2] vertical waveform zoom (vzoom.c): the tile painter 0x0806298c (fp = the widget) converts its amplitude
   scale with `vcvt.f32.s32 s16, s15` at 0x08062a60, 0x08062b16, 0x08062c18 -> bl vzoom_scale_thunk: the same
   conversion, times the gain when fp is the zoomed widget. Flags are not live at these sites (the next compare
   sets them); ip is unused by the painter; s15 is reloaded after each site but kept anyway. */
    .syntax unified
    .thumb
    .text
    .global vzoom_scale_thunk
    .thumb_func
    .type vzoom_scale_thunk, %function
vzoom_scale_thunk:
    vcvt.f32.s32 s16, s15
    push    {r0}
    ldr     r0, =0x2001c340
    ldr     ip, [r0]
    cmp     ip, fp
    bne     1f
    vpush   {s14}
    vldr    s14, [r0, #4]
    vmul.f32 s16, s16, s14
    vpop    {s14}
1:  pop     {r0}
    bx      lr
    .ltorg
