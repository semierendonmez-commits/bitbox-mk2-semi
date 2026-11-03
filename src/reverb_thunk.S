// ported to the bitbox MK2 2.3.4 by tools/port_mk2.py
/*
 * The reverb process's core call (bl 0x080ab0a4 at 0x080aaaf2): r0 state, r1 in, r2 outL, r3 outR, [sp] n. The object
 * (r4 there) tells which reverb cell this is, so park it, then fall into reverb_core with the call's registers and stack
 * untouched.
 */
    .syntax unified
    .thumb
    .text

    .global reverb_core_thunk
    .thumb_func
    .type reverb_core_thunk, %function
reverb_core_thunk:
    ldr     ip, =0x2001bbf0
    str     r4, [ip]
    b.w     reverb_core
    .ltorg
