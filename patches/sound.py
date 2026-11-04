"""bitbox MK2 2.3.4: Drive and Filter 2 (src/filter.c) in every pad record, on the LFO page of every pad mode (MK2
pages are 2 x 2 x 2 dial grids; LFO has 5 of 8 slots used); clip Shift / Play Len / Stretch / Str Mode / Loop Fade
(src/clipshift.c, src/stretch.c) at the end of the clip Pos list; Sync 1/32; CPU load in the status bar (src/load.c).
Registration rides on `interp_register` (patches/fidelity.py), the DSP on filter_process, so this patchset needs
`fidelity` and the cave.

Ported from portkit's micro patches/sound.py: hook sites through re/mk2_map.py (tools/port_mk2.py) and checked
against the MK2 BL targets / bytes; the page data is MK2-specific (micro: 6 pages x 45 ids, MK2: 5 pages x 36 ids).
"""
import os
import struct

from thumb import BASE, ROOT, bl, symbols

STOCK_IMG = open(os.path.join(ROOT, "firmware", "BITBOXMK2.bin"), "rb").read()

sym = symbols(os.path.join(ROOT, "out", "cave.elf"))

P_FILT2, P_RES2, P_DRIVE, P_SHIFT, P_PLAYLEN, P_STRETCH = 0x1C0, 0x1C1, 0x1C2, 0x1C3, 0x1C4, 0x1C6  # 0x1c5 is taken by the firmware (0x0807f7e2)
P_SMODE = 0x1C7
P_LFADE = 0x183
STORE_ADD = 0x080893D6
PAGE_BUILDER = 0x08078EC8

HOOKS = [
    (0x080B5ACE, 0x080B576C, "slicer_start"),          # slice start, Sync path: note the slot's speed (Slice Str)
    # pad record init 0x08089400: the two pad blocks' "add Filter (0xaa)" -> add it, then the new parameters
    (0x080895B0, STORE_ADD, "pad_store_add"),
    (0x080897DA, STORE_ADD, "pad_store_add"),
    # smooth time stretch (src/stretch.c): the clip slot reader's read + resample
    (0x080B6A24, 0x080B66D4, "clip_render"),
    # audio load (src/load.c): the audio task's two per-block calls, both call sites each; status bar right text
    (0x08042354, 0x080952D8, "load_in"),
    (0x08042370, 0x080952D8, "load_in"),
    (0x08042360, 0x08095364, "load_out"),
    (0x0804237C, 0x08095364, "load_out"),
    (0x080714F0, 0x0805FF6E, "load_text"),           # the idle (empty) case
    (0x080714C6, 0x0805FF6E, "load_text_mb"),        # MK2: "%dMB" (free recording memory) + the load
    (0x080714E4, 0x0805FF6E, "load_text_mb"),        # MK2: "FULL" + the load
    # Sync list + "1/32"
    (0x0806AF74, 0x0806A27C, "sync_register"),
    # parameter pages: every caller of the page builder (MK2 has 6, the micro 7)
    (0x08087E26, PAGE_BUILDER, "clip_page"),
    (0x08087E8C, PAGE_BUILDER, "clip_page"),
    (0x080912DE, PAGE_BUILDER, "clip_page"),
    (0x08091EE8, PAGE_BUILDER, "clip_page"),
    (0x08093A02, PAGE_BUILDER, "clip_page"),
    (0x080943AC, PAGE_BUILDER, "clip_page"),
    # clip reader 0x080b66d4 and segment starts 0x080b576c / 0x080b5a34: the stream calls get the pad (r4) and shift
    (0x080B6758, 0x080A58A8, "clip_read1_thunk"),
    (0x080B6848, 0x080A58A8, "clip_read1_thunk"),
    (0x080B67A6, 0x080A5870, "clip_read2_thunk"),
    (0x080B681A, 0x080A5870, "clip_read2_thunk"),
    (0x080B58FA, 0x080A59CC, "clip_seek_thunk"),
    (0x080B5C74, 0x080A59CC, "clip_seek_thunk"),
]

# MK2 pad tables: 5 pages x 36 halfword ids (Main, Pos, LFO, Gran, Conf; tab labels at 0x080c9804).
PAGE = 36 * 2
TABLES = {                                   # name: (address, page 2 (LFO) ids as stock)
    "clip_a": 0x080C7BD4, "clip_b": 0x080C7D3C, "gran_a": 0x080C7F3C, "gran_b": 0x080C80A4,
    "sample_a": 0x080C84F4, "sample_b": 0x080C865C, "multi": 0x080C87C4, "slicer_a": 0x080C8A04,
    "slicer_b": 0x080C8B6C,
}
LFO = [0x08, 0x09, 0x0B, 0x0C, 0x0D]
LFO_MULTI = [0x08, 0x09, 0x0B, 0x0D, 0x0C]
# LFO page, second 2 x 2 group: [LFO 5th, Drive] / [Filter 2, Res 2] - Filter 2 / Res 2 sit like Filter / Res on Main
NEW = [P_DRIVE, P_FILT2, P_RES2]

