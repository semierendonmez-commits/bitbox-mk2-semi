"""bitbox MK2 2.3.4 GEN generative sequencer (src/gen.c; its settings and pages in src/master.c): more parameter ids and
a bigger settings store.

- The parameter table (0x0806a300: 0x1d1 entries of 28 bytes, ids 0..0x1d0, all used by stock and this project) is
  made 0x2fd entries (ids up to 0x2fc): its allocation and clear (0x0806a30a, 0x0806a31c), the lookup by xml name
  (0x0806c180) and the eight bound checks (register 0x0806a24e, getters 0x0806c004 .. 0x0806c120).
- The settings store (init 0x08089cbc: capacity 0x40, 59 entries in use) gets capacity 0x80 for GEN's 41 settings.
- Learn: the grid's note-on calls (0x08094e28, six sites) go through gen_grid_note, which teaches the Learn lanes.
- The pads' own generator (sP): their page 3 (Granular's "Gran", empty for the other pad modes) becomes "Gen" with
  Gen / Hits / Steps / Rotate / Rate / Prob / Var / Vel (a 2 x 2 x 2 dial grid, filled column by column); Cond, Proc
  and Resp Pad at the end of the Conf list (Granular pads: all eleven there). The tab bars' "Gran" labels go through
  gen_tab_label (0x08092440), so a pad page shows "Gen" or "Gran" by the pad's mode (gen_page from clip_page).
  The tab bars (built at 0x08092414..0x08092466: bar 0 Sample / Clip / Slicer without tab 3, bar 1 Granular with all
  five, bar 2 Multisample without Pos and tab 3) keep tab 3: `subs r3, r5, #4` -> `movs r3, #1` (0x0809244a: bar 0
  adds it) and `cmpne r5, #4` -> `cmpne r5, #2` (0x08092454: bar 2 adds it); the bars' tab counts (laid out over the
  width) 4 -> 5 (0x08092700, bar 0) and 3 -> 4 (0x080923e2, bar 2).
Needs `master`, `sound` (the pad records' capacity, the sends on Multisample's Conf list) and the cave."""
import os
import struct

import pages

from thumb import ROOT, bl, symbols

sym = symbols(os.path.join(ROOT, "out", "cave.elf"))
GRID_NOTE = 0x08094E28
GRID_SITES = (0x08078D64, 0x0807B1D4, 0x0807B9E2, 0x0807E852, 0x0807F722, 0x08080A8A)
EXCLUSIVE = {GRID_NOTE}
HOOKS = [(a, GRID_NOTE, "gen_grid_note") for a in GRID_SITES] + [(0x08092440, 0x0805CE4C, "gen_tab_label")]

STOCK_IMG = open(os.path.join(ROOT, "firmware", "BITBOXMK2.bin"), "rb").read()
BASE = 0x08040000
PAGE = 36 * 2
PG_ALGO, PG_STEPS, PG_HITS, PG_ROT, PG_RATE, PG_VAR, PG_PROB, PG_VEL, PG_COND, PG_PROC, PG_RESP = range(0x20B, 0x216)
GEN_PAGE = [PG_ALGO, PG_HITS, PG_STEPS, PG_ROT, PG_RATE, PG_PROB, PG_VAR, PG_VEL]   # column by column: Gen | Steps ...
GEN_CONF = [PG_COND, PG_PROC, PG_RESP]
GEN_ALL = [PG_ALGO, PG_STEPS, PG_HITS, PG_ROT, PG_RATE, PG_VAR, PG_PROB, PG_VEL, PG_COND, PG_PROC, PG_RESP]
TABLES = {"clip_a": 0x080C7BD4, "clip_b": 0x080C7D3C, "gran_a": 0x080C7F3C, "gran_b": 0x080C80A4,
          "sample_a": 0x080C84F4, "sample_b": 0x080C865C, "multi": 0x080C87C4, "slicer_a": 0x080C8A04,
          "slicer_b": 0x080C8B6C}


def conf_patch(name, table):
    at = table + 4 * PAGE
    ids = list(struct.unpack_from("<36H", STOCK_IMG, at - BASE))
    for pa, old, new in pages.PATCHES:                   # pages.py rewrites some Conf lists (+ Retrig)
        if pa == at:
            ids[:len(new) // 2] = struct.unpack(f"<{len(new) // 2}H", new)
    n = ids.index(0) + (3 if name == "multi" else 0)    # Multisample: after FX1 / FX2 / FX3 Send (sound.py)
    new = GEN_ALL if name.startswith("gran") else GEN_CONF
    return (at + 2 * n, bytes(2 * len(new)), struct.pack(f"<{len(new)}H", *new))


def gen_page_patch(table):
    at = table + 3 * PAGE
    return (at, bytes(2 * len(GEN_PAGE)), struct.pack(f"<{len(GEN_PAGE)}H", *GEN_PAGE))

def h(s):
    return bytes.fromhex(s)

BOUND_R3 = (h("b3f5e87f"), h("b3f53f7f"))           # cmp.w r3, #0x1d0 -> #0x2fc
BOUND_R0 = (h("b0f5e87f"), h("b0f53f7f"))           # cmp.w r0, #0x1d0 -> #0x2fc
SIZE = {0: (h("43f2dc20"), h("45f2ac30")), 2: (h("43f2dc22"), h("45f2ac32")), 3: (h("43f2dc23"), h("45f2ac33"))}

PATCHES = [(0x0806A24E, *BOUND_R3)] \
    + [(a, *BOUND_R0) for a in (0x0806C004, 0x0806C030, 0x0806C05C, 0x0806C088, 0x0806C0B4, 0x0806C0E8, 0x0806C120)] \
    + [(0x0806A30A, *SIZE[0]), (0x0806A31C, *SIZE[2]), (0x0806C180, *SIZE[3])] \
    + [(0x08089CC0, h("4021"), h("8021"))] \
    + [(0x0809244A, h("2b1f"), h("0123")), (0x08092454, h("042d"), h("022d"))] \
    + [(0x08092700, h("0423"), h("0523")), (0x080923E2, h("0323"), h("0423"))] \
    + [(a, bl(a, stock), bl(a, sym[name])) for a, stock, name in HOOKS] \
    + [conf_patch(n, t) for n, t in TABLES.items()] \
    + [gen_page_patch(t) for n, t in TABLES.items() if not n.startswith("gran")]
