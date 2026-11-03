"""bitbox MK2 2.3.4 screen saver with a live monitor (src/ss.c): Tools -> More, page Saver (time, view). Hooks the app
dispatcher's three call sites (a touch wakes it and is swallowed) and the HAL UART receive ISR pointer (MIDI wakes
it); the monitor and the timer run from master_dist (patches/master.py), the buffer is allocated by fx3_label
(patches/fx3.py). Needs `master`, `fx`, `fx3` and the cave."""
import os
import struct

from thumb import ROOT, bl, symbols

sym = symbols(os.path.join(ROOT, "out", "cave.elf"))

DISPATCH = 0x0808073C
SET_ADDRESS = 0x0804C03A                             # HAL_LTDC_SetAddress: the UI flips layer 2 every frame
HOOKS = [(at, DISPATCH, "ss_dispatch") for at in (0x08071530, 0x080810D4, 0x08082542)] \
    + [(at, SET_ADDRESS, "ss_set_address") for at in (0x08043666, 0x08043686)]
EXCLUSIVE = {DISPATCH, SET_ADDRESS}
RX_ISR = 0x0804E9C9                                  # HAL UART_RxISR_8BIT, set as huart->RxISR at two sites
DATA = [(at, RX_ISR, "ss_rx_isr") for at in (0x0804E9B8, 0x0804EB44)]

PATCHES = (
    [(at, bl(at, stock), bl(at, sym[name])) for at, stock, name in HOOKS]
    + [(at, struct.pack("<I", old), struct.pack("<I", sym[name] | 1)) for at, old, name in DATA]
)
