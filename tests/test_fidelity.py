# ported to the bitbox MK2 2.3.4 by tools/port_mk2.py
"""Unicorn tests for patches/fidelity.py and patches/pages.py on the patched image (out/fidelity+pages+sound+reverb+cave/BITBOXMK2.bin).

Real firmware code runs for: the Interp registration call, the app pad store (init 0x08089e7c, get/set), the stock
cutoff / resonance curves, the parameter evaluator, the audio-buffer accessors, the two resampling kernels and the
voice-start filter reset. Only the allocator (0x08043b4c) is replaced, by a bump allocator.

  python3 -I tests/test_fidelity.py        (after ./build.sh and patch.py fidelity pages padpages sound reverb fx3 master cave)
"""
import math
import os
import struct
import sys

from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_THUMB, Uc
from unicorn.arm_const import UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_CPU_ARM_MAX
from unicorn.arm_const import (UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_S0, UC_ARM_REG_S1, UC_ARM_REG_S15, UC_ARM_REG_SL,
                               UC_ARM_REG_SP)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "patches"))
from thumb import BASE, bl_target, symbols  # noqa: E402

IMG = open(os.path.join(ROOT, "out", os.environ.get("PK_BUILD", "fidelity+pages+sound+reverb+fixes+master+fx+cave"), "BITBOXMK2.bin"), "rb").read()
SYM = symbols(os.path.join(ROOT, "out", "cave.elf"))

APP_PADS = 0x24015170 + 0x1840
PARK = 0x2406BD00
RET = 0x24000000
STACK = 0x2407F000
P_INTERP = 0xD2
NAMES = ["Normal", "HighQ", "SP1200", "SP-12", "S950", "MPC60", "SP Raw", "16k", "12k", "10k", "8k", "6k", "4k", "2k"]
N = 64

failures = 0


def check(what, ok, detail=""):
    global failures
    print(("ok   " if ok else "FAIL ") + what + ("" if ok else f"   {detail}"))
    failures += 0 if ok else 1


class Emu:
    def __init__(self):
        # Unicorn 2.1's M-profile CPUs have no double precision, which the stock Res curve uses (vcvt.f64.f32 at
        # 0x080a1636). As blackbox-mod/emu.py: an A-profile CPU in Thumb state with the FPU on runs the same Thumb-2
        # and FPv5 code.
        mu = self.mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        mu.ctl_set_cpu_model(UC_CPU_ARM_MAX)
        mu.mem_map(BASE, (len(IMG) + 0xFFFF) & ~0xFFFF)
        mu.mem_write(BASE, IMG)
        mu.mem_map(0x24000000, 0x80000)          # AXI SRAM
        mu.reg_write(UC_ARM_REG_C1_C0_2, 0xF << 20)   # FPU on
        mu.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        mu.mem_map(0x30000000, 0x1000000)
        mu.mem_map(0xE0000000, 0x100000)         # SCS: DWT cycle counter, DEMCR (load meter)
        self.heap = 0x30000000                   # scratch for test objects (D2 SRAM; clear of PARK / pad state)
        self.alloc_at = 0x24040000               # bump allocator for the firmware's own allocations
        self.stubs, self.hooked = {}, set()
        mu.hook_add(UC_HOOK_CODE, lambda uc, a, size, _: uc.emu_stop(), begin=RET, end=RET)
        self.stub(0x08043B4C, self._alloc)

    def _hook(self, uc, addr, size, _):
        if addr in self.stubs:
            self.stubs[addr](uc)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def stub(self, addr, fn):
        addr &= ~1
        self.stubs[addr] = fn
        if addr not in self.hooked:              # one hook per address; a hook on every instruction is ~50x slower
            self.hooked.add(addr)
            self.mu.hook_add(UC_HOOK_CODE, self._hook, begin=addr, end=addr)

    def _alloc(self, uc):
        n = uc.reg_read(UC_ARM_REG_R0) * max(1, uc.reg_read(UC_ARM_REG_R1))
        p = self.alloc_at
        self.alloc_at += (n + 7) & ~7
        uc.mem_write(p, bytes(n))
        uc.reg_write(UC_ARM_REG_R0, p)

    def new(self, size):
        p = self.heap
        self.heap += (size + 15) & ~15
        assert self.heap <= 0x31000000, "test heap exhausted"
        self.mu.mem_write(p, bytes(size))
        return p

    def w32(self, a, v):
        self.mu.mem_write(a, struct.pack("<I", v & 0xFFFFFFFF))

    def r32(self, a):
        return struct.unpack("<I", self.mu.mem_read(a, 4))[0]

    def r16(self, a):
        return struct.unpack("<H", self.mu.mem_read(a, 2))[0]

    def wf(self, a, vals):
        self.mu.mem_write(a, struct.pack(f"<{len(vals)}f", *vals))

    def rf(self, a, n):
        return list(struct.unpack(f"<{n}f", self.mu.mem_read(a, 4 * n)))

    def call(self, fn, *args, s=(), regs=(), stack=()):
        mu = self.mu
        sp = STACK - 4 * len(stack)
        for k, v in enumerate(stack):
            self.w32(sp + 4 * k, v)
        for r, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            mu.reg_write(r, v & 0xFFFFFFFF)
        for r, v in zip((UC_ARM_REG_S0, UC_ARM_REG_S1), s):
            mu.reg_write(r, struct.unpack("<I", struct.pack("<f", v))[0])
        for r, v in regs:
            mu.reg_write(r, v)
        mu.reg_write(UC_ARM_REG_SP, sp)
        mu.reg_write(UC_ARM_REG_LR, RET | 1)
        mu.emu_start(fn | 1, RET, count=20_000_000)
        return mu.reg_read(UC_ARM_REG_R0)

    def cstr(self, a):
        return bytes(self.mu.mem_read(a, 32)).split(b"\0")[0].decode()


e = Emu()

# --- the Interp registration call site in the patched image now reaches interp_register
seen = {}


lists = {}


def fake_register(uc):
    sp = uc.reg_read(UC_ARM_REG_SP)
    lists[uc.reg_read(UC_ARM_REG_R1)] = (e.cstr(uc.reg_read(UC_ARM_REG_R2)), uc.reg_read(UC_ARM_REG_R3), e.r32(sp),
                                         e.cstr(e.r32(sp + 4)))
    if uc.reg_read(UC_ARM_REG_R1) != P_INTERP:
        return
    seen.update(param=uc.reg_read(UC_ARM_REG_R1), label=uc.reg_read(UC_ARM_REG_R2), names=uc.reg_read(UC_ARM_REG_R3),
                count=e.r32(sp), xml=e.r32(sp + 4))


knobs = []


def fake_knob(uc):
    sp = uc.reg_read(UC_ARM_REG_SP)
    to_i = lambda v: v - (1 << 32) if v >> 31 else v
    knobs.append((uc.reg_read(UC_ARM_REG_R1), uc.reg_read(UC_ARM_REG_R2), e.cstr(uc.reg_read(UC_ARM_REG_R3)),
                  to_i(e.r32(sp)), to_i(e.r32(sp + 4)), e.cstr(e.r32(sp + 8))))


