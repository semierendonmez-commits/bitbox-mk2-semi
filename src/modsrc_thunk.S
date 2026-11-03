/* [mk2] ENV modulation source (modsrc.c): the route add's PTCH compare at 0x080af77e
   (cmp r7, #0x19; ite eq; addeq.w r1, r4, #0x13c; movne r1, #0 -> 0x080af788) -> r1 = modsrc_pick(r7, r4), back to
   0x080af788. r0, r2, r3, ip and lr live there: kept (r0 is the per-voice flag, ip the target, r2/r3 scratch but
   kept anyway). */
    .syntax unified
    .thumb
    .text
    .global modsrc_pick_thunk
    .thumb_func
    .type modsrc_pick_thunk, %function
modsrc_pick_thunk:
    push    {r0, r2, r3, ip, lr}
    vpush   {s0-s15}
    mov     r0, r7
    mov     r1, r4
    bl      modsrc_pick
    mov     r1, r0
    vpop    {s0-s15}
    pop     {r0, r2, r3, ip, lr}
    add     lr, lr, #6                  /* skip the addeq / movne (2 + 4 bytes ... see patches/modsrc.py) */
    bx      lr

/* the route add's pad-level source dispatch at 0x080af73a (cmp r7, #0x1d; bne.w 0x080af8b2 = no pad object for the
   code): MPAN (0x1d) -> 0x080af740 as stock, ENV (0x30) -> 0x080af774 (the compare chain with modsrc_pick_thunk),
   anything else -> 0x080af8b2. r3 is scratch here; lr is saved by the function. */
    .global modsrc_route_thunk
    .thumb_func
    .type modsrc_route_thunk, %function
modsrc_route_thunk:
    cmp     r7, #0x1d
    beq     1f
    sub     r3, r7, #0x30
    cmp     r3, #3
    bls     2f                          /* ENV, CHAOS, RAND, NET (0x30..0x33) */
    ldr     r3, =0x080af8b3
    bx      r3
1:  ldr     r3, =0x080af741
    bx      r3
2:  ldr     r3, =0x080af775
    bx      r3
    .ltorg
