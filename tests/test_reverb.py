# ported to the bitbox MK2 2.3.4 by tools/port_mk2.py
"""Unicorn tests for patches/reverb.py (src/reverb.c) on the patched image: registration, the reverb cell record, the
real reverb state init 0x080aae74 and plate core 0x080ab0a4 (Type Plate bit-identical), impulse responses of Room /
Hall / Chamber (decay time, stereo, stability), the page filter, and the cost per block.

  python3 -I tests/test_reverb.py      (after ./build.sh and patch.py fidelity pages padpages sound reverb fx3 cave)
"""
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_src = open(os.path.join(HERE, "test_fidelity.py")).read()
exec(compile(_src[:_src.index("\ne = Emu()\n")], "test_fidelity.py (header)", "exec"))   # Emu, check, IMG, SYM, ...

from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.arm_const import UC_ARM_REG_R4  # noqa: E402

e = Emu()
e.stub(0x08043B88, lambda uc: uc.reg_write(UC_ARM_REG_R0, e.new(uc.reg_read(UC_ARM_REG_R0))))  # DTCM allocator (size)
APP = 0x24015170
REC = APP + 0x2200 + 1 * 0x18                    # reverb cell (row 1)
FS = 29800

# --- registration
lists, knobs = {}, []
def fake_list(uc):
    sp = uc.reg_read(UC_ARM_REG_SP)
    lists[uc.reg_read(UC_ARM_REG_R1)] = (e.cstr(uc.reg_read(UC_ARM_REG_R2)), uc.reg_read(UC_ARM_REG_R3), e.r32(sp),
                                         e.cstr(e.r32(sp + 4)))
def fake_knob(uc):
    sp = uc.reg_read(UC_ARM_REG_SP)
    knobs.append((uc.reg_read(UC_ARM_REG_R1), uc.reg_read(UC_ARM_REG_R2), e.cstr(uc.reg_read(UC_ARM_REG_R3)),
                  e.r32(sp), e.r32(sp + 4)))
e.stub(0x0806A27C, fake_list)
e.stub(0x0806A2D4, fake_knob)
e.call(SYM["reverb_register"], 0, 0x178, 0x080C203C, 0x080C58C0, stack=(2, 0x080C2044))
del e.stubs[0x0806A27C], e.stubs[0x0806A2D4]
t = lists.get(0x178)
check("Type list: Plate, Room, Hall, Chamber (label Type:, xml stays fx2algo's slot)",
      t and t[0] == "Type:" and [e.cstr(e.r32(t[1] + 4 * k)) for k in range(t[2])] == ["Plate", "Room", "Hall", "Chamber"])
check("eleven settings registered: ids 0x191-0x19b (Level last), 0..1000",
      [k[0] for k in knobs] == list(range(0x191, 0x19C)) and knobs[-1][2] == "Level:" and all(k[1] == 8 and k[3] == 0 and k[4] == 1000 for k in knobs),
      knobs)

# --- reverb cell record (type 0x44): Type first, stock three, then the settings
typ = e.new(4)
e.mu.mem_write(typ, b"\x44")
e.call(0x08089E7C, REC, typ)
cnt, ent = struct.unpack("<H", e.mu.mem_read(REC + 0xA, 2))[0], e.r32(REC + 4)
ids = [struct.unpack("<H", e.mu.mem_read(ent + 8 * k, 2))[0] for k in range(cnt)]
vals = [e.r32(ent + 8 * k + 4) for k in range(cnt)]
check("reverb record: Type, Decay, Predelay, Damping, then Size .. Lo Cut, Level with defaults",
      ids == [0x178, 0x1A, 0x1E, 0x1D] + list(range(0x191, 0x19C))
      and vals == [0, 600, 40, 500, 500, 700, 500, 400, 300, 400, 300, 1000, 800, 100, 500], (ids, vals))

def setp(pid, v):
    e.call(0x080893B2, REC, pid, v)

# --- real plate state (init 0x080aae74 on 0xb0 bytes), reverb object with the cell's id
def new_state(decay=0.6, damp=0.5):
    st = e.new(0xB0)
    e.call(0x080AAE74, st)
    e.wf(st, [decay * 0.93, damp])
    return st

obj = e.new(0x200)
e.w32(obj + 0x18, 0x30100)
N = 64

