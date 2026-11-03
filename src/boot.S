/* [mk2] Boot: zero the patch RAM (DTCM 0x2001a000..0x2001ffff, tests/test_ram.py) before anything runs.
   The reset handler's first call, bl SystemInit (0x08054824) at 0x08040518, comes here: SP = 0x24080000, the FPU is
   still off (CPACR is set after SystemInit), .data / .bss not yet set up - so plain integer stores only, then on to
   SystemInit with the reset handler's lr. On a unit RAM holds garbage at power-up (or the last run's data after a
   reset); the simulator starts with zeros, which hid code that read its state before setting it (sT4 / sT8 black
   screen: modenv_tick followed a garbage pad pointer). */
    .syntax unified
    .thumb
    .text
    .global boot_zero
    .thumb_func
    .type boot_zero, %function
boot_zero:
    ldr     r0, =0x2001a000
    ldr     r1, =0x20020000
    movs    r2, #0
1:  str     r2, [r0], #4
    cmp     r0, r1
    blo     1b
    ldr     r0, =0x08054825          /* SystemInit */
    bx      r0
    .ltorg
