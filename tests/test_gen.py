"""Unicorn tests for the GEN generative sequencer (src/gen.c, patches/gen.py): the bigger parameter table and settings
store, registrations, the pads' Gen page / Conf lists / tab bars, and gen_tick on a fake engine (song position at
+0x30, the note queue 0x080963cc stubbed and recorded) with the generators in the 16 pad records (sP): every
algorithm and its Var, the step conditions, Prob, Epoch, Run, jumps, two pads at once.

  python3 -I tests/test_gen.py     (PK_BUILD=...+gen+cave)
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_src = open(os.path.join(HERE, "test_fidelity.py")).read()
exec(compile(_src[:_src.index("\ne = Emu()\n")], "test_fidelity.py (header)", "exec"))   # Emu, check, IMG, SYM, ...

from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP  # noqa: E402

if "gen_tick" not in SYM:
    print("no gen in this build")
    sys.exit(0)

e = Emu()
APP = 0x24015170
SET = APP + 0x7ED0
BASE = 0x08040000
STOCK = open(os.path.join(ROOT, "firmware", "BITBOXMK2.bin"), "rb").read()

# --- the patches: parameter table 0x2fd entries, settings store capacity 0x80
check("parameter table: size 0x53ac (0x2fd x 28) at its alloc / clear / xml lookup, bound 0x2fc at all eight checks",
      all(IMG[a - BASE:a - BASE + 4] in (bytes.fromhex(x) for x in ("45f2ac30", "45f2ac32", "45f2ac33"))
          for a in (0x0806A30A, 0x0806A31C, 0x0806C180))
      and all(IMG[a - BASE:a - BASE + 4] in (bytes.fromhex("b0f53f7f"), bytes.fromhex("b3f53f7f"))
              for a in (0x0806A24E, 0x0806C004, 0x0806C030, 0x0806C05C, 0x0806C088, 0x0806C0B4, 0x0806C0E8, 0x0806C120))
      and IMG[0x08089CC0 - BASE:0x08089CC2 - BASE] == bytes.fromhex("8021"))
check("stock image had 0x1d0 / 0x32dc / 0x40 there",
      STOCK[0x0806C004 - BASE:0x0806C008 - BASE] == bytes.fromhex("b0f5e87f")
      and STOCK[0x0806A30A - BASE:0x0806A30E - BASE] == bytes.fromhex("43f2dc20"))

# --- the real parameter table init (with our registrations through master_register) and the getters
e.mu.mem_write(0x24014D18, b"\0" * 4)
e.call(0x0806A300, 0x24030000)
table = e.r32(0x24014D18)
def getter_label(pid):
    return e.call(0x0806C000, pid)                         # +4: label pointer (0 when unregistered / out of range)
lab = [e.cstr(getter_label(p)) if getter_label(p) else "" for p in (0x1D1, 0x20B, 0x210, 0x212, 0x215)]
check("parameter table: ids 0x1d1.. registered and readable through the patched getters (Run, the pads' Gen, Var, "
      "Gen Vel, Resp Pad)", lab == ["Run:", "Gen:", "Var:", "Gen Vel:", "Resp Pad:"], lab)
buf = e.new(32)
e.mu.mem_write(buf, b"semigvel\0")
check("lookup by xml name finds the pads' Gen Vel (0x212) past the old table end", e.call(0x0806C148, buf) == 0x212,
      hex(e.call(0x0806C148, buf)))

# --- settings store: the real init (0x08089cbc) with master's hook: GEN's 12 global settings
e.stub(0x08045012, lambda uc: None)
e.call(0x08089CBC, SET)
cnt = e.r16(SET + 0xA)
cap = e.r16(SET + 0xC)


def sget(pid):
    return struct.unpack("<i", struct.pack("<I", e.call(SYM["fast_get"], SET, pid)))[0]


def sset(pid, v):
    e.call(0x080893B2, SET, pid, v & 0xFFFFFFFF)


check("settings store: capacity 0x80, 73 entries; GEN's globals (Run Off, Cut On, Scale Pent Min, Range 12, Mutate 25); "
      "no lane settings left", cap == 0x80 and cnt == 73 and sget(0x1D1) == 0 and sget(0x1FA) == 1
      and sget(0x1FB) == 9 and sget(0x1FC) == 12 and sget(0x1D5) == 25, (cap, cnt))

# --- the pads' records (app +0x1840 + row x 0xf0 + col x 0x30): the real record init (type 3, every parameter)
def rec(lane):
    return APP + 0x1840 + (lane // 4) * 0xF0 + (lane % 4) * 0x30


typ3 = e.new(4)
e.mu.mem_write(typ3, b"\x03")
for l in range(16):
    e.call(0x08089E7C, rec(l), typ3)
ALGO, STEPS, HITS, ROT, RATE, VAR, PROB, VEL, COND, PROC, RESP = range(0x20B, 0x216)


def pset(lane, pid, v):
    e.call(0x080893B2, rec(lane), pid, v & 0xFFFFFFFF)


def pget(lane, pid):
    return e.call(SYM["fast_get"], rec(lane), pid)


check("pad records: the generator's defaults (Gen Off, Steps 16, Hits 4, Rotate 0, 1/16, Var 50, Prob 100, Vel 100, "
      "Always, Classic, Resp Same) in all 16", all([pget(l, p) for p in range(ALGO, RESP + 1)]
                                                    == [0, 16, 4, 0, 1, 50, 100, 100, 0, 1, 0] for l in range(16)))

# --- the pads' pages: page 3 = Gen (dial grid, column by column), Cond / Proc / Resp at the end of Conf; Granular: all
#     eleven at the end of its Conf; the tab bars keep tab 3
TABLES = {"clip_a": 0x080C7BD4, "clip_b": 0x080C7D3C, "gran_a": 0x080C7F3C, "gran_b": 0x080C80A4,
          "sample_a": 0x080C84F4, "sample_b": 0x080C865C, "multi": 0x080C87C4, "slicer_a": 0x080C8A04,
          "slicer_b": 0x080C8B6C}
def page(t, n):
    ids = struct.unpack_from("<36H", IMG, t + n * 72 - BASE)
    return list(ids[:ids.index(0)] if 0 in ids else ids)
gen_pages = {n: page(t, 3) for n, t in TABLES.items() if not n.startswith("gran")}
confs = {n: page(t, 4) for n, t in TABLES.items()}
check("Gen page (page 3) of Sample / Clip / Slicer / Multisample: Gen, Hits | Steps, Rotate ; Rate, Prob | Var, Vel",
      all(v == [ALGO, HITS, STEPS, ROT, RATE, PROB, VAR, VEL] for v in gen_pages.values()), gen_pages)
check("Conf lists end with Cond, Proc, Resp Pad (Granular: all eleven; Multisample after the sends); Granular keeps Gran",
      all(c[-3:] == [COND, PROC, RESP] for n, c in confs.items() if not n.startswith("gran"))
      and all(c[-11:] == list(range(ALGO, RESP + 1)) for n, c in confs.items() if n.startswith("gran"))
      and confs["multi"][-6:-3] == [0xBB, 0xBC, 0x1C8] and page(TABLES["gran_a"], 3)[0] == 0x15C, confs)
check("tab bars: tab 3 kept in bar 0 (movs r3, #1) and bar 2 (cmpne r5, #2), 5 / 4 tabs laid out",
      IMG[0x0809244A - BASE:0x0809244C - BASE] == bytes.fromhex("0123")
      and IMG[0x08092454 - BASE:0x08092456 - BASE] == bytes.fromhex("022d")
      and IMG[0x08092700 - BASE:0x08092702 - BASE] == bytes.fromhex("0523")
      and IMG[0x080923E2 - BASE:0x080923E4 - BASE] == bytes.fromhex("0423"))
texts = []
e.stub(0x0805CE4C, lambda uc: texts.append((uc.reg_read(UC_ARM_REG_R0), e.cstr(uc.reg_read(UC_ARM_REG_R1)))))
e.call(SYM["gen_tab_label"], 0x30F00000, 0x080C97AC)          # "Pos": not noted
e.call(SYM["gen_tab_label"], 0x30F00100, 0x080C97B0)          # "Gran" of two bars
e.call(SYM["gen_tab_label"], 0x30F00200, 0x080C97B0)
texts.clear()
pset(0, 0x6D, 0)
e.call(SYM["gen_page"], rec(0), APP)                         # a Sample pad's page
t_sample = list(texts)
texts.clear()
e.call(SYM["gen_page"], rec(0), APP)                         # again: nothing to do
t_again = list(texts)
pset(1, 0x6D, 3)
e.call(SYM["gen_page"], rec(1), APP)                         # a Granular pad
t_gran = list(texts)
texts.clear()
e.call(SYM["gen_page"], APP + 0x7F38, APP)                   # not a pad
check("tab 3: \"Gen\" on a Sample pad's page, \"Gran\" back on a Granular pad's, only the Gran widgets, once",
      t_sample == [(0x30F00100, "Gen"), (0x30F00200, "Gen")] and t_again == []
      and t_gran == [(0x30F00100, "Gran"), (0x30F00200, "Gran")] and texts == [], (t_sample, t_again, t_gran, texts))
pset(1, 0x6D, 0)

# --- gen_tick on a fake engine
eng = e.new(0x10000)
e.w32(0x24023CB8 + 0xA204, eng)
notes = []
params = []
now = [0]


def post(uc):
    m = uc.reg_read(UC_ARM_REG_R1)
    typ = e.mu.mem_read(m, 1)[0]
    if typ in (0x42, 0x43):
        notes.append((now[0], typ, e.r32(m + 8), e.r16(m + 0x16), e.r32(m + 0xC)))
    else:
        params.append((now[0], typ, e.r32(m + 8), e.r32(m + 0xC), e.r32(m + 0x10)))


e.stub(0x080963CC, post)
e.stub(0x08043BD0, lambda uc: uc.reg_write(UC_ARM_REG_R0, 8 << 20))   # heap free: plenty
e.call(SYM["gen_init"])
check("gen_init: the 16 Learn tables from the heap (pointer + magic at 0x2001efe0)",
      e.r32(0x2001EFE0) >= 0x24040000 and e.r32(0x2001EFE4) == 0x4C524E32, hex(e.r32(0x2001EFE0)))


def run(t0, t1, dt=20):
    t = t0
    while t < t1:
        now[0] = t
        e.w32(eng + 0x30, t)
        e.call(SYM["gen_tick"])
        t += dt


def ons(cell=None):
    return [(t, c, v) for t, ty, c, v, nt in notes if ty == 0x43 and (cell is None or c == cell)]


BAR = 3840
DEFAULTS = {STEPS: 16, HITS: 4, ROT: 0, RATE: 1, VAR: 50, PROB: 100, VEL: 100, COND: 0, PROC: 1, RESP: 0}


def reset():
    for l in range(16):
        pset(l, ALGO, 0)
        for p, v in DEFAULTS.items():
            pset(l, p, v)
    sset(0x1D2, 0)
    sset(0x1D3, 0)
    sset(0x1D1, 0)
    run(0, 40)                                                # Run Off: state reset
    notes.clear()


def lane(l, **kw):
    for k, v in kw.items():
        pset(l, globals()[k.upper()], v)


def steps_of(lst, st=240, origin=0):
    return [(t - origin) // st for t, c, v in lst]


# 1. Euclid 16 / 4 on pad 1 (bottom left: row 0, col 0), 1/16: one hit per beat, on the beat, note off half a step on
reset()
lane(0, algo=1)
sset(0x1D1, 1)
run(0, 2 * BAR)
o = ons()
offs = [(t, c) for t, ty, c, v, nt in notes if ty == 0x42]
check("Euclid 16/4: 8 hits in 2 bars on the beats (within one block), cell 0x000 (pad 1), vel 100 (+0x16); offs 120 later",
      [t // 960 for t, c, v in o] == list(range(8)) and all(t % 960 < 20 for t, c, v in o)
      and all(c == 0 and v == 100 for t, c, v in o) and len(offs) >= 7 and all(t % 960 >= 120 for t, c in offs[:7]),
      (o, offs[:3]))

# 2. E(3,8) with 1:2 on pad 13 (top left: row 3, col 0): cycles of 8 sixteenths, plays the 1st of every 2
reset()
lane(12, algo=1, steps=8, hits=3, rot=0, cond=1)
sset(0x1D1, 1)
run(0, 2 * BAR)
s = steps_of(ons(0x300))
check("Cond 1:2: E(3,8) at sixteenths 0 3 6 of cycles 0 and 2 only (cell 0x300 = pad 13)", s == [0, 3, 6, 16, 19, 22], s)
pset(12, COND, 2)                                            # 2:2
notes.clear()
run(2 * BAR, 4 * BAR)
s = steps_of(ons(0x300), origin=2 * BAR)
check("Cond 2:2: cycles 1 and 3 of the window", s == [8, 11, 14, 24, 27, 30], s)
pset(12, COND, 8)                                            # 3:4
notes.clear()
run(4 * BAR, 6 * BAR)
s = steps_of(ons(0x300), origin=4 * BAR)                    # cycles 8..11: 3:4 -> cycle 10 (absolute 10 % 4 == 2)
check("Cond 3:4: only the 3rd of every 4 cycles", s == [16, 19, 22], s)

# 3. Fill / !Fill, First / !First, Prob
pset(12, COND, 10)
notes.clear()
run(6 * BAR, 7 * BAR)
nofill = len(ons(0x300))
sset(0x1D2, 1)
run(7 * BAR, 8 * BAR)
fill = len(ons(0x300))
sset(0x1D2, 0)
pset(12, COND, 11)
notes.clear()
run(8 * BAR, 9 * BAR)
nf = len(ons(0x300))
check("Fill: silent without Fill, plays with Fill On; !Fill plays without", nofill == 0 and fill == 6 and nf == 6,
      (nofill, fill, nf))
reset()
lane(12, algo=1, steps=8, hits=3, rot=0, cond=12)
sset(0x1D1, 1)
run(BAR, 3 * BAR)
s = steps_of(ons(0x300), origin=BAR)
check("First: only the first cycle after Run", s == [0, 3, 6], s)
sset(0x1D1, 0)
run(3 * BAR, 3 * BAR + 40)
pset(12, COND, 13)
notes.clear()
sset(0x1D1, 1)
run(4 * BAR, 6 * BAR)
s = steps_of(ons(0x300), origin=4 * BAR)
check("!First: every cycle but the first", s == [8, 11, 14, 16, 19, 22, 24, 27, 30], s)
reset()
lane(0, algo=1, steps=16, hits=16, prob=50)
sset(0x1D1, 1)
run(0, 8 * BAR)
n50 = len(ons())
notes.clear()
run(8 * BAR, 16 * BAR)
n50b = len(ons())
check("Prob 50 %: about half of 128 sixteenths each time, different steps", 44 <= n50 <= 84 and 44 <= n50b <= 84,
      (n50, n50b))

# 4. the algorithms (pad 1, 16 steps, 1/16): per cycle step lists
def cycles(algo, hits=4, rot=0, ncyc=8, steps=16, var=50, l=0, mutate=None, proc=1):
    reset()
    lane(l, algo=algo, steps=steps, hits=hits, rot=rot, var=var, proc=proc)
    if mutate is not None:
        sset(0x1D5, mutate)
    sset(0x1D1, 1)
    run(0, ncyc * steps * 240)
    out = [[] for _ in range(ncyc)]
    for t, c, v in ons():
        st = t // 240
        if st // steps < ncyc:
            out[st // steps].append((st % steps, v))
    return out


br = cycles(2, hits=6, proc=0)                               # Proc Basic
check("Breakcut Basic: every cycle starts with a hit, cuts of 1..4 steps (or a closing roll), cuts change between cycles",
      all(c and c[0][0] == 0 for c in br) and len({tuple(x for x, _ in c) for c in br}) >= 3
      and all(all(b - a <= 4 for (a, _), (b, _) in zip(c, c[1:])) for c in br),
      [[x for x, _ in c] for c in br])
cr = cycles(3, hits=4)
pat = [tuple(x for x, _ in c) for c in cr]
check("Call+Resp: cycles 0, 1, 2 the call (E(4,16)), cycle 3 the response (different), then again",
      pat[0] == pat[1] == pat[2] == (0, 4, 8, 12) and pat[3] != pat[0] and pat[4] == pat[0] and pat[7] != pat[0], pat)
r0 = len(cycles(3, hits=4, var=0, ncyc=4)[3])
r100 = len(cycles(3, hits=4, var=100, ncyc=4)[3])
check("Call+Resp Var: the response adds nothing at 0, more at 100", r0 <= 4 < r100, (r0, r100))
ev = cycles(4, hits=6, var=60, ncyc=10)
pe = [tuple(x for x, _ in c) for c in ev]
check("Evolve: starts as Euclid, mutates (Var 60), stays near Hits",
      len(set(pe)) >= 3 and all(3 <= len(p) <= 10 for p in pe), pe)
ev0 = [tuple(x for x, _ in c) for c in cycles(4, hits=6, var=0, ncyc=6)]
check("Evolve Var 0: no mutation", len(set(ev0)) == 1, ev0)
d_lo = sum(len(c) for c in cycles(5, hits=4, rot=0, ncyc=2, steps=32, var=0))
d_hi = sum(len(c) for c in cycles(5, hits=24, rot=0, ncyc=2, steps=32, var=0))
dk = cycles(5, hits=8, rot=0, ncyc=1, steps=32, var=0)[0]
check("Drummap (pad 1: the kick map): more hits with more density; hits step 0, loud steps louder",
      0 < d_lo < d_hi and dk and dk[0][0] == 0 and dk[0][1] >= max(v for _, v in dk), (d_lo, d_hi, dk))
d0 = [x for x, _ in cycles(5, hits=10, ncyc=1, var=0)[0]]
d100 = [x for x, _ in cycles(5, hits=10, ncyc=1, var=100)[0]]
dsn = [x for x, _ in cycles(5, hits=10, ncyc=1, var=0, l=1)[0]]
check("Drummap Y = Var: 0 vs 100 different patterns, both on step 0; pad 2 plays the snare map (not on step 0)",
      d0 != d100 and d0[0] == 0 and d100[0] == 0 and dsn and dsn[0] != 0, (d0, d100, dsn))
rn = cycles(6, hits=8, ncyc=8, var=0)
tot = sum(len(c) for c in rn)
check("Random: about Hits per cycle (8 of 16), varying; Var 0: one velocity",
      40 <= tot <= 88 and len({tuple(x for x, _ in c) for c in rn}) > 4 and {v for c in rn for _, v in c} == {100}, tot)
rv = cycles(6, hits=8, ncyc=4, var=100)
check("Random Var 100: a velocity spread", len({v for c in rv for _, v in c}) >= 5, {v for c in rv for _, v in c})
eg = cycles(1, hits=4, ncyc=4, var=100)
check("Euclid Var 100: ghost notes (soft) between the four main hits",
      all(sum(1 for x, v in c if x % 4 == 0 and v == 100) == 4 for c in eg)
      and sum(1 for c in eg for x, v in c if x % 4 and v < 60) >= 4, eg)

# 5. Epoch 4 bars: the first bar at half density, the last bar a fill
reset()
lane(0, algo=1, steps=16, hits=8)
lane(12, algo=1, steps=16, hits=4, cond=10)
sset(0x1D3, 1)
sset(0x1D1, 1)
run(0, 4 * BAR)
per_bar = [len([1 for t, c, v in ons(0) if t // BAR == b]) for b in range(4)]
fills = [t // BAR for t, c, v in ons(0x300)]
check("Epoch 4 bars: Euclid 8 -> 4, 6, 8, 8 hits per bar; the Fill pad only in bar 4",
      per_bar == [4, 6, 8, 8] and fills and set(fills) == {3}, (per_bar, fills))

# 6. Run Off: note off for a sounding note, nothing more; a jump back: no burst of old steps
reset()
lane(0, algo=1)
sset(0x1D1, 1)
run(0, 980)
sset(0x1D1, 0)
run(980, 3 * BAR)
check("Run Off: the pending note off is sent, then nothing", [ty for t, ty, c, v, nt in notes][-1] == 0x42
      and not [1 for t, ty, c, v, nt in notes if ty == 0x43 and t > 980], notes[-3:])
reset()
lane(0, algo=1)
sset(0x1D1, 1)
run(10 * BAR, 10 * BAR + 1000)
notes.clear()
run(0, 100)                                                 # song restarts
o = ons()
check("song position jump back: restarts on the next step, no burst", len(o) == 1 and o[0][0] < 40, o)

# 7. every pad its own generator: pad 7 (row 1, col 2) and pad 16 (row 3, col 3) at once, own steps and rates
reset()
lane(6, algo=1, steps=16, hits=4)
lane(15, algo=1, steps=8, hits=8, rate=2)                    # every eighth
sset(0x1D1, 1)
run(0, BAR)
c7 = steps_of(ons(0x102))
c16 = steps_of(ons(0x303))
check("Pad 7 (0x102) and pad 16 (0x303) at once: E(4,16) and every 1/8, nothing else",
      c7 == [0, 4, 8, 12] and c16 == [0, 2, 4, 6, 8, 10, 12, 14] and len(ons()) == 12, (c7, c16))

# 8. Call+Resp melody: Range 12 in Pent Min: calls the same melody (notes 48 + 0..12, pent min steps), the response
#    other notes, ending on the pad's pitch (note 0); Range 0: no pitch
def call_notes(rng, scale=9):
    reset()
    lane(0, algo=3, steps=16, hits=4, rot=0)
    sset(0x1FC, rng)
    sset(0x1FB, scale)
    sset(0x1D1, 1)
    run(0, 4 * 16 * 240)
    cyc = [[] for _ in range(4)]
    for t, ty, c, v, nt in notes:
        if ty == 0x43:
            cyc[t // (16 * 240)].append(nt)
    return cyc
cn = call_notes(12)
semis = {n - 48 for c in cn for n in c if n}
check("Call+Resp Range 12, Pent Min: 3 equal pitched calls, a different response ending on the pad's pitch, "
      "all notes in the scale within an octave",
      cn[0] == cn[1] == cn[2] and len(set(cn[0])) >= 2 and cn[3] != cn[0] and cn[3][-1] == 0
      and semis <= {0, 3, 5, 7, 10, 12}, cn)
c0 = call_notes(0)
check("Call+Resp Range 0: unpitched (note 0, the pad's own pitch)", all(n == 0 for c in c0 for n in c), c0)
sset(0x1FC, 12)

# 9. Breakcut Cut on a Sample pad: Start (param 0x71, frames) before each cut, the cycle's first cut from the top;
#    Cut Off / Run Off: the pad's Start back
pad = e.new(0x200)
e.w32(pad, 0x080DF054)
e.w32(pad + 0x18, 0x000)
e.w32(pad + 0x100, 160000)
e.w32(pad + 0xF8, 1234)
e.w32(pad + 0x10, 0)
e.w32(eng + 0xFAF8 + 8, pad)
reset()
params.clear()
lane(0, algo=2, steps=16, hits=6, var=100, proc=0)
sset(0x1D1, 1)
run(0, 8 * 16 * 240)
hits = [(t, nt) for t, ty, c, v, nt in notes if ty == 0x43]
starts = [(t, val) for t, ty, c, prm, val in params if prm == 0x71]
okpair = all(any(st == ht for st, _ in starts) for ht, _ in hits)
firsts = [(t // 3840, val) for t, val in starts if t % (16 * 240) < 20]
moved = sum(1 for (t, val) in starts if val // 5000 != ((t // 3840) * 16 + (t // 240) % 16) % 32)
check("Breakcut + Cut: a Start (frames) with every hit; the 160000-frame sample is 2 bars at 120 BPM (32 steps of 5000 "
      "frames): each cycle starts at its bar (0, 80000, 0, ...), some cuts moved (Var 100)",
      hits and okpair and all(v % 5000 == 0 for _, v in starts) and all(v == (c % 2) * 80000 for c, v in firsts)
      and moved >= 4, (len(hits), len(starts), firsts[:4], moved))
params.clear()
sset(0x1D1, 0)
run(8 * 16 * 240, 8 * 16 * 240 + 40)
check("Run Off: the pad's own Start (1234 frames) and Poly Mode back (Mono while cutting)",
      [(prm, val) for t, ty, c, prm, val in params] == [(0x71, 1234), (0x82, 0)], params)
sset(0x1D1, 1)
sset(0x1FA, 0)
params.clear()
run(9 * 16 * 240, 11 * 16 * 240)
check("Cut Off: Breakcut only triggers (no Start)", not params, params[:3])
sset(0x1FA, 1)
e.w32(eng + 0xFAF8 + 8, 0)

# 10. Call+Resp with a Resp Pad: the calls on the pad, the response on pad 6 (row 1, col 1)
reset()
lane(0, algo=3, steps=16, hits=4, resp=6)
sset(0x1D1, 1)
run(0, 4 * 16 * 240)
by_cyc = [sorted({c for t, c, v in ons() if t // (16 * 240) == k}) for k in range(4)]
check("Call+Resp Resp Pad 6: cycles 0..2 on pad 1 (0x000), the response (cycle 3) on pad 6 (0x101)",
      by_cyc[:3] == [[0], [0], [0]] and by_cyc[3] == [0x101], by_cyc)

# 11. Breakcut procedures: each gives hits every cycle with segments inside the cycle; rolls make extra hits inside
#     a step (sub hits between steps); Rolls Off removes them; Recut Locked repeats the same cuts
def proc_run(proc, rolls=1, recut=0, ncyc=4, var=50):
    reset()
    lane(0, algo=2, steps=16, hits=6, proc=proc, var=var)
    sset(0x1FE, rolls)
    sset(0x1FF, recut)
    sset(0x1D1, 1)
    run(0, ncyc * 16 * 240, dt=10)
    return [t for t, c, v in ons()]
res = {}
for proc in range(1, 10):
    hs = proc_run(proc)
    sub = sum(1 for t in hs if t % 240 >= 20)
    res[proc] = (len(hs), sub)
names = ["", "Classic", "ChooseCut", "Jungle", "WarpCut", "SQPusher", "CageCut", "Timeline", "Permute", "ThrashCut"]
check("Breakcut procs Classic .. ThrashCut: every proc plays (>= 8 hits in 4 cycles); the roll procs make hits "
      "inside steps", all(res[p][0] >= 8 for p in res) and sum(res[p][1] for p in res) > 0,
      {names[p]: res[p] for p in res})
warp = proc_run(4, rolls=1, var=100)
warp_off = proc_run(4, rolls=0, var=100)
check("WarpCut, Var 100 (all rolls): hits inside steps; Rolls Off: none inside steps, fewer hits",
      sum(1 for t in warp if t % 240 >= 20) > 4 and sum(1 for t in warp_off if t % 240 >= 20) == 0
      and len(warp_off) < len(warp), (len(warp), len(warp_off)))
locked = proc_run(1, recut=4, ncyc=4)
cyc = [tuple(t % 3840 for t in locked if t // 3840 == k) for k in range(4)]
check("Recut Locked: the same cuts every cycle", cyc[0] == cyc[1] == cyc[2] == cyc[3] and cyc[0], cyc)
sset(0x1FF, 0)
sset(0x1FE, 1)

# 12. Acid: pitched notes in the scale, Hits = density, Var: accents (two velocities); Turing Var 100: a loop of Steps
reset()
lane(0, algo=7, steps=16, hits=24, vel=127)
sset(0x1D5, 0)
sset(0x1D1, 1)
run(0, 2 * 3840)
an = [(t, v, nt) for t, ty, c, v, nt in notes if ty == 0x43]
semis = {nt - 48 for t, v, nt in an if nt} | {0}
check("Acid (Hits 24, Pent Min, Mutate 0): many notes, pitches in the scale (within Range +- octave), two velocities "
      "(accents), the two cycles the same", len(an) >= 12 and semis <= {s + o for s in (0, 3, 5, 7, 10) for o in (-12, 0, 12)}
      and len({v for t, v, nt in an}) == 2
      and [(t % 3840, nt) for t, v, nt in an if t < 3840] == [(t % 3840, nt) for t, v, nt in an if t >= 3840],
      (len(an), sorted(semis), {v for t, v, nt in an}))
sset(0x1D5, 25)
reset()
lane(0, algo=8, steps=8, hits=5, var=100)
sset(0x1D1, 1)
run(0, 4 * 8 * 240)
tn = [(t // 240 % 8, nt) for t, ty, c, v, nt in notes if ty == 0x43 and t >= 8 * 240]
check("Turing Var 100 (locked): a loop of 8 steps (hits and pitches repeat every 8 steps)",
      tn and tn[:len(tn) // 3] == tn[len(tn) // 3:2 * len(tn) // 3], tn)

# 13. Learn: hits taught on the grid (gen_grid_note) every 4 steps on pad 1 -> its Learn generator plays gaps of 4
reset()
lane(0, algo=9, steps=16)
sset(0x204, 1)
e.stub(0x08094E28, lambda uc: None)
for k in range(12):
    e.w32(eng + 0x30, k * 4 * 240)
    e.call(SYM["gen_grid_note"], 0x24023CB8, 0x000, 0, 0, stack=(0,))
sset(0x1D1, 1)
run(4 * 3840, 6 * 3840)
ls = sorted({(t - 4 * 3840) // 240 % 16 for t, c, v in ons()})
check("Learn: taught every 4 steps on pad 1 -> pad 1 plays steps 0 4 8 12", ls == [0, 4, 8, 12], ls)
sset(0x204, 0)

# 14. Turn every 8 bars: the last beat of bar 8 turns (one of roll / stutter / drop / fill), the rest as before
reset()
lane(0, algo=1, steps=16, hits=4)
sset(0x205, 1)
sset(0x1D1, 1)
run(0, 16 * 3840, dt=10)
inside = [t for t, c, v in ons() if t % (8 * 3840) >= 8 * 3840 - 960]
outside = [t for t, c, v in ons() if t % (8 * 3840) < 8 * 3840 - 960]
plain = [t for t in outside if t % 960 == 0 or t % 960 < 20]
check("Turn 8 bars: outside the last beat the plain Euclid (one hit a beat); in it a change (not exactly one hit)",
      len(plain) == len(outside) and len(outside) == 2 * 31 and len(inside) != 2, (len(outside), len(inside)))
sset(0x205, 0)

# 15. the saver's view of the pads (gen_lane_view): only running generators, their steps, position and hits
reset()
lane(5, algo=1, steps=12, hits=3)
sset(0x1D1, 1)
run(0, 5 * 240 + 40)
nb, pb, hb = e.new(4), e.new(4), e.new(4)
views = [e.call(SYM["gen_lane_view"], l, nb, pb, hb) for l in range(16)]
check("gen_lane_view: pad 6 only; 12 steps, at step 5, hits 0 4 8",
      views == [0] * 5 + [1] + [0] * 10 and e.r32(nb) == 12 and e.r32(pb) == 5 and e.r32(hb) == 0x111,
      (views, e.r32(nb), e.r32(pb), hex(e.r32(hb))))

print(f"\n{'FAILED: ' + str(failures) if failures else 'all passed'}")
sys.exit(1 if failures else 0)
