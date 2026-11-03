"""bitbox MK2 2.3.4 UI additions (src/dialdef.c): double tap on a dial -> its default value (the dial widget's
message handler, vtable slot 0x080c9528). Needs the cave."""
import os
import struct

from thumb import ROOT, symbols

sym = symbols(os.path.join(ROOT, "out", "cave.elf"))

DIAL_MSG = 0x0808FA25                                # dial widget message handler, class vtable 0x080c94f8 [12]
WORDS = [(0x080C9528, DIAL_MSG, "dialdef_msg")]

PATCHES = [(at, struct.pack("<I", old), struct.pack("<I", sym[name] | 1)) for at, old, name in WORDS]
