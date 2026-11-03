"""bitbox MK2 2.3.4 master section (src/master.c, src/master_thunk.S): the stock compressor's settings (Comp Type
Stock / SSL, Threshold, Ratio, Attack, Release, Gain, Mix) and a master distortion (Type, Drive, Tone, Mix, Level),
on three new Tools tabs: Comp, Cmp2, Dist (and "MIDIIn" shortened to "MIDI" so six tabs fit). Needs `fidelity` (registers the parameters) and the cave.

Ported from portkit's micro patches/master.py: the DSP hooks through re/mk2_map.py; the micro's arrow-key pages are
replaced by Tools tabs (data: the label slots and id table of the tab builder 0x0807974c) and a wrapper on all callers
of that builder for the SSL knobs.
"""
import os
import struct

from thumb import ROOT, bl, symbols

sym = symbols(os.path.join(ROOT, "out", "cave.elf"))

TOOLS_PAGE = 0x0807974C
HOOKS = [
    (0x08089E30, 0x080893D6, "master_store_add"),   # settings store init 0x08089cbc: last add (0x17b), then ours
    (0x080983A8, 0x080BA43C, "master_comp"),        # block driver: compressor (engine +0xfb38): Stock / SSL, Mix
] + [(at, TOOLS_PAGE, "master_tools_page") for at in
     (0x0806D8BC, 0x0806DD80, 0x08075276, 0x080752A2, 0x08076DFE, 0x08076FD4, 0x08083E0A, 0x08093A30, 0x0809431E)]

THUNKS = [
    (0x08098190, "18f80330", "master_dist_thunk"),  # ldrb.w r3, [r8, r3]: compressor flag; distortion on bus 0 first
]

EXCLUSIVE = {TOOLS_PAGE}

# Tools tabs: label slots (6 pointers; the first empty one, "" at 0x080cb46c, ends the tab row) and 8 ids per tab
LABELS, IDS, EMPTY = 0x080C7EA4, 0x080C7EBC, 0x080CB46C
P_C_TYPE, P_C_THRESH, P_C_RATIO, P_C_MIX = 0x19D, 0x1A1, 0x1A2, 0x19E
P_C_ATTACK, P_C_REL, P_C_GAIN = 0x1A3, 0x1A5, 0x1A6
P_D_TYPE, P_D_DRIVE, P_D_TONE, P_D_MIX, P_D_LEVEL = 0x1A7, 0x1A8, 0x1A9, 0x1AB, 0x1AC
# shown 2 x 2, column-wise: [top left, bottom left, top right, bottom right]
TABS = [
    ("tab_comp", [P_C_TYPE, P_C_THRESH, P_C_RATIO, P_C_MIX]),
    ("tab_comp2", [P_C_ATTACK, P_C_REL, P_C_GAIN, P_D_LEVEL]),   # "More": master_tools_page fills it by Page
    ("tab_dist", [P_D_TYPE, P_D_DRIVE, P_D_TONE, P_D_MIX]),
]

PATCHES = (
    [(at, bl(at, stock), bl(at, sym[name])) for at, stock, name in HOOKS]
    + [(at, bytes.fromhex(old), bl(at, sym[name])) for at, old, name in THUNKS]
    + [(LABELS + 4 * (3 + k), struct.pack("<I", EMPTY), struct.pack("<I", sym[name])) for k, (name, _) in enumerate(TABS)]
    + [(LABELS + 4 * 2, struct.pack("<I", 0x080C7BC4), struct.pack("<I", sym["tab_midi"]))]   # "MIDIIn" -> "MIDI"
    + [(IDS + 16 * (3 + k), bytes(16), struct.pack("<8H", *ids, 0, 0, 0, 0)) for k, (_, ids) in enumerate(TABS)]
)
