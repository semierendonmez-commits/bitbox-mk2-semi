# ported to the bitbox MK2 2.3.4 by tools/port_mk2.py
"""Reverb types for the FX2 reverb cell: Type (Plate / Room / Hall / Chamber) and eleven settings incl. Level (src/reverb.c).
Needs the cave; the page filter for Type = Plate lives in clip_page (patches/sound.py hooks the page builder), so this
patchset goes together with `sound`. Addresses: re/symbols.md "Reverb"."""
import os

from thumb import ROOT, bl, symbols

sym = symbols(os.path.join(ROOT, "out", "cave.elf"))

HOOKS = [
    (0x0806BC8E, 0x0806A27C, "reverb_register"),         # list 0x178 "FX2" -> "Type:" + the eleven settings
    (0x08089960, 0x080893D6, "reverb_defaults_first"),   # reverb record (type 0x44): Type before "Decay 600"
    (0x08089976, 0x080893D6, "reverb_defaults_last"),    # ... settings after "Damping 500"
    (0x080AAAF2, 0x080AB0A4, "reverb_core_thunk"),       # reverb process: the plate core call
]
EXCLUSIVE = {0x080AB0A4}

PATCHES = [(at, bl(at, stock), bl(at, sym[name])) for at, stock, name in HOOKS]