def run(st, sig, n=N):
    """feed sig through reverb_core_thunk in blocks; returns (L, R)"""
    inb, lb, rb = e.new(4 * n), e.new(4 * n), e.new(4 * n)
    L, R = [], []
    for k in range(0, len(sig), n):
        e.wf(inb, sig[k:k + n])
        e.call(SYM["reverb_core_thunk"], st, inb, lb, rb, stack=(n,), regs=[(UC_ARM_REG_R4, obj)])
        L += e.rf(lb, n)
        R += e.rf(rb, n)
    return L, R

def stock(st, sig, n=N):
    inb, lb, rb = e.new(4 * n), e.new(4 * n), e.new(4 * n)
    L, R = [], []
    for k in range(0, len(sig), n):
        e.wf(inb, sig[k:k + n])
        e.call(0x080AB0A4, st, inb, lb, rb, stack=(n,))
        L += e.rf(lb, n)
        R += e.rf(rb, n)
    return L, R

imp = [1.0] + [0.0] * (N * 40 - 1)
setp(0x178, 0)
a = run(new_state(), imp)
b = stock(new_state(), imp)
check("Type Plate: identical to the stock plate core", a == b)

def rt60(sig, fs=FS):
    """decay time from the energy envelope (Schroeder integral, -5 .. -25 dB fit, x 3)"""
    e2 = [v * v for v in sig]
    tail = []
    acc = 0.0
    for v in reversed(e2):
        acc += v
        tail.append(acc)
    tail.reverse()
    tot = tail[0] or 1e-30
    db = [10 * math.log10(x / tot + 1e-30) for x in tail]
    i5 = next((i for i, v in enumerate(db) if v <= -5), None)
    i25 = next((i for i, v in enumerate(db) if v <= -25), None)
    if i5 is None or i25 is None:
        return None
    return 3.0 * (i25 - i5) / fs

def corr(x, y):
    sxy = sum(p * q for p, q in zip(x, y))
    sx = math.sqrt(sum(p * p for p in x)) or 1e-30
    sy = math.sqrt(sum(q * q for q in y)) or 1e-30
    return sxy / (sx * sy)

results = {}
for name, tval in (("Room", 1), ("Hall", 2), ("Chamber", 3)):
    setp(0x178, tval)
    for decay in (0.3, 0.7):
        st = new_state(decay=decay)
        L, R = run(st, [1.0] + [0.0] * (FS * 3 - 1))
        results[(name, decay)] = (rt60(L), max(abs(v) for v in L + R), corr(L[3000:], R[3000:]),
                                  all(math.isfinite(v) for v in L + R))
for (name, decay), (rt, peak, c, fin) in sorted(results.items()):
    print(f"     {name:8} decay {decay}: RT60 {rt if rt is None else round(rt, 2)} s, peak {peak:.3f}, L/R corr {c:.2f}")
for name in ("Room", "Hall", "Chamber"):
    r3, r7 = results[(name, 0.3)][0], results[(name, 0.7)][0]
    check(f"{name}: impulse response finite, decays, longer at Decay 70 % than 30 %, stereo decorrelated",
          all(results[(name, d)][3] for d in (0.3, 0.7)) and r3 and r7 and r7 > 1.3 * r3
          and results[(name, 0.7)][2] < 0.5 and results[(name, 0.7)][1] < 2.0, (r3, r7))

# stability: full decay, noise in, long run; size and modulation swept while running
setp(0x178, 2)
setp(0x191, 1000)
setp(0x196, 1000)
st = new_state(decay=1.0, damp=0.0)
noise = [((i * 1103515245 + 12345) % 65536) / 32768.0 - 1.0 for i in range(FS)]
L, R = run(st, noise)
setp(0x191, 0)
L2, R2 = run(st, [0.0] * FS)
check("Hall at full Decay, no damping, Size and Mod Depth at max, then Size to min: finite, bounded",
      all(math.isfinite(v) for v in L + R + L2 + R2) and max(abs(v) for v in L + R + L2 + R2) < 20,
      max(abs(v) for v in L + R + L2 + R2))
setp(0x191, 500)
setp(0x196, 400)

# Bass Mult: longer low-frequency tail with a larger multiplier
def low_tail(bass):
    setp(0x193, bass)
    st = new_state(decay=0.6, damp=0.3)
    L, _ = run(st, [1.0] + [0.0] * (FS * 2 - 1))
    lp, acc = 0.0, 0.0
    for i, v in enumerate(L):
        lp += 0.02 * (v - lp)
        if i > FS:
            acc += lp * lp
    return acc
