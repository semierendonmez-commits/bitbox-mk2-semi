"""bitbox MK2 2.3.4: a KEY modulation source (keyboard tracking, the engine's per-voice code 14) as a 20th entry of the
pads' MOD Source list (src/modsrc.c). Data only: the code and name tables in the cave, their literals, the counts."""
import os
import struct

from thumb import ROOT, symbols

sym = symbols(os.path.join(ROOT, "out", "cave.elf"))
CODES, NAMES = 0x080C5DE4, 0x080C5E0C

PATCHES = (
    [(at, struct.pack("<I", CODES), struct.pack("<I", sym["mod_codes"])) for at in (0x0807A8BC, 0x08083F48, 0x08091A10)]
    + [(at, struct.pack("<I", NAMES), struct.pack("<I", sym["mod_names"])) for at in (0x0806AACC, 0x08083F4C)]
    + [(0x0806A9A6, bytes.fromhex("1323"), bytes.fromhex("1423")),     # registration: movs r3, #19 -> #20 (list size)
       (0x08083D98, bytes.fromhex("132b"), bytes.fromhex("142b")),     # code -> name search: cmp r3, #19 -> #20
       (0x080917FA, bytes.fromhex("1320"), bytes.fromhex("1420"))]     # MOD page search: movne r0, #19 -> #20
)
