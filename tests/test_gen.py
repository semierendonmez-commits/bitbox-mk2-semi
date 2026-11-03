"""Unicorn tests for the GEN generative sequencer (src/gen.c, patches/gen.py): the bigger parameter table and settings
store, registrations, and gen_tick on a fake engine (song position at +0x30, the note queue 0x080963cc stubbed and
recorded): every algorithm, the step conditions, Prob, Epoch, Run, jumps.

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
lab = [e.cstr(getter_label(p)) if getter_label(p) else "" for p in (0x1D1, 0x1D6, 0x1DE, 0x1F9)]
check("parameter table: ids 0x1d1.. registered and readable through the patched getters (Run, Algo, Vel, lane 4 Vel)",
      lab == ["Run:", "Algo:", "Vel:", "Vel:"], lab)
buf = e.new(32)
e.mu.mem_write(buf, b"semig4vel\0")
check("lookup by xml name finds lane 4 Vel (0x1f9) past the old table end", e.call(0x0806C148, buf) == 0x1F9,
      hex(e.call(0x0806C148, buf)))

# --- settings store: the real init (0x08089cbc) with master's hook: GEN's 41 entries fit (capacity 0x80)
e.stub(0x08045012, lambda uc: None)
e.call(0x08089CBC, SET)
cnt = e.r16(SET + 0xA)
cap = e.r16(SET + 0xC)


def sget(pid):
    return struct.unpack("<i", struct.pack("<I", e.call(SYM["fast_get"], SET, pid)))[0]


def sset(pid, v):
    e.call(0x080893B2, SET, pid, v & 0xFFFFFFFF)


check("settings store: capacity 0x80, 100 entries (59 + GEN 41), GEN defaults (Run Off, lane 1 Euclid on pad 13)",
      cap == 0x80 and cnt == 100 and sget(0x1D1) == 0 and sget(0x1D6) == 1 and sget(0x1D7) == 12
      and sget(0x1D8) == 16 and sget(0x1D9) == 4, (cap, cnt, [sget(p) for p in range(0x1D1, 0x1DF)]))

# --- gen_tick on a fake engine
eng = e.new(0x10000)
e.w32(0x24023CB8 + 0xA204, eng)
notes = []
now = [0]


def post(uc):
    m = uc.reg_read(UC_ARM_REG_R1)
    typ = e.mu.mem_read(m, 1)[0]
    notes.append((now[0], typ, e.r32(m + 8), e.r32(m + 0x10)))


e.stub(0x080963CC, post)
GS = 0x2001C380


def run(t0, t1, dt=20):
    t = t0
    while t < t1:
        now[0] = t
        e.w32(eng + 0x30, t)
        e.call(SYM["gen_tick"])
        t += dt


def ons(cell=None):
    return [(t, c, v) for t, ty, c, v in notes if ty == 0x43 and (cell is None or c == cell)]


LANE = lambda l, k: 0x1D6 + 9 * l + k                       # noqa: E731
ALGO, PAD, STEPS, HITS, ROT, RATE, COND, PROB, VEL = range(9)
BAR = 3840


def reset(**lanes):
    for l in range(4):
        sset(LANE(l, ALGO), 0)
    sset(0x1D2, 0)
    sset(0x1D3, 0)
    sset(0x1D1, 0)
    run(0, 40)                                                # Run Off: state reset
    notes.clear()


def steps_of(lst, st=240, origin=0):
    return [(t - origin) // st for t, c, v in lst]


# 1. Euclid 16 / 4 on pad 13 (bottom left: row 0, col 0), 1/16: one hit per beat, on the beat, note off half a step on
reset()
sset(LANE(0, ALGO), 1)
sset(0x1D1, 1)
run(0, 2 * BAR)
o = ons()
offs = [(t, c) for t, ty, c, v in notes if ty == 0x42]
check("Euclid 16/4: 8 hits in 2 bars on the beats (within one block), cell 0x000 (pad 13), vel 100; note offs 120 later",
      [t // 960 for t, c, v in o] == list(range(8)) and all(t % 960 < 20 for t, c, v in o)
      and all(c == 0 and v == 100 for t, c, v in o) and len(offs) >= 7 and all(t % 960 >= 120 for t, c in offs[:7]),
      (o, offs[:3]))

# 2. E(3,8) with 1:2 on pad 1 (top left: row 3, col 0): cycles of 8 sixteenths, plays the 1st of every 2
reset()
for k, v in ((ALGO, 1), (PAD, 0), (STEPS, 8), (HITS, 3), (ROT, 0), (COND, 1)):
    sset(LANE(1, k), v)
sset(0x1D1, 1)
run(0, 2 * BAR)
s = steps_of(ons(0x300))
check("Cond 1:2: E(3,8) at sixteenths 0 3 6 of cycles 0 and 2 only (cell 0x300 = pad 1)", s == [0, 3, 6, 16, 19, 22], s)
sset(LANE(1, COND), 2)                                       # 2:2
notes.clear()
run(2 * BAR, 4 * BAR)
s = steps_of(ons(0x300), origin=2 * BAR)
check("Cond 2:2: cycles 1 and 3 of the window", s == [8, 11, 14, 24, 27, 30], s)
sset(LANE(1, COND), 8)                                       # 3:4
notes.clear()
run(4 * BAR, 6 * BAR)
s = steps_of(ons(0x300), origin=4 * BAR)                    # cycles 8..11: 3:4 -> cycle 10 (absolute 10 % 4 == 2)
check("Cond 3:4: only the 3rd of every 4 cycles", s == [16, 19, 22], s)

# 3. Fill / !Fill, First / !First, Prob
sset(LANE(1, COND), 10)
notes.clear()
run(6 * BAR, 7 * BAR)
nofill = len(ons(0x300))
sset(0x1D2, 1)
run(7 * BAR, 8 * BAR)
fill = len(ons(0x300))
sset(0x1D2, 0)
sset(LANE(1, COND), 11)
notes.clear()
run(8 * BAR, 9 * BAR)
nf = len(ons(0x300))
check("Fill: silent without Fill, plays with Fill On; !Fill plays without", nofill == 0 and fill == 6 and nf == 6,
      (nofill, fill, nf))
reset()
for k, v in ((ALGO, 1), (PAD, 0), (STEPS, 8), (HITS, 3), (ROT, 0), (COND, 12)):
    sset(LANE(1, k), v)
sset(0x1D1, 1)
run(BAR, 3 * BAR)
s = steps_of(ons(0x300), origin=BAR)
check("First: only the first cycle after Run", s == [0, 3, 6], s)
sset(0x1D1, 0)
run(3 * BAR, 3 * BAR + 40)
sset(LANE(1, COND), 13)
notes.clear()
sset(0x1D1, 1)
run(4 * BAR, 6 * BAR)
s = steps_of(ons(0x300), origin=4 * BAR)
check("!First: every cycle but the first", s == [8, 11, 14, 16, 19, 22, 24, 27, 30], s)
reset()
for k, v in ((ALGO, 1), (STEPS, 16), (HITS, 16), (PROB, 50)):
    sset(LANE(0, k), v)
sset(0x1D1, 1)
run(0, 8 * BAR)
n50 = len(ons())
notes.clear()
run(8 * BAR, 16 * BAR)
n50b = len(ons())
check("Prob 50 %: about half of 128 sixteenths each time, different steps", 44 <= n50 <= 84 and 44 <= n50b <= 84,
      (n50, n50b))

# 4. the algorithms (lane 0, 16 steps, 1/16): per cycle step lists
def cycles(algo, hits=4, rot=0, ncyc=8, steps=16, mutate=None, lane=0):
    reset()
    for k, v in ((ALGO, algo), (STEPS, steps), (HITS, hits), (ROT, rot), (PROB, 100), (COND, 0), (PAD, 12)):
        sset(LANE(lane, k), v)
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


br = cycles(2, hits=6)
check("Breakcut: every cycle starts with a hit, cuts of 1..4 steps (or a closing roll), cuts change between cycles",
      all(c and c[0][0] == 0 for c in br) and len({tuple(x for x, _ in c) for c in br}) >= 3
      and all(all(b - a <= 4 for (a, _), (b, _) in zip(c, c[1:])) for c in br),
      [[x for x, _ in c] for c in br])
cr = cycles(3, hits=4)
pat = [tuple(x for x, _ in c) for c in cr]
check("Call+Resp: cycles 0, 1, 2 the call (E(4,16)), cycle 3 the response (different), then again",
      pat[0] == pat[1] == pat[2] == (0, 4, 8, 12) and pat[3] != pat[0] and pat[4] == pat[0] and pat[7] != pat[0], pat)
ev = cycles(4, hits=6, mutate=60, ncyc=10)
pe = [tuple(x for x, _ in c) for c in ev]
check("Evolve: starts as Euclid, mutates (Mutate 60 %), stays near Hits",
      len(set(pe)) >= 3 and all(3 <= len(p) <= 10 for p in pe), pe)
d_lo = sum(len(c) for c in cycles(5, hits=4, rot=0, ncyc=2, steps=32))
d_hi = sum(len(c) for c in cycles(5, hits=24, rot=0, ncyc=2, steps=32))
dk = cycles(5, hits=8, rot=0, ncyc=1, steps=32)[0]
check("Drummap: more hits with more density; the kick map hits step 0, loud steps louder",
      0 < d_lo < d_hi and dk and dk[0][0] == 0 and dk[0][1] >= max(v for _, v in dk), (d_lo, d_hi, dk))
rn = cycles(6, hits=8, ncyc=8)
tot = sum(len(c) for c in rn)
check("Random: about Hits per cycle (8 of 16), varying", 40 <= tot <= 88 and len({tuple(c) for c in rn}) > 4, tot)

# 5. Epoch 4 bars: the first bar at half density, the last bar a fill
reset()
for k, v in ((ALGO, 1), (STEPS, 16), (HITS, 8), (PAD, 12)):
    sset(LANE(0, k), v)
for k, v in ((ALGO, 1), (STEPS, 16), (HITS, 4), (PAD, 0), (COND, 10)):
    sset(LANE(1, k), v)
sset(0x1D3, 1)
sset(0x1D1, 1)
run(0, 4 * BAR)
per_bar = [len([1 for t, c, v in ons(0) if t // BAR == b]) for b in range(4)]
fills = [t // BAR for t, c, v in ons(0x300)]
check("Epoch 4 bars: Euclid 8 -> 4, 6, 8, 8 hits per bar; Fill lane only in bar 4",
      per_bar == [4, 6, 8, 8] and fills and set(fills) == {3}, (per_bar, fills))

# 6. Run Off: note off for a sounding note, nothing more; a jump back: no burst of old steps
reset()
sset(LANE(0, ALGO), 1)
sset(0x1D1, 1)
run(0, 980)
sset(0x1D1, 0)
run(980, 3 * BAR)
check("Run Off: the pending note off is sent, then nothing", [ty for t, ty, c, v in notes][-1] == 0x42
      and not [1 for t, ty, c, v in notes if ty == 0x43 and t > 980], notes[-3:])
reset()
sset(LANE(0, ALGO), 1)
sset(0x1D1, 1)
run(10 * BAR, 10 * BAR + 1000)
notes.clear()
run(0, 100)                                                 # song restarts
o = ons()
check("song position jump back: restarts on the next step, no burst", len(o) == 1 and o[0][0] < 40, o)

print(f"\n{'FAILED: ' + str(failures) if failures else 'all passed'}")
sys.exit(1 if failures else 0)
