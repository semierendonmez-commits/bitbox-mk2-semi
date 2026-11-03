"""bitbox MK2 2.3.4: more modulation sources for the pads' MOD Source list (src/modsrc.c): KEY (keyboard tracking, the
engine's per-voice code 14) and ENV (the pad's envelope, new code 0x30: a thunk in the route add picks its object).
The code and name tables in the cave, their literals, the counts 19 -> 21."""
import os
import struct

from thumb import ROOT, bl, symbols

sym = symbols(os.path.join(ROOT, "out", "cave.elf"))
CODES, NAMES = 0x080C5DE4, 0x080C5E0C

PATCHES = (
    [(at, struct.pack("<I", CODES), struct.pack("<I", sym["mod_codes"])) for at in (0x0807A8BC, 0x08083F48, 0x08091A10)]
    + [(at, struct.pack("<I", NAMES), struct.pack("<I", sym["mod_names"])) for at in (0x0806AACC, 0x08083F4C)]
    + [(0x0806A9A6, bytes.fromhex("1323"), bytes.fromhex("1523")),     # registration: movs r3, #19 -> #21 (list size)
       (0x08083D98, bytes.fromhex("132b"), bytes.fromhex("152b")),     # code -> name search: cmp r3, #19 -> #21
       (0x080917FA, bytes.fromhex("1320"), bytes.fromhex("1520"))]     # MOD page search: movne r0, #19 -> #21
    + [(0x080AF77E, bytes.fromhex("192f0cbf"), bl(0x080AF77E, sym["modsrc_pick_thunk"]))]   # route add: ENV's object
    + [(0x080AF73A, bytes.fromhex("1d2f40f0"), bl(0x080AF73A, sym["modsrc_route_thunk"]))]  # ... lets code 0x30 through
)