lo, hi = low_tail(100), low_tail(900)
setp(0x193, 500)
check("Hall: Bass Mult up -> more low end in the late tail", hi > 2 * lo, (lo, hi))

# Early: early reflections only in the first 100 ms
def early_energy(lvl):
    setp(0x197, lvl)
    st = new_state()
    L, _ = run(st, [1.0] + [0.0] * (FS // 4 - 1))
    return sum(v * v for v in L[: FS // 15])
check("Hall: Early 0 vs 100 %: more energy in the first 70 ms", early_energy(1000) > 1.5 * early_energy(0))
setp(0x197, 300)

# --- page filter: Type Plate hides the settings
def page_ids(tval):
    setp(0x178, tval)
    lst = e.new(4 + 12 * 0x30)
    def fill(uc):
        pids = [0x178, 0x1A, 0x1E, 0x1D] + list(range(0x191, 0x19C))
        e.w32(lst, len(pids))
        for k, pid in enumerate(pids):
            e.mu.mem_write(lst + 4 + 12 * k, struct.pack("<HHHHI", pid, 0, 0, 0, 0))
        uc.reg_write(UC_ARM_REG_R0, 1)
    e.stub(0x08078EC8, fill)
    m = e.new(8)
    e.w32(m, 0x080C05EC)
    e.mu.mem_write(m + 4, struct.pack("<H", 0x310))
    e.call(SYM["clip_page"], APP, m, lst, 0)
    del e.stubs[0x08078EC8]
    return [struct.unpack_from("<H", e.mu.mem_read(lst + 4 + 12 * k, 2))[0] for k in range(e.r32(lst))]
check("reverb page: Plate shows Type + stock three + Level; Hall shows all 15",
      page_ids(0) == [0x178, 0x1A, 0x1E, 0x1D, 0x19B] and len(page_ids(2)) == 15)

# Level: -12 / 0 / +12 dB on the plate (stock core, scaled after it)
def level_peak(v):
    setp(0x19B, v)
    setp(0x178, 0)
    st = new_state()
    L, R = run(st, [1.0] + [0.0] * (N * 20 - 1))
    setp(0x19B, 500)
    return max(abs(x) for x in L + R)
p0, p5, p10 = level_peak(0), level_peak(500), level_peak(1000)
check("Level 0 / 500 / 1000: -12 / 0 / +12 dB (+-0.3 dB)",
      abs(20 * math.log10(p0 / p5) + 12) < 0.3 and abs(20 * math.log10(p10 / p5) - 12) < 0.3,
      (20 * math.log10(p0 / p5), 20 * math.log10(p10 / p5)))

# loudness against the plate: noise in, steady RMS within 1.5 dB for every type
import random as _r
_r.seed(5)
noise = [_r.uniform(-0.5, 0.5) for _ in range(FS // 2)]
noise = noise[: len(noise) // N * N]
lv = {}
for name, t in (("Plate", 0), ("Room", 1), ("Hall", 2), ("Chamber", 3)):
    setp(0x178, t)
    L, R = run(new_state(decay=0.55, damp=0.4), noise)
    lv[name] = 10 * math.log10(sum(v * v for v in L[FS // 4:] + R[FS // 4:]) / len(L[FS // 4:] + R[FS // 4:]))
print("     levels (dB, steady):", {k: round(v, 1) for k, v in lv.items()})
check("Room / Hall / Chamber within 1.5 dB of the plate (make-up gains)",
      all(abs(lv[k] - lv["Plate"]) < 1.5 for k in ("Room", "Hall", "Chamber")), lv)

# --- cost: instructions per 64-frame block at 29.8 kHz
cnt = [0]
h = e.mu.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: cnt.__setitem__(0, cnt[0] + 1))
for name, tval in (("Plate", 0), ("Room", 1), ("Hall", 2), ("Chamber", 3)):
    setp(0x178, tval)
    st = new_state()
    run(st, [0.1] * (N * 2))
    cnt[0] = 0
    run(st, [0.1] * (N * 8))
    per_s = cnt[0] / 8 * FS / N
    print(f"     {name:8}: {cnt[0] // 8} instructions per {N}-frame block, ~{per_s / 1e6:.1f} M/s at 29.8 kHz")
e.mu.hook_del(h)

print(f"\n{'FAILED: ' + str(failures) if failures else 'all passed'}")
sys.exit(1 if failures else 0)