e.stub(0x0806A27C, fake_register)
e.stub(0x0806A2D4, fake_knob)
# run the stock instructions around the call site: r4 = table, 0x0806b72e .. 0x0806b742
e.mu.reg_write(UC_ARM_REG_R0, 0)
e.mu.reg_write(UC_ARM_REG_SP, STACK - 0x40)
e.mu.reg_write(UC_ARM_REG_LR, RET | 1)
e.mu.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: uc.emu_stop(), begin=0x0806B742, end=0x0806B742)
e.mu.emu_start(0x0806B72E | 1, 0x0806B742, count=1000)
names = [e.cstr(e.r32(seen["names"] + 4 * k)) for k in range(seen.get("count", 0))]
check("Fidelity list: Normal, HighQ, 5 machines, 16k .. 2k", names == NAMES and seen["param"] == P_INTERP, names)
check("label 'Fidelity:', xml name stays 'interpqual'",
      e.cstr(seen["label"]) == "Fidelity:" and e.cstr(seen["xml"]) == "interpqual", (e.cstr(seen["label"]), e.cstr(seen["xml"])))
check("new knobs registered: Filter 2 (bipolar), Res 2, Drive",
      knobs[:7] == [(0x1C0, 9, "Filter 2:", -1000, 1000, "pkfilt2"), (0x1C1, 8, "Res 2:", 0, 1000, "pkres2"),
                (0x1C2, 8, "Drive:", 0, 1000, "pkdrive"), (0x1C3, 1, "Shift ms:", -100, 100, "pkshift"),
                (0x1C6, 1, "Stretch %:", -100, 100, "pkstretch"),
                (0x1C8, 8, "FX3 Send:", 0, 1000, "fx3send"), (0x183, 1, "Loop Fade:", 0, 50, "pkloopfade")], knobs)
check("delay knobs (fx.c, registered first by master_register): 0x1ae..0x1b4",
      [k[0] for k in knobs[7:14]] == list(range(0x1AE, 0x1B5)), knobs[7:14])
knobs = knobs[:7] + knobs[14:]
check("master section knobs registered after them (master.c; Dist Type is a list; MK2 label 'Dist Lvl:')",
      [k[0] for k in knobs[7:]] == [0x1A1, 0x1A2, 0x1A3, 0x1A5, 0x1A6, 0x19E, 0x1A8, 0x1A9, 0x1AB, 0x1AC]
      and knobs[-1][2] == "Dist Lvl:"
      and [lists.get(i, ("",))[0] for i in (0x19D, 0x19F, 0x17D, 0x17F, 0x1A7)]
      == ["Comp Type:", "Ratio:", "Attack:", "Release:", "Dist Type:"], knobs[7:])
pl = lists.get(0x1C4)
pl_names = [e.cstr(e.r32(pl[1] + 4 * k)) for k in range(pl[2])] if pl else []
check("Play Len list: Full, 1/4 .. 32 bars in quarter bars, xml pkplaylen",
      pl and pl[0] == "Play Len:" and pl[3] == "pkplaylen" and len(pl_names) == 129 and pl_names[:6] ==
      ["Full", "1/4", "1/2", "3/4", "1", "1 1/4"] and pl_names[-1] == "32" and pl_names[8] == "2", pl_names[:10])
sm = lists.get(0x1C7)
check("Stretch Mode list: Auto, Jump, Smooth (xml pkstretchmode)", sm and sm[0] == "Str Mode:" and sm[2] == 3
      and [e.cstr(e.r32(sm[1] + 4 * k)) for k in range(3)] == ["Auto", "Jump", "Smooth"] and sm[3] == "pkstretchmode")
del e.stubs[0x0806A27C]
del e.stubs[0x0806A2D4]

# --- app pad records (real store init, type 0x45 = grid pads at boot) and engine pads
typ = e.new(4)
e.mu.mem_write(typ, b"\x45")


def record(row, col):
    return APP_PADS + row * 0xF0 + col * 0x30


for row in range(4):
    for col in range(4):
        e.call(0x08089E7C, record(row, col), typ)


def set_interp(v, row=1, col=2):
    e.call(0x080893B2, record(row, col), P_INTERP, v)


def set_param(pid, v, row=1, col=2):
    e.call(0x080893B2, record(row, col), pid, v)


def store_ids(rec):
    cnt, cap, ent = e.r16(rec + 0xA), e.r16(rec + 0xC), e.r32(rec + 4)
    return {e.r16(ent + 8 * k): e.r32(ent + 8 * k + 4) for k in range(cnt)}, cnt, cap


ids45, cnt45, cap45 = store_ids(record(1, 2))
rec3 = e.new(0x30)
typ3 = e.new(4)
e.mu.mem_write(typ3, b"\x03")
e.call(0x08089E7C, rec3, typ3)
ids3, cnt3, cap3 = store_ids(rec3)
check("pad records (type 0x45 and 3): Filter 2 = 0, Res 2 = 500, Drive = 0, within capacity",
      all(d.get(0x1C0) == 0 and d.get(0x1C1) == 500 and d.get(0x1C2) == 0 and d.get(0x1C3) == 0 and d.get(0x1C4) == 0
          and d.get(0x1C6) == 0 for d in (ids45, ids3))
      and cnt3 <= cap3 and cnt45 <= cap45 and 0xAA in ids3, (cnt45, cap45, cnt3, cap3))


def engine_pad(row=1, col=2):
    p = e.new(0x700)
    e.w32(p + 0x18, row << 8 | col)
    return p


# --- filter object (stock ctor 0x080b9318) with a parameter block: Filter knob @+8, Res @+0x44 (evaluator 0x080a2d9c)
def param(at, value):
    src = e.new(8)
    e.wf(src + 4, [value])
    e.w32(at + 0, src)          # slot 0: source
    e.wf(at + 4, [1.0])         # slot 0: amount
    e.w32(at + 0x20, 0)         # slot order
    e.w32(at + 0x24, 1)         # one slot
    e.wf(at + 0x28, [-1.0, 1.0])
    return src


def filter_obj(knob, res=0.5):
    obj = e.new(0x100)
    e.call(0x080B9318, obj)
    params = e.new(0x100)
    param(params + 8, knob)
    param(params + 0x44, res)
    e.w32(obj + 0x84, params)
    return obj


def buffer(left, right=None):
    b = e.new(0x20)
    L = e.new(4 * len(left))
    e.wf(L, left)
    R = 0
    if right is not None:
        R = e.new(4 * len(right))
        e.wf(R, right)
    e.w32(b + 0, len(left))
    e.w32(b + 4, len(left))
    e.w32(b + 8, L)
    e.w32(b + 0xC, R)
    e.mu.mem_write(b + 0x10, bytes([0, 1]))
    return b, L, R


