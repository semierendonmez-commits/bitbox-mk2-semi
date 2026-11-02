"""bitbox MK2 2.3.4 FX3 (src/fx3.c, src/fx3_thunk.S): a third send effect (a reverb with every reverb type) on the FX2
cell: a second touch on the selected FX2 cell turns it into FX3 (and back). FX3 Send on the pads' Pos list
(patches/sound.py). Needs `fidelity` (registers FX3 Send, pad record entry), `sound`, `reverb`, `fx` (the return,
fx_return) and the cave. Ported from portkit's micro patches/fx3.py; the micro's Effects-screen cell and arrow-key
pages are replaced by the FX2 / FX3 switch."""
import os

from thumb import ROOT, bl, symbols

sym = symbols(os.path.join(ROOT, "out", "cave.elf"))

BUF_MIX = 0x080A46D0
HOOKS = [
    (0x080B3658, BUF_MIX, "fx3_mix_a"),          # sample pads: final mix scratch -> output, first scratch -> FX3 bus
    (0x080B916E, BUF_MIX, "fx3_mix_b"),          # clip pads
    (0x08072130, 0x0805D8CC, "fx3_label"),       # FX grid widgets' labels (boot): note FX2's widget, FX3 init
    (0x08080AC8, 0x0807D0C8, "fx3_set_cell"),    # dispatcher, set cell: second touch on FX2 <-> FX3
]

# FX cell record row 3 (app +0x2248, cell 0x330): its eight stock inits make it an empty type-0 record;
# `movs r3, #0` before each (the type byte for 0x08089e7c) -> `movs r3, #0x44` (reverb)
ROW3_TYPE = [0x0807A1F8, 0x0807E25C, 0x08083788, 0x0808ACD0, 0x0808B0E4, 0x0808DF38, 0x0807CC36, 0x0807CC5C]

THUNKS = [
    (0x080B2438, "d6ed007a", "fx3_check_a"),    # vldr s15, [r6]: sample pads' "any send on"
    (0x080B8EB4, "dbed007a", "fx3_check_b"),    # vldr s15, [fp]: clip pads'
]

PATCHES = (
    [(at, bl(at, stock), bl(at, sym[name])) for at, stock, name in HOOKS]
    + [(at, bytes.fromhex(old), bl(at, sym[name])) for at, old, name in THUNKS]
    + [(at, bytes.fromhex("0023"), bytes.fromhex("4423")) for at in ROW3_TYPE]
)