# play length: the segment-count loads before "next segment < count" in the clip voice 0x080b6bd0 (pad in r6)
THUNKS = [
    (0x080B5AAA, "07f085f8", "slicer_len_thunk"),   # slice voice start, Sync On: duration -> int; Sync Grid
    (0x080B7056, "d6e99012", "clip_segments_thunk"),
    (0x080B7242, "d6e99012", "clip_segments_thunk"),
    # stretch, live at every segment start (0x080b5a34): file position = index x length x speed; last-segment test
    (0x080B5B90, "27ee888a", "clip_pos_thunk"),
    (0x080B5ADC, "d4f84032", "clip_last_thunk"),
]

EXCLUSIVE = {PAGE_BUILDER, 0x080B66D4, 0x080952D8, 0x08095364}

# Pad record capacity 64 -> 80 entries (`movs r1, #0x40` before the store allocation 0x08089356 in the record init
# 0x08089400 and the record reset 0x08089e44): 58 stock + 9 new entries + GEN's 11 (src/gen.c, sP).
CAPACITY = [(0x08089406, "4021", "5021"), (0x08089E50, "4021", "5021")]

# Sync's beats table (0x080c60cc, 7 entries) -> sync_beats (8) in the clip parameter handler's literal
DATA = [(0x080B860C, 0x080C60CC, "sync_beats")]

# Clip Pos page (page 1, a scrolling list): Loop Mode, Beat Count, Quant Size, Sync -> + Shift, Play Len, Stretch,
# Stretch Mode, Loop Fade
CLIP_TABLES = [TABLES["clip_a"], TABLES["clip_b"]]
POS = [0x6F, 0xB4, 0x2A, 0x76]


# FX1 Send / FX2 Send at the end of the Pos list (Multisample: Conf list, it has no Pos page): numeric %, and a MOD
# indicator like Start / Length if the firmware lets them be modulated. Stock shows the sends only as bars on the FX
# screen (forum wish: "mix % per cell"). Pan is already on the Conf list, with modulation.
P_FX1, P_FX2, P_FX3 = 0xBB, 0xBC, 0x1C8
P_SGRID, P_SSTR = 0x1B6, 0x1B7
SLICER_TABLES = [TABLES["slicer_a"], TABLES["slicer_b"]]


def sends_patch(table, page):
    at = table + page * PAGE
    ids = list(struct.unpack_from("<36H", STOCK_IMG, at - BASE))
    n = ids.index(0)
    if table in CLIP_TABLES and page == 1:
        n += 5                                       # after the clip tools (Shift .. Loop Fade), patched below
    if table in SLICER_TABLES and page == 1:         # Sync Grid (slicer.c) right after Sync (0xd5)
        k = ids.index(0xD5) + 1
        tail = ids[k:n]
        return (at + 2 * k, struct.pack(f"<{len(tail) + 5}H", *tail, 0, 0, 0, 0, 0),
                struct.pack(f"<{len(tail) + 5}H", P_SGRID, P_SSTR, *tail, P_FX1, P_FX2, P_FX3))
    return (at + 2 * n, bytes(6), struct.pack("<3H", P_FX1, P_FX2, P_FX3))   # (GEN's go on after them: gen.py)


def lfo_patch(table):
    ids = LFO_MULTI if table == TABLES["multi"] else LFO
    at = table + 2 * PAGE
    return (at, struct.pack(f"<{len(ids) + 3}H", *ids, 0, 0, 0), struct.pack(f"<{len(ids) + 3}H", *ids, *NEW))


PATCHES = (
    [(at, bl(at, stock), bl(at, sym[name])) for at, stock, name in HOOKS]
    + [lfo_patch(t) for t in TABLES.values()]
    + [sends_patch(t, 4 if t == TABLES["multi"] else 1) for t in TABLES.values()]
    + [(at, bytes.fromhex(old), bl(at, sym[name])) for at, old, name in THUNKS]
    + [(at, struct.pack("<I", old), struct.pack("<I", sym[name])) for at, old, name in DATA]
    + [(at, bytes.fromhex(old), bytes.fromhex(new)) for at, old, new in CAPACITY]
    + [(t + PAGE, struct.pack("<9H", *POS, 0, 0, 0, 0, 0),
       struct.pack("<9H", *POS, P_SHIFT, P_PLAYLEN, P_STRETCH, P_SMODE, P_LFADE)) for t in CLIP_TABLES]
)
