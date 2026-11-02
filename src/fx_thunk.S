@ FX returns (src/fx.c): the block driver's two return mixes, bus 0 += bus 0xa / 0xb (bl 0x080a46d0 at 0x0809816a,
@ 0x0809817a; r0 = bus 0, r1 = the return, engine in sl). Pass the engine and which return to fx_return.
    .syntax unified
    .thumb
    .text

    .global fx_ret1_thunk
    .thumb_func
fx_ret1_thunk:
    mov r2, sl
    movs r3, #0
    b.w fx_return

    .global fx_ret2_thunk
    .thumb_func
fx_ret2_thunk:
    mov r2, sl
    movs r3, #1
    b.w fx_return
