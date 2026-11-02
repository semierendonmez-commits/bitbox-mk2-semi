"""bitbox MK2 2.3.4 FX section (src/fx.c, src/fx_thunk.S): FX return routing (FX Out, Tools -> Rec) and the delay's
Level; delay types Digital / Tape / BBD with modulation (Mod Rate, Mod Depth, Flutter, Mod Phase) and loop colour
(Drive, Tone) on a second page of the delay cell; the reverb's second page (reverb.c's Type list); Compressor bus
choice (Off / Out1/2 / FX1/2 / Both, master.c). Needs `fidelity` (registers the parameters), `sound` (clip_page),
`reverb`, `master` and the cave."""
import os
import struct

from thumb import ROOT, bl, symbols

sym = symbols(os.path.join(ROOT, "out", "cave.elf"))

BUF_MIX, LINE_READ, LINE_WRITE, DELAY_CORE = 0x080A46D0, 0x080A91F4, 0x080A915C, 0x080A89D4
HOOKS = [
    (0x0809816A, BUF_MIX, "fx_ret1_thunk"),          # block driver: bus 0 += FX1 return (bus 0xa)
    (0x0809817A, BUF_MIX, "fx_ret2_thunk"),          # ... += FX2 return (bus 0xb)
    (0x080A905E, DELAY_CORE, "delay_core"),          # delay process: its core
    (0x08089952, 0x080893D6, "delay_defaults_last"), # delay record defaults: last ("Filter" 0x18), then ours
    (0x0806B308, 0x0806A2D4, "comp_register"),       # "Compressor:" 0x96 knob -> list Off / Out1/2 / FX1/2 / Both
    (0x08080C00, 0x0807F7BC, "fx_knob"),             # dispatcher, knob command: a Type change shows the page again
] + [(at, LINE_READ, "delay_read") for at in (0x080A8CE4, 0x080A8CF2, 0x080A8DD4, 0x080A8EB6)] \
  + [(at, LINE_WRITE, "delay_write") for at in (0x080A8DC0, 0x080A8DCA, 0x080A8DFC, 0x080A8EE8, 0x080A8EF2)]

EXCLUSIVE = {DELAY_CORE}

# Tools -> Rec tab: its free fourth slot (ids 0x080c7ebc + 16, column-wise; bottom right) = FX Out
IDS, P_FX_OUT = 0x080C7EBC, 0x1B5

PATCHES = (
    [(at, bl(at, stock), bl(at, sym[name])) for at, stock, name in HOOKS]
    + [(IDS + 16 + 6, bytes(2), struct.pack("<H", P_FX_OUT))]
)
