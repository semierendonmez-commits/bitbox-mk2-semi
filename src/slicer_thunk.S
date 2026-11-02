/* [mk2] Slicer Sync Grid (slicer.c): bl __aeabi_d2iz at 0x080b5aaa in the slice voice start 0x080b5a34 (pad r4, the
   transport is its first stack argument, [sp, #0x60] at this point) -> slicer_len(lo, hi, pad, transport). */
    .syntax unified
    .thumb
    .text
    .global slicer_len_thunk
    .thumb_func
    .type slicer_len_thunk, %function
slicer_len_thunk:
    mov     r2, r4
    ldr     r3, [sp, #0x60]
    b.w     slicer_len