def sine(f, n=N, a=0.8, ph=0):
    return [a * math.sin(2 * math.pi * f * (i + ph) / 48000.0) for i in range(n)]


def run_a(obj, b, pad):
    e.call(SYM["filter_voice_a"], obj, b, regs=[(UC_ARM_REG_R5, pad)])


def run_b(obj, b, pad):
    e.call(SYM["filter_voice_b"], obj, b, regs=[(UC_ARM_REG_R6, pad)])


pad = engine_pad()
x = sine(997)

set_interp(0)
obj = filter_obj(0.0)
b, L, R = buffer(x, x)
run_a(obj, b, pad)
check("Normal, knob centre: buffer untouched", all(abs(p - q) < 1e-7 for p, q in zip(e.rf(L, N), x)))

set_interp(NAMES.index("12k"))
obj = filter_obj(0.0)
b, L, R = buffer(x, x)
run_a(obj, b, pad)
out = e.rf(L, N)
runs = [out[i] == out[i + 1] for i in range(N - 1)]
check("12k: zero-order hold (one new value per 4 samples)", len(set(out)) <= N // 4 + 1, len(set(out)))
check("12k: 12-bit values", all(abs(v * 2048 - round(v * 2048)) < 1e-4 for v in out))
check("12k: right channel crunched too", e.rf(R, N) == out)

other = engine_pad(row=0, col=0)
set_interp(0, row=0, col=0)
obj = filter_obj(0.0)
b, L, R = buffer(x, x)
run_a(obj, b, other)
check("another pad's Interp (Normal) is its own: no crunch", all(abs(p - q) < 1e-7 for p, q in zip(e.rf(L, N), x)))

obj = filter_obj(0.0)
b, L, R = buffer(x, x)
run_b(obj, b, pad)
check("class B thunk (pad in r6) finds the pad: crunch applies", len(set(e.rf(L, N))) <= N // 4 + 1)

obj = filter_obj(0.0)
b, L, R = buffer(x, x)
run_a(obj, b, 0)
check("no pad: no crunch", all(abs(p - q) < 1e-7 for p, q in zip(e.rf(L, N), x)))

# --- 24 dB SVF
set_interp(0)


def rms_after(f, knob, blocks=8):
    obj = filter_obj(knob)
    acc = []
    for k in range(blocks):
        y = sine(f, ph=k * N)
        b, L, R = buffer(y, y)
        run_a(obj, b, pad)
        acc = e.rf(L, N)
    return math.sqrt(sum(v * v for v in acc) / N)


lp_hi, lp_lo = rms_after(12000, -0.6), rms_after(200, -0.6)
check("Filter left (LP): 12 kHz far below 200 Hz", lp_hi < 0.05 * lp_lo, (lp_hi, lp_lo))
hp_hi, hp_lo = rms_after(12000, 0.6), rms_after(200, 0.6)
check("Filter right (HP): 200 Hz far below 12 kHz", hp_lo < 0.05 * hp_hi, (hp_lo, hp_hi))

# --- Filter 2, Drive (new pad parameters, read from the app record)
def rms_f2(f, knob2, res2=500, knob=0.0, blocks=8):
    set_param(0x1C0, knob2)
    set_param(0x1C1, res2)
    obj = filter_obj(knob)
    for k in range(blocks):
        y = sine(f, ph=k * N)
        b, L, R = buffer(y, y)
        run_a(obj, b, pad)
        acc = e.rf(L, N)
    set_param(0x1C0, 0)
    set_param(0x1C1, 500)
    return math.sqrt(sum(v * v for v in acc) / N)


lo2, hi2 = rms_f2(200, -600), rms_f2(12000, -600)
check("Filter 2 alone, left (LP): 12 kHz far below 200 Hz", hi2 < 0.05 * lo2, (hi2, lo2))
lo2, hi2 = rms_f2(200, 600), rms_f2(12000, 600)
check("Filter 2 alone, right (HP): 200 Hz far below 12 kHz", lo2 < 0.05 * hi2, (lo2, hi2))
mid1 = rms_after(12000, -0.4, blocks=16)
mid12 = rms_f2(12000, -400, knob=-0.4, blocks=16)
check("Filter + Filter 2 in series: more attenuation than Filter alone", mid12 < 0.5 * mid1, (mid12, mid1))
check("Filter 2 HP + Filter LP = band: 200 Hz and 12 kHz both down",
      rms_f2(200, 500, knob=-0.5) < 0.1 * 0.8 / math.sqrt(2) and rms_f2(12000, 500, knob=-0.5) < 0.1 * 0.8 / math.sqrt(2))


def run_drive(d, amp, blocks=4):
    set_param(0x1C2, d)
    obj = filter_obj(0.0)
    for k in range(blocks):
        y = sine(997, a=amp, ph=k * N)
        b, L, R = buffer(y, y)
        run_a(obj, b, pad)
        out = e.rf(L, N)
    set_param(0x1C2, 0)
    return y, out


y, out = run_drive(0, 0.8)
check("Drive 0, filters off: buffer untouched", all(abs(p - q) < 1e-7 for p, q in zip(out, y)))
y, out = run_drive(1000, 0.8)
check("Drive full: bounded (|y| <= 1), finite", all(math.isfinite(v) and abs(v) <= 1.0001 for v in out), max(out))


def harm3(sig, f=997):
    c = sum(v * math.cos(2 * math.pi * 3 * f * i / 48000) for i, v in enumerate(sig))
    s_ = sum(v * math.sin(2 * math.pi * 3 * f * i / 48000) for i, v in enumerate(sig))
    c1 = sum(v * math.cos(2 * math.pi * f * i / 48000) for i, v in enumerate(sig))
    s1 = sum(v * math.sin(2 * math.pi * f * i / 48000) for i, v in enumerate(sig))
    return math.hypot(c, s_) / math.hypot(c1, s1)


h_lo, h_hi = harm3(run_drive(200, 0.5)[1]), harm3(run_drive(1000, 0.5)[1])
check("Drive adds odd harmonics, more at full than at 20 %", h_hi > 0.1 and h_hi > 2 * h_lo, (h_lo, h_hi))
_, q1 = run_drive(1000, 0.05)
check("Drive full: quiet input comes up (makeup)", max(q1) > 0.3, max(q1))

set_param(0x1C1, 1000)
obj = filter_obj(-0.4, res=1.0)
outs = []
for amp in (0.1, 0.8):
    obj = filter_obj(-0.4, res=1.0)
    for k in range(8):
        y = sine(300, a=amp, ph=k * N)
        b, L, R = buffer(y, y)
        run_a(obj, b, pad)
    outs.append(e.rf(L, N))
set_param(0x1C1, 500)
check("Res full without Drive is linear (no saturation on Res any more)",
      all(abs(8 * p - q) < 1e-3 for p, q in zip(outs[0], outs[1])))

# voice-start reset clears Filter 2's running flag too
set_param(0x1C0, -500)
obj = filter_obj(0.0)
for k in range(2):
    b, L, R = buffer(sine(300, ph=k * N), sine(300, ph=k * N))
    run_a(obj, b, pad)
on2 = e.r32(obj + 0x4C)
e.call(0x080B98BC, obj)
check("Filter 2 state: running flag (+0x4c) set, cleared by the stock voice-start reset", on2 == 1 and e.r32(obj + 0x4C) == 0)
set_param(0x1C0, 0)

# --- MK2: LFO page (page 2 of 5 x 36 ids) of every pad mode: the five LFO ids, then Drive, Filter 2, Res 2
MK2_PAGE = 36 * 2
lfos = {t: struct.unpack_from("<9H", IMG, t + 2 * MK2_PAGE - 0x08040000) for t in
        (0x080C7BD4, 0x080C7D3C, 0x080C7F3C, 0x080C80A4, 0x080C84F4, 0x080C865C, 0x080C87C4, 0x080C8A04, 0x080C8B6C)}
check("LFO pages: + Drive, Filter 2, Res 2 after the LFO's five (Multisample: its own LFO order)",
      all(m == ((0x08, 0x09, 0x0B, 0x0D, 0x0C) if t == 0x080C87C4 else (0x08, 0x09, 0x0B, 0x0C, 0x0D))
          + (0x1C2, 0x1C0, 0x1C1, 0) for t, m in lfos.items()), lfos)

# --- clip shift: the clip reader's stream calls, real 0x080a58a8 on an in-memory stream (1000 frames, value = index)
stream = e.new(0x40)
plane = e.new(4 * 1000)
e.wf(plane, [float(i) for i in range(1000)])
e.w32(stream + 4, plane)
e.w32(stream + 8, plane)
e.w32(stream + 0x10, 1000)
cpad = engine_pad()


def clip_read(pos, n=64, pad_=None):
    dst = e.new(4 * n)
    e.call(SYM["clip_read1_thunk"], stream, dst, n, pos, stack=(0,), regs=[(UC_ARM_REG_R4, cpad if pad_ is None else pad_)])
    return [int(v) for v in e.rf(dst, n)]


def stock_read(pos, n=64):
    dst = e.new(4 * n)
    e.call(0x080A58A8, stream, dst, n, pos, stack=(0,))
    return [int(v) for v in e.rf(dst, n)]


set_param(0x1C3, 0)
check("Shift 0: clip read identical to stock", clip_read(100) == stock_read(100) == list(range(100, 164)))
set_param(0x1C3, 10)
check("Shift +10 ms: reads 480 frames later", clip_read(0) == list(range(480, 544)))
check("Shift +10 ms: wraps at the file end (seamless)", clip_read(500) == list(range(980, 1000)) + list(range(0, 44)))
check("Shift +10 ms: past the end stays silent as stock", clip_read(990, 20) == list(range(470, 480)) + [0] * 10)
set_param(0x1C3, -10)
check("Shift -10 ms: downbeat reads the file's end", clip_read(0) == list(range(520, 584)))
sliced = engine_pad()
e.mu.mem_write(sliced + 0x254, b"\x01")
set_param(0x1C3, 10)
check("Shift on a sliced pad: ignored", clip_read(0, pad_=sliced) == list(range(0, 64)))
seeks = []
e.stub(0x080A59CC, lambda uc: seeks.append(uc.reg_read(UC_ARM_REG_R1)))
e.call(SYM["clip_seek_thunk"], stream, 992, regs=[(UC_ARM_REG_R4, cpad)])
e.call(SYM["clip_seek_thunk"], stream, 100, regs=[(UC_ARM_REG_R4, cpad)])
del e.stubs[0x080A59CC]
check("Shift: segment prefetch seek follows (wrapped)", seeks == [472, 580], seeks)
set_param(0x1C3, 0)
clip_loop = [struct.unpack_from("<9H", IMG, t + 1 * 36 * 2 - 0x08040000) for t in (0x080C7BD4, 0x080C7D3C)]   # MK2 Pos list
check("Clip loop page: Loop Mode, Beat Count, Quant Size, Sync, Shift, Play Len, Stretch, Stretch Mode, Loop Fade",
      all(p == (0x6F, 0xB4, 0x2A, 0x76, 0x1C3, 0x1C4, 0x1C6, 0x1C7, 0x183) for p in clip_loop), clip_loop)
check("full pad record (type 3): 67 entries, capacity raised to 72", cnt3 == 67 and cap3 == 72 and ids3.get(0x1C8) == 0,
      (cnt3, cap3))





def segments(q, count, extra, unit, synced=1, sliced=0, stretch=0):
    set_param(0x1C6, stretch)
    p = engine_pad()
    e.w32(p + 0x240, count)
    e.w32(p + 0x244, extra)
    e.w32(p + 0x220, unit)
    e.w32(p + 0x224, 0)
    e.mu.mem_write(p + 0x1F5, bytes([synced]))
    e.mu.mem_write(p + 0x254, bytes([sliced]))
    set_param(0x1C4, q)
    e.call(SYM["clip_segments_thunk"], 0x11, 0x22, 0x33, 0x44, regs=[(UC_ARM_REG_R6, p)])
    r = [e.mu.reg_read(x) for x in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]
    set_param(0x1C4, 0)
    set_param(0x1C6, 0)
    return (r[1] + r[2]) & 0xFFFFFFFF, r, count          # as the firmware's `add r2, r1`


# a 4-bar clip at Sync 1/4: 16 segments of 960 ticks; the stock sum is +0x240 + +0x244
n, r, c = segments(0, 16, 0, 960)
check("Play Len Full: segment count unchanged; thunk keeps r0 / r3, r1 = +0x240", n == 16 and r[0] == 0x11 and
      r[3] == 0x44 and r[1] == 16, r)
check("Play Len 1 bar at Sync 1/4: 4 segments; 1/4: 1; 2 3/4: 11",
      segments(4, 16, 0, 960)[0] == 4 and segments(1, 16, 0, 960)[0] == 1 and segments(11, 16, 0, 960)[0] == 11)
check("Play Len 1 bar at Sync 1/16 (64 segments): 16; at Sync 1 bar (4 segments): 1",
      segments(4, 64, 0, 240)[0] == 16 and segments(4, 4, 0, 3840)[0] == 1)
check("Play Len 1 1/2 bars at Sync 1 bar: rounds to whole units (2)", segments(6, 4, 0, 3840)[0] == 2)
check("Play Len longer than the clip (8 bars of 4): full length", segments(32, 16, 0, 960)[0] == 16)
check("Play Len with +0x244 part (count 15 + 1): 1 bar = 4", segments(4, 15, 1, 960)[0] == 4)
check("Stretch: loop bound 16 -> 8 segments at +100 %, 32 at -100 %, 11 at +50 %; with Play Len 1 bar: 4",
      segments(0, 16, 0, 960, stretch=100)[0] == 8 and segments(0, 16, 0, 960, stretch=-100)[0] == 32
      and segments(0, 16, 0, 960, stretch=50)[0] == 11 and segments(4, 16, 0, 960, stretch=-100)[0] == 4)


def last_count(stretch, count=16):
    set_param(0x1C6, stretch)
    p = engine_pad()
    e.w32(p + 0x240, count)
    e.call(SYM["clip_last_thunk"], 0x11, 0x22, 0x33, 0x44, regs=[(UC_ARM_REG_R4, p)])
    r = [e.mu.reg_read(x) for x in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]
    set_param(0x1C6, 0)
    return r


check("Stretch: last-segment count (r3) 16 / 8 / 32, r0-r2 kept",
      last_count(0)[3] == 16 and last_count(100)[3] == 8 and last_count(-100) == [0x11, 0x22, 0x33, 32])
check("Play Len unchanged: sliced pad, Sync None (not synced), Sync Slice",
      segments(4, 16, 0, 960, sliced=1)[0] == 16 and segments(4, 16, 0, 960, synced=0)[0] == 16
      and segments(4, 16, 0, 0xF0000)[0] == 16)

# --- Sync 1/32: list registration, beats table in the clip param handler (real 0x080b7e10)
lists.clear()
e.stub(0x0806A27C, fake_register)
e.call(SYM["sync_register"], 0, 0x76, 0x080C203C, 0x080C58C0, stack=(7, 0x080C2044))
del e.stubs[0x0806A27C]
sl_ = lists.get(0x76)
sync_names = [e.cstr(e.r32(sl_[1] + 4 * k)) for k in range(sl_[2])] if sl_ else []
check("Sync list: stock 7 + 1/32 appended (old indexes keep their meaning)",
      sync_names == ["Slice", "1 bar", "1/2", "1/4", "1/8", "1/16", "None", "1/32"] and sl_[0] == "Sync:", sync_names)
check("Sync beats table: clip param handler's literal points at the 8-entry table",
      struct.unpack_from("<I", IMG, 0x080B860C - 0x08040000)[0] == SYM["sync_beats"])


def sync_ticks(index):
    pad_ = engine_pad()
    msg_ = e.new(0x20)
    e.w32(msg_ + 0xC, 0x76)
    e.w32(msg_ + 0x10, index)
    e.stub(0x080B5224, lambda uc: None)
    e.stub(0x080B5624, lambda uc: None)
    e.call(0x080B7E10, pad_, 0, msg_, 0)
    del e.stubs[0x080B5224], e.stubs[0x080B5624]
    return struct.unpack("<q", e.mu.mem_read(pad_ + 0x220, 8))[0]


got = [sync_ticks(i) for i in range(8)]
check("Sync -> ticks (real handler): Slice 0xf0000, 1 bar 3840 .. 1/16 240, None 0, 1/32 120",
      got == [0xF0000, 3840, 1920, 960, 480, 240, 0, 120], got)

# --- parameter page: Play Len / Stretch hidden unless synced (page builder stubbed, real record + list removal)
def page_ids(quant, sync):
    set_param(0x2A, quant)
    set_param(0x76, sync)
    lst = e.new(4 + 12 * 0x30)

    def fill(uc):
        ids = [0x6F, 0xB4, 0x2A, 0x76, 0x1C3, 0x1C4, 0x1C6, 0x1C7]
        e.w32(lst, len(ids))
        for k, pid in enumerate(ids):
            e.mu.mem_write(lst + 4 + 12 * k, struct.pack("<HHHHI", pid, 0, 0, 0, 0))
        uc.reg_write(UC_ARM_REG_R0, 1)
    e.stub(0x08078EC8, fill)
    m = e.new(8)
    e.w32(m, 0x080C05EC)
    e.mu.mem_write(m + 4, struct.pack("<H", 0x12))
    r = e.call(SYM["clip_page"], 0x24015170, m, lst, 0)
    del e.stubs[0x08078EC8]
    n = e.r32(lst)
    return r, [struct.unpack_from("<H", e.mu.mem_read(lst + 4 + 12 * k, 2))[0] for k in range(n)]


full = [0x6F, 0xB4, 0x2A, 0x76, 0x1C3, 0x1C4, 0x1C6, 0x1C7]
short = [0x6F, 0xB4, 0x2A, 0x76, 0x1C3]
check("page: synced (Quant 1 bar, Sync 1/4): Play Len and Stretch shown; builder's result passed on",
      page_ids(3, 3) == (1, full), page_ids(3, 3))
check("page: Sync 1/32 counts as synced", page_ids(3, 7)[1] == full)
check("page: Quant None, Sync None or Slice: Play Len and Stretch hidden, Shift stays",
      page_ids(8, 3)[1] == short and page_ids(3, 6)[1] == short and page_ids(3, 0)[1] == short)

# --- real clip setup 0x080b5224 (fake streamed source: 384000 frames at 48 kHz = 16 beats at 120 BPM)
vcode = e.new(0x40)
for k in range(0, 0x40, 2):
    e.mu.mem_write(vcode + k, b"\x70\x47")                    # bx lr
vt = e.new(0x40)
vfns = {0x14: lambda uc: e.mu.mem_write(uc.reg_read(UC_ARM_REG_R0), struct.pack("<q", 384000)),
        0x18: lambda uc: uc.reg_write(UC_ARM_REG_R0, 48000), 0x20: lambda uc: uc.reg_write(UC_ARM_REG_R0, 2)}
for off in range(0, 0x40, 4):
    e.w32(vt + off, (vcode + off) | 1)
    e.stub(vcode + off, vfns.get(off, lambda uc: uc.reg_write(UC_ARM_REG_R0, 0)))
vsrc = e.new(0x40)
e.w32(vsrc, vt)
vstream = e.new(0x40)
e.w32(vstream + 0x20, vsrc)
e.w32(vstream + 0x24, 0x77)


def setup(stretch, beatcount=16):
    set_param(0x1C6, stretch)
    p_ = engine_pad()
    e.w32(p_ + 0x24, vstream)
    e.w32(p_ + 0xB80, beatcount)
    e.mu.mem_write(p_ + 0x218, struct.pack("<qq", 3840, 960))   # Quant 1 bar, Sync 1/4
    e.mu.mem_write(p_ + 0x1F5, b"\x01")
    fp_ = e.new(0x40)
    e.wf(fp_ + 0x18, [120.0])
    e.call(0x080B5224, p_, fp_)
    set_param(0x1C6, 0)
    f = lambda o: e.rf(p_ + o, 1)[0]
    return round(f(0x1FC)), round(f(0x204)), e.r32(p_ + 0x210), round(f(0x258))


check("clip setup (stock): 16 segments of 24000 file / 24000 output frames, clip tempo 120; Stretch leaves it alone",
      setup(0) == setup(100) == setup(0, 0) == (24000, 24000, 16, 120), (setup(0), setup(100), setup(0, 0)))


def seg_start(stretch, k, count=None):
    """real segment start 0x080b5a34 after the real setup: (slot file start, prefetch seek, output frames to the end)"""
    seeks = []
    e.stub(0x080A59CC, lambda uc: seeks.append(uc.reg_read(UC_ARM_REG_R1)))
    set_param(0x1C6, 0)
    p_ = engine_pad()
    e.w32(p_ + 0x24, vstream)
    e.w32(p_ + 0xB80, 16)
    e.mu.mem_write(p_ + 0x218, struct.pack("<qq", 3840, 960))
    e.mu.mem_write(p_ + 0x1F5, b"\x01")
    fp_ = e.new(0x40)
    e.wf(fp_ + 0x18, [120.0])
    e.call(0x080B5224, p_, fp_)
    set_param(0x1C6, stretch)
    del seeks[:]
    slot = e.call(0x080B5A34, p_, e.new(0x400), 0, k, stack=(fp_, 0, 0))
    del e.stubs[0x080A59CC]
    base = p_ + 0x30 + 0x70 * slot
    r = (struct.unpack("<q", e.mu.mem_read(base + 8, 8))[0], seeks, e.r32(base + 0x28))
    set_param(0x1C6, 0)
    return r


check("segment start (real 0x080b5a34), Stretch 0: segment 3 at 72000, prefetch 71992, 23744 output frames",
      seg_start(0, 3) == (72000, [71992], 23744), seg_start(0, 3))
check("segment start, Stretch +100 %: segment 3 at 144000 (double speed), same output length",
      seg_start(100, 3) == (144000, [143992], 23744), seg_start(100, 3))
check("segment start, Stretch -100 %: segment 3 at 36000 (half speed); +50 %: 108000",
      seg_start(-100, 3)[0] == 36000 and seg_start(50, 3)[0] == 108000, (seg_start(-100, 3), seg_start(50, 3)))

# --- new parameter ids: the stock code must not use them itself (0x1c5 is taken by the parameter command, 0x0807f7e2)
from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs  # noqa: E402
_md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
_md.skipdata = True
_ours = {f"#{v:#x}" for v in (0x1C0, 0x1C1, 0x1C2, 0x1C3, 0x1C4, 0x1C6, 0x1C7, 0x1C8, *range(0x191, 0x19C),
                               0x1A1, 0x1A2, 0x1A3, *range(0x1A5, 0x1AA), 0x1AB, 0x1AC,
                               0x19D, 0x19E, 0x19F, 0x17D, 0x17F, 0x183)}
_uses = [hex(i.address) for i in _md.disasm(IMG[:0x080BD100 - 0x08040000], 0x08040000)   # code, not the data after it
         if i.address not in (0x08042DB8,)     # MK2: movw r2, #0x197 in the LTDC panel timing set-up (320 x 240), no id
         if i.mnemonic in ("movs", "movw", "mov.w", "cmp", "cmp.w") and "sp" not in i.op_str
         and any(i.op_str.endswith(v) for v in _ours)]
check("new parameter ids 0x1c0-0x1c4, 0x1c6-0x1c8, 0x191-0x19b, 0x1a1-0x1a3, 0x1a5-0x1a9, 0x1ab, 0x1ac, 0x19d-0x19f, 0x17d, 0x17f, 0x183: no immediate use in the stock code", not _uses, _uses)

# --- Loop Fade (0x183): a dip around the loop's seams, on the frames as read (same stream: value = index, 1000 frames)
def fade_read(pos, n):
    dst = e.new(4 * n)
    e.call(SYM["clip_read1_thunk"], stream, dst, n, pos, stack=(0,), regs=[(UC_ARM_REG_R4, cpad)])
    return e.rf(dst, n)
def gain_at(q):
    v = fade_read(q, 1)[0]
    return v / q
set_param(0x1C3, 0)
set_param(0x183, 0)
check("Loop Fade 0: reads unchanged", fade_read(0, 64) == [float(i) for i in range(64)])
set_param(0x183, 2)                                        # 2 ms = 96 frames (unsynced pad: loop end = file end)
g = {q: round(gain_at(q), 3) for q in (24, 48, 95, 96, 500, 903, 952, 975)}
print("     Loop Fade 2 ms, gains:", g)
check("Loop Fade 2 ms: fade in over the first 96 frames (sine), out over the last 96, middle untouched",
      abs(g[48] - 0.707) < 0.01 and g[24] < g[48] < g[95] <= 1.0 and g[96] == g[500] == g[903] == 1.0
      and abs(g[952] - 0.707) < 0.02 and g[975] < g[952] and fade_read(999, 1)[0] < 1.0, g)
set_param(0x1C3, 5)                                        # Shift 5 ms = 240 frames: the file's seam at logical 760
out = fade_read(700, 120)
seam = {q: out[q - 700] / ((q + 240) % 1000) for q in (712, 759, 761, 808, 819)}
check("Loop Fade with Shift: dip around the file's seam (logical 760), outside it untouched",
      seam[759] < 0.03 and seam[761] < 0.03 and abs(seam[712] - 0.707) < 0.03 and abs(seam[808] - 0.707) < 0.03
      and seam[819] < 1.0, {k: round(v, 3) for k, v in seam.items()})
set_param(0x1C3, 0)
set_param(0x183, 0)

# --- smooth time stretch (WSOLA, clip_render in place of 0x080b66d4), real stream reads on an in-memory mono stream
SR = 48000
FILE_N = 96000
wsrc = e.new(0x40)
wplane = e.new(4 * FILE_N)
e.wf(wplane, [0.8 * math.sin(2 * math.pi * 1000 * i / SR) for i in range(FILE_N)])
e.w32(wsrc + 4, wplane)
e.w32(wsrc + 8, wplane)
e.w32(wsrc + 0x10, FILE_N)


def wbuf(cap):
    b = e.new(0x20)
    e.w32(b, cap)
    e.w32(b + 4, cap)
    e.w32(b + 8, e.new(4 * cap))
    e.w32(b + 0xC, e.new(4 * cap))
    return b


def render(stretch, mode=0, rate=1.0, blocks=120, n=128, seglen=24000.0, unit=24000.0, start=4000, slot=1, jumps=None,
           changes=None, calls=None):
    set_param(0x1C6, stretch)
    set_param(0x1C7, mode)
    e.w32(0x2406C000 + ((1 * 4 + 2) * 4 + slot) * 40, 0)      # this slot's grain state: fresh (time stands still here)
    p_ = engine_pad()
    e.w32(p_ + 0x24, wsrc)
    e.wf(p_ + 0x1FC, [seglen])
    e.wf(p_ + 0x204, [unit])
    e.mu.mem_write(p_ + 0x1F5, b"\x01")
    frac_p = p_ + 0x30 + 0x70 * slot + 0x1C
    e.wf(frac_p, [0.0])
    src_b, out_b = wbuf(600), wbuf(600)
    cons, res = e.new(4), e.new(4)
    pos, out, steps = start, [], []
    for blk in range(blocks):
        if jumps and blk in jumps:                                 # the stock sets the next segment's position
            pos = jumps[blk] if jumps[blk] >= 0 else pos - jumps[blk]
        if changes and blk in changes:                             # Stretch turned while playing
            set_param(0x1C6, changes[blk])
        ok = e.call(SYM["clip_render"], p_, 0, pos & 0xFFFFFFFF, 0, s=(rate,),
                    stack=(n, cons, frac_p, src_b, out_b, res))
        rb = e.r32(res)
        out += e.rf(e.r32(rb + 8), n)
        c = struct.unpack("<i", e.mu.mem_read(cons, 4))[0]
        steps.append(c)
        pos += c
    set_param(0x1C6, 0)
    set_param(0x1C7, 0)
    return out, pos - start, ok


def freq(sig):
    z = sum(1 for a, b in zip(sig, sig[1:]) if a < 0 <= b)
    return z * SR / len(sig)


def rms(sig):
    return math.sqrt(sum(v * v for v in sig) / len(sig))


o0, adv0, ok0 = render(0, mode=1)                                  # Jump: the stock path
check("Stretch Mode Jump: stock read (1 kHz, advances 1 frame per frame)", ok0 == 1 and abs(freq(o0[256:]) - 1000) < 30
      and abs(adv0 - 120 * 128) <= 2, (freq(o0[256:]), adv0))
o1, adv1, ok1 = render(100)                                        # Auto + Stretch +100 %: smooth, double speed
check("Smooth +100 %: file advances 2 frames per output frame (double tempo)", abs(adv1 - 2 * 120 * 128) <= 2, adv1)
check("Smooth +100 %: pitch unchanged (1 kHz), level steady", abs(freq(o1[1024:]) - 1000) < 30
      and abs(rms(o1[1024:]) - 0.8 / math.sqrt(2)) < 0.08, (freq(o1[1024:]), rms(o1[1024:])))
check("Smooth +100 %: no clicks (sample-to-sample step within a sine's)",
      max(abs(a - b) for a, b in zip(o1[1024:], o1[1025:])) < 0.2, max(abs(a - b) for a, b in zip(o1[1024:], o1[1025:])))
o2, adv2, _ = render(-50)
check("Smooth -50 %: 2/3 tempo, pitch unchanged", abs(adv2 - 120 * 128 / 1.5) <= 2 and abs(freq(o2[1024:]) - 1000) < 30,
      (adv2, freq(o2[1024:])))
o3, adv3, _ = render(0, mode=2, seglen=36000.0)                     # Smooth at 0: follows a song tempo 1.5 x the clip's
check("Smooth, Stretch 0, clip slower than the song (segment 36000 file / 24000 out): 1.5 x tempo, same pitch",
      abs(adv3 - 1.5 * 120 * 128) <= 2 and abs(freq(o3[1024:]) - 1000) < 30, (adv3, freq(o3[1024:])))
o4, adv4, _ = render(0, mode=2, rate=2.0)
check("Smooth, pitch rate 2 (an octave up): 2 kHz, tempo still from the segments (1 x)",
      abs(freq(o4[1024:]) - 2000) < 60 and abs(adv4 - 120 * 128) <= 2, (freq(o4[1024:]), adv4))
o5, adv5, _ = render(100, mode=1)
check("Stretch +100 % in Jump mode: stock read (pitch and advance 1:1 within the segment)",
      abs(adv5 - 120 * 128) <= 2 and abs(freq(o5[256:]) - 1000) < 30)

# --- audio load meter: DWT counter driven by the stubs (1000 + 3000 busy, 10000 per block -> 40 %)
cyc = [0]
def tick(k):
    def f(uc):
        cyc[0] += k
        e.w32(0xE0001004, cyc[0])
    return f
e.stub(0x080952D8, tick(1000))
e.stub(0x08095364, tick(3000))
for _ in range(400):
    cyc[0] += 6000
    e.w32(0xE0001004, cyc[0])
    e.call(SYM["load_in"], 0, 0, 0)
    e.call(SYM["load_out"], 0, 0, 0, 0)
del e.stubs[0x080952D8], e.stubs[0x08095364]
texts = []
e.stub(0x0805ff6e, lambda uc: texts.append(e.cstr(uc.reg_read(UC_ARM_REG_R1))))
lw = e.new(0x60)
e.mu.mem_write(lw + 4, struct.pack("<4i", 80, 222, 48, 20))
e.call(SYM["load_text"], lw, 0)
del e.stubs[0x0805ff6e]
check("load meter: 4000 of 10000 cycles per block -> 40 %; DWT enabled (TRCENA, CYCCNTENA)",
      texts == ["40%"] and e.r32(0xE000EDFC) & (1 << 24) and e.r32(0xE0001000) & 1, texts)
check("load text moved to the right end of the status bar (x 280, w 39)",
      struct.unpack("<4i", e.mu.mem_read(lw + 4, 16)) == (280, 222, 39, 20))
check("load hooks: idle text -> load_text; MK2: '%dMB' and FULL -> load_text_mb",
      bl_target(IMG[0x080714f0 - 0x08040000:0x080714f4 - 0x08040000], 0x080714f0) == SYM["load_text"] & ~1
      and bl_target(IMG[0x080714c6 - 0x08040000:0x080714ca - 0x08040000], 0x080714c6) == SYM["load_text_mb"] & ~1
      and bl_target(IMG[0x080714e4 - 0x08040000:0x080714E8 - 0x08040000], 0x080714e4) == SYM["load_text_mb"] & ~1)
texts.clear()
e.stub(0x0805ff6e, lambda uc: texts.append(e.cstr(uc.reg_read(UC_ARM_REG_R1))))
mw = e.new(0x60)
e.mu.mem_write(mw + 4, struct.pack("<4i", 80, 222, 48, 20))
mbtext = e.new(16)
e.mu.mem_write(mbtext, b"245MB\0")
e.call(SYM["load_text_mb"], mw, mbtext)
e.mu.mem_write(mbtext, b"FULL\0")
e.call(SYM["load_text_mb"], mw, mbtext)
del e.stubs[0x0805ff6e]
check("MK2 load_text_mb: '245MB' -> '245M  40%', 'FULL' -> 'FULL  40%', rect widened to 120 at x 80",
      texts == ["245M  40%", "FULL  40%"] and struct.unpack("<4i", e.mu.mem_read(mw + 4, 16)) == (80, 222, 120, 20), texts)

# --- machine modes after the voice
for name in ("SP1200", "SP-12", "S950", "MPC60", "SP Raw"):
    set_interp(NAMES.index(name))
    obj = filter_obj(0.0)
    y = sine(997)
    b, L, R = buffer(y, y)
    run_a(obj, b, pad)
    out = e.rf(L, N)
    finite = all(math.isfinite(v) for v in out)
    changed = any(abs(p - q) > 1e-6 for p, q in zip(out, y))
    check(f"{name}: output finite and processed", finite and changed)
set_interp(NAMES.index("SP Raw"))
obj = filter_obj(0.0)
b, L, R = buffer(x, x)
run_a(obj, b, pad)
out = e.rf(L, N)
check("SP Raw: 12-bit hold, no output filter", all(abs(v * 2048 - round(v * 2048)) < 1e-4 for v in out))

# --- voice-start reset (stock 0x080b98bc) between blocks: no stale state, no NaN
set_interp(0)
obj = filter_obj(-0.6, res=0.9)
for k in range(4):
    b, L, R = buffer(sine(300, ph=k * N), sine(300, ph=k * N))
    run_a(obj, b, pad)
e.call(0x080B98BC, obj)
check("reset clears the 'on' flag (+0x40)", e.r32(obj + 0x40) == 0)
b, L, R = buffer([0.0] * N, [0.0] * N)
e.mu.mem_write(b + 0x10, bytes([0, 1]))
z = [1e-9] * N
b, L, R = buffer(z, z)
run_a(obj, b, pad)
check("after reset: silent input stays silent (integrators re-zeroed)", max(abs(v) for v in e.rf(L, N) + e.rf(R, N)) < 1e-6)

# --- kernels: without a machine, read_normal / read_hq give exactly the stock kernels' output
src = e.new(4 * 300)
e.wf(src, [math.sin(i * 0.37) for i in range(300)])


def kernel(fn, rate, n=64):
    newpos, frac, dst = e.new(4), e.new(4), e.new(4 * n)
    e.wf(frac, [0.25])
    e.call(fn, src, 300, 8, newpos, s=(rate,), stack=(frac, dst, n))
    return e.rf(dst, n), e.r32(newpos), e.rf(frac, 1)[0]


e.w32(PARK, 0)
for wrap, stock in (("read_normal", 0x080A27A8), ("read_hq", 0x080A283C)):
    check(f"{wrap}, no machine: identical to stock kernel", kernel(SYM[wrap], 1.37) == kernel(stock, 1.37))

set_interp(NAMES.index("SP1200"))
e.w32(PARK, pad)
e.w32(PARK + 4, 1000)
e.wf(PARK + 8, [1.37])
out, newpos, frac = kernel(SYM["read_normal"], 1.37)
check("read_normal, SP1200: machine reader, finite output", all(math.isfinite(v) and abs(v) < 2 for v in out))
check("read_normal, SP1200: position advances by rate x n", abs((newpos - 8 + frac - 0.25) - 1.37 * 64) < 1e-3,
      (newpos, frac))
check("read_normal, SP1200: reads on the 26.04 kHz grid (repeats samples)", len(set(out)) < 64)

# --- voice_read wrapper parks pad / position / rate and forwards everything to the stock reader
got = {}


def fake_reader(uc):
    sp = uc.reg_read(UC_ARM_REG_SP)
    got.update(r=[uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)],
               stack=[e.r32(sp + 4 * k) for k in range(4)],
               s=[struct.unpack("<f", struct.pack("<I", uc.reg_read(r)))[0] for r in (UC_ARM_REG_S0, UC_ARM_REG_S1)])
    uc.reg_write(UC_ARM_REG_R0, 77)


e.stub(0x080AECA8, fake_reader)
r = e.call(SYM["voice_read"], pad, 0x11, 1234, 0x22, s=(0.5, 1.0), stack=(0x33, 0x44, 0x55, 0x66))
check("voice_read forwards r0-r3, four stack words, frac, and its return value",
      got["r"] == [pad, 0x11, 1234, 0x22] and got["stack"] == [0x33, 0x44, 0x55, 0x66] and got["s"][0] == 0.5 and r == 77,
      got)
check("voice_read: machine pad at rate 1.0 goes in just above 1.0", 1.0 < got["s"][1] < 1.000001, got["s"])
check("voice_read parks pad, position and rate", e.r32(PARK) == pad and e.r32(PARK + 4) == 1234)
set_interp(0)
e.call(SYM["voice_read"], pad, 0x11, 1234, 0x22, s=(0.5, 1.0), stack=(0x33, 0x44, 0x55, 0x66))
check("voice_read: Normal pad keeps rate exactly 1.0", got["s"][1] == 1.0)

# --- pad settings page (page 5) of every pad-mode table lists Interp right after Excl Group
TABLES = {"Sample": (0x080C84F4, 0x080C865C, 0x080C87C4), "Clip": (0x080C7BD4, 0x080C7D3C),
          "Slicer": (0x080C8A04, 0x080C8B6C), "Granular": (0x080C7F3C, 0x080C80A4)}
for mode, tables in TABLES.items():
    ok = True
    for t in tables:
        page = struct.unpack_from("<36H", IMG, t + 4 * 36 * 2 - BASE)       # MK2: Conf list, page 4 of 5 x 36
        ids = list(page[:page.index(0)])
        ok &= P_INTERP in ids and ids.index(P_INTERP) == ids.index(0xA6) + 1 and len(set(ids)) == len(ids)
    check(f"{mode}: Fidelity on the pad settings page after Excl Group", ok)

# --- turning Stretch while playing: the next segment's position jumps; spliced, not cut (pkZC)
def max_step(sig, a, b):
    return max(abs(sig[k] - sig[k - 1]) for k in range(a, b))
SIN_STEP = 2 * math.pi * 1000 / SR * 0.8                         # largest sample step of the 0.8 x 1 kHz source
oj, _, _ = render(40, mode=2, blocks=80, jumps={40: 4000 + 40 * 128 + 7333}, changes={40: -30})
step_j = max_step(oj, 40 * 128 - 8, 40 * 128 + 3 * 512)
step_n = max_step(oj, 512, 40 * 128 - 8)
print(f"     jump while playing: largest sample step {step_j:.3f} (before the jump {step_n:.3f}, sine {SIN_STEP:.3f})")
check("WSOLA, Stretch turned + the segment position jumps while playing: no click (steps stay those of the source)",
      step_j < 1.5 * SIN_STEP and step_n < 1.5 * SIN_STEP, (step_j, step_n))
os_, _, _ = render(40, mode=2, blocks=60, jumps={40: 10})
check("WSOLA, jump to the clip's start (retrigger / loop end): restarted at once (one grain at full weight)",
      abs(os_[40 * 128] - 0.8 * math.sin(2 * math.pi * 1000 * 10 / SR)) < 0.05, os_[40 * 128])
stock_calls = [0]
_orig = e.stubs.get(0x080B66D4)
e.stub(0x080B66D4, lambda uc: (stock_calls.__setitem__(0, stock_calls[0] + 1), uc.reg_write(UC_ARM_REG_R0, 0)))
oa, _, _ = render(50, mode=0, blocks=40, changes={20: 0})
del e.stubs[0x080B66D4]
check("Stretch Mode Auto: stays smooth when Stretch is turned to 0 while playing (no switch to the stock read)",
      stock_calls[0] == 0 and max_step(oa, 512, len(oa)) < 1.5 * SIN_STEP, (stock_calls[0], max_step(oa, 512, len(oa))))

print(f"\n{'FAILED: ' + str(failures) if failures else 'all passed'}")
sys.exit(1 if failures else 0)
