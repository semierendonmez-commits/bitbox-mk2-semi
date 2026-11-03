"""Unicorn tests for patches/fx3.py (src/fx3.c): memory init and the FX3 record, the pad send checks and the pad mix
onto bus 0xc (real buffer and mix functions), and the effect in the block driver (bus 0xc -> reverb at 29.8 kHz ->
bus 0) with the real plate core and the Hall type.

  python3 -I tests/test_fx3.py      (after ./build.sh and patch.py fidelity pages padpages sound reverb fx3 cave)
"""
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_src = open(os.path.join(HERE, "test_fidelity.py")).read()
exec(compile(_src[:_src.index("\ne = Emu()\n")], "test_fidelity.py (header)", "exec"))   # Emu, check, IMG, SYM, ...

from unicorn.arm_const import UC_ARM_REG_R4, UC_ARM_REG_R9, UC_ARM_REG_S15  # noqa: E402

e = Emu()
e.stub(0x08042DFC, lambda uc: uc.reg_write(UC_ARM_REG_R0, e.new(uc.reg_read(UC_ARM_REG_R0))))   # DTCM allocator
APP = 0x24013CB0
REC = APP + 0x2200 + 3 * 0x18
F3 = 0x2001C200
N = 32

# pad records (FX3 Send lives there)
typ = e.new(4)
e.mu.mem_write(typ, b"\x45")
for row in range(4):
    for col in range(4):
        e.call(0x08089FC4, APP + 0x1840 + row * 0xF0 + col * 0x30, typ)

def set_pad(pid, v, row=1, col=2):
    e.call(0x080894FA, APP + 0x1840 + row * 0xF0 + col * 0x30, pid, v)

# --- init: too little heap -> nothing; enough -> plate state, pre-delay, record
e.stub(0x08042E44, lambda uc: uc.reg_write(UC_ARM_REG_R0, 2 * 1024 * 1024))
e.call(SYM["fx3_init"])
check("fx3_init with 2 MB free: no memory taken, FX3 off; the FX3 record is made anyway",
      e.r32(F3) != 0x46583321 or e.r32(F3 + 4) == 0)
cnt = struct.unpack("<H", e.mu.mem_read(REC + 0xA, 2))[0]
ent = e.r32(REC + 4)
ids = [struct.unpack("<H", e.mu.mem_read(ent + 8 * k, 2))[0] for k in range(cnt)]
check("FX3 record (FX row 3, app +0x2248): a reverb record (Type, Decay, Predelay, Damping, 11 settings)",
      ids == [0x178, 0x1A, 0x1E, 0x1D] + list(range(0x191, 0x19C)), ids)
e.stub(0x08042E44, lambda uc: uc.reg_write(UC_ARM_REG_R0, 32 * 1024 * 1024))
e.call(SYM["fx3_init"])
plate = e.r32(F3 + 4)
check("fx3_init with 32 MB free: plate state with its 12 buffers, pre-delay ring",
      e.r32(F3) == 0x46583321 and plate and all(e.r32(plate + 4 * w) for w in (8, 11, 14, 17, 20, 25, 30, 33, 37, 39, 41, 43))
      and e.r32(F3 + 8))

# --- engine with real buffers on the buses we use
engine = e.new(0x16000)
def bus(i):
    return engine + 0x150E4 + 20 * i
for i in (0, 0xC, 0xD, 0xE):
    e.call(0x080A310C, bus(i), 128)
def clear_buses():
    for i in (0, 0xC, 0xD, 0xE):
        e.call(0x080A31D4, bus(i))
        e.call(0x080A31C8, bus(i), N)
def planes(b):
    return e.r32(b + 8), e.r32(b + 0xC)

# --- the "any send on" check
pad = e.new(0x700)
e.w32(pad + 0x18, 1 << 8 | 2)
f1 = e.new(4)
def check_a(fx1, fx3):
    e.wf(f1, [fx1])
    set_pad(0x1C8, fx3)
    e.call(SYM["fx3_check_a"], regs=[(UC_ARM_REG_R6, f1), (UC_ARM_REG_R4, pad)])
    return round(struct.unpack("<f", struct.pack("<I", e.mu.reg_read(UC_ARM_REG_S15)))[0], 4)
check("send check: FX1 0 + FX3 0 -> 0; FX1 0 + FX3 on -> 1.0; FX1 0.3 -> 0.3",
      check_a(0.0, 0) == 0.0 and check_a(0.0, 500) == 1.0 and check_a(0.3, 500) == 0.3,
      (check_a(0.0, 0), check_a(0.0, 500), check_a(0.3, 500)))

# --- the pad's final mix: scratch -> bus 0xc (x FX3 gain) and -> its output
clear_buses()
scratch, out = e.new(0x20), e.new(0x20)
e.call(0x080A310C, scratch, 128)
e.call(0x080A310C, out, 128)
e.call(0x080A31C8, scratch, N)
e.call(0x080A31C8, out, N)
sl, sr = e.new(4), e.new(4)
e.call(0x080A32D4, scratch, sl, sr)
e.wf(e.r32(sl), [0.5] * N)
e.wf(e.r32(sr), [0.25] * N)
e.call(0x080A31D4, out)
e.call(0x080A31C8, out, N)
set_pad(0x1C8, 500)
e.call(SYM["fx3_mix_a"], out, scratch, regs=[(UC_ARM_REG_R4, pad), (UC_ARM_REG_R9, engine)])
e.call(0x080A04C0, s=(0.5,))
g = struct.unpack("<f", struct.pack("<I", e.mu.reg_read(UC_ARM_REG_S0)))[0]
bl_, br_ = planes(bus(0xC))
ol, orr = planes(out)
check("pad mix: bus 0xc gets scratch x FX3 gain (stock send curve), the output gets the scratch",
      abs(e.rf(bl_, 1)[0] - 0.5 * g) < 1e-6 and abs(e.rf(br_, 1)[0] - 0.25 * g) < 1e-6 and abs(e.rf(ol, 1)[0] - 0.5) < 1e-6,
      (e.rf(bl_, 1), g, e.rf(ol, 1)))
set_pad(0x1C8, 0)

# --- the effect in the block driver
def run_fx3(blocks, impulse=True, rtype=0):
    e.call(0x080894FA, REC, 0x178, rtype)
    L, R = [], []
    for k in range(blocks):
        clear_buses()
        if impulse and k == 0:
            il, ir = e.new(4), e.new(4)
            e.call(0x080A32D4, bus(0xC), il, ir)
            e.wf(e.r32(il), [1.0] + [0.0] * (N - 1))
            e.wf(e.r32(ir), [1.0] + [0.0] * (N - 1))
        e.call(SYM["fx3_master"], bus(0), bus(0xE))
        if e.mu.mem_read(bus(0) + 0x10, 1)[0]:
            L += [0.0] * N
            R += [0.0] * N
        else:
            pl, pr = planes(bus(0))
            L += e.rf(pl, N)
            R += e.rf(pr, N)
    return L, R

for name, t in (("Plate", 0), ("Hall", 2)):
    e.w32(F3 + 0x30, 0)                                         # idle counter: active
    L, R = run_fx3(int(48000 * 1.5) // N, rtype=t)
    early = sum(v * v for v in L[: 48000 // 4])
    late = sum(v * v for v in L[48000: 48000 * 3 // 2])
    print(f"     FX3 {name}: energy first 250 ms {early:.4f}, 1.0-1.5 s {late:.6f}, peak {max(abs(v) for v in L + R):.3f}")
    check(f"FX3 {name}: impulse on bus 0xc -> reverb on bus 0 (48 kHz), finite, decaying, stereo",
          all(math.isfinite(v) for v in L + R) and early > 0 and 0 < late < early and L != R)

# idle: silent bus and the run-on used up -> bus 0 untouched
e.w32(F3 + 0x30, 10 ** 6)
L, R = run_fx3(4, impulse=False)
check("FX3 idle after its run-on: bus 0 left alone (stays silent)", all(v == 0.0 for v in L + R))

# --- row 3 is a reverb record in the image: the eight stock inits' type byte (movs r3, #0 -> #0x44)
sites = [0x08078180, 0x0807ACC4, 0x0807ACEA, 0x0807C3DE, 0x08080F74, 0x0808AE36, 0x0808B2BE, 0x0808E2F0]
check("FX row 3 (app +0x2248) inits: type 0x44 at all eight sites",
      all(IMG[a - 0x08040000: a - 0x08040000 + 2] == bytes.fromhex("4423") for a in sites))

# --- right arrow on the FX cell page (screen 0x23): FX3 -> master EQ (cell 0x320, page 0x23); else stock (0x14)
from unicorn.arm_const import UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3  # noqa: E402
calls = []
def _screen(uc):
    calls.append(("screen", uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1), uc.reg_read(UC_ARM_REG_R2),
                  uc.reg_read(UC_ARM_REG_R3)))
e.stub(0x0808EEDC, _screen)
e.stub(0x0805B18C, lambda uc: calls.append(("notify", struct.unpack("<H", uc.mem_read(uc.reg_read(UC_ARM_REG_R1), 2))[0])))
e.mu.mem_write(APP + 0x28, struct.pack("<H", 0x330))
e.call(SYM["fx3_right"], APP, 0x14, 0, 0)
check("right arrow on FX3's page: EQ cell selected (app +0x28 = 0x320, notification 0x112), FX cell page 0x23",
      e.r16(APP + 0x28) == 0x320 and calls == [("notify", 0x112), ("screen", APP, 0x23, 0, 0)], calls)
calls.clear()
e.call(SYM["fx3_right"], APP, 0x14, 0, 0)
check("right arrow on the EQ's / RVB's page: stock (screen 0x14)", calls == [("screen", APP, 0x14, 0, 0)], calls)

# --- recording: sends survive the pad record's rebuild (rec_reinit at 0x0807b59e)
REC_PAD = APP + 0x1840 + 3 * 0xF0 + 3 * 0x30
t45 = e.new(4)
e.mu.mem_write(t45, b"\x45")
e.call(0x08089FC4, REC_PAD, t45)
for pid, v in ((0xBB, 300), (0xBC, 700), (0x1C8, 500), (0x1C2, 400)):
    e.call(0x080894FA, REC_PAD, pid, v)
t3 = e.new(4)
e.mu.mem_write(t3, b"\x03")
e.call(SYM["rec_reinit"], REC_PAD, t3)
get = lambda pid: e.call(0x080894D0, REC_PAD, pid)
check("rec_reinit: full pad record (67 entries), FX1 / FX2 / FX3 sends kept (300 / 700 / 500), Drive reset (0)",
      e.r16(REC_PAD + 0xA) == 67 and (get(0xBB), get(0xBC), get(0x1C8), get(0x1C2)) == (300, 700, 500, 0),
      (e.r16(REC_PAD + 0xA), get(0xBB), get(0xBC), get(0x1C8), get(0x1C2)))
check("patch: 0x0807b59e calls rec_reinit; the input node's vtable entry 0x080ddfb4 -> mon_node",
      struct.unpack_from("<I", IMG, 0x080DDFB4 - 0x08040000)[0] == SYM["mon_node"] | 1)

# --- the monitor through the selected pad's sends (mon_node around the input node 0x080a4d1c)
mon_obj = e.new(0x100)
e.w32(mon_obj + 0x68, 0)                                    # Mon Output: bus 0
MON_SIG = 0.25
def _input_node(uc):                                        # stand-in node: adds MON_SIG to bus 0 (memory only:
    b = bus(0)                                              #  no nested e.call inside a hook)
    l, r = struct.unpack("<II", uc.mem_read(b + 8, 8))
    if uc.mem_read(b + 0x10, 1)[0]:
        uc.mem_write(l, b"\0" * 4 * N)
        uc.mem_write(r, b"\0" * 4 * N)
        uc.mem_write(b + 4, struct.pack("<I", N))
        uc.mem_write(b + 0x10, bytes([0, 1]))
    for pl in (l, r):
        v = struct.unpack(f"<{N}f", uc.mem_read(pl, 4 * N))
        uc.mem_write(pl, struct.pack(f"<{N}f", *(x + MON_SIG for x in v)))
    uc.reg_write(UC_ARM_REG_R0, 1)
e.stub(0x080A4D1C, _input_node)
def mon_block(fx1, fx2, fx3, pre=0.1):
    e.call(0x080894FA, APP + 0x1840, 0xBB, fx1)            # selected pad: row 0, col 0 (app +0x14 = 0)
    e.call(0x080894FA, APP + 0x1840, 0xBC, fx2)
    e.call(0x080894FA, APP + 0x1840, 0x1C8, fx3)
    e.mu.mem_write(APP + 0x14, struct.pack("<H", 0))
    clear_buses()
    il, ir = e.new(4), e.new(4)
    e.call(0x080A32D4, bus(0), il, ir)                      # something already on bus 0 (a pad)
    e.wf(e.r32(il), [pre] * N)
    e.wf(e.r32(ir), [pre] * N)
    r = e.call(SYM["mon_node"], mon_obj, engine)
    out = {}
    for i in (0, 0xC, 0xD, 0xE):
        if e.mu.mem_read(bus(i) + 0x10, 1)[0]:
            out[i] = 0.0
        else:
            pl, pr = planes(bus(i))
            out[i] = e.rf(pl, 1)[0]
    return r, out
r, o = mon_block(0, 0, 0)
check("mon_node, sends 0: the node only (bus 0 = pad + monitor), FX buses untouched",
      r == 1 and abs(o[0] - 0.35) < 1e-6 and o[0xC] == o[0xD] == o[0xE] == 0.0, o)
r, o = mon_block(1000, 500, 250)
print(f"     monitor sends: FX1 {o[0xD]:.4f}, FX2 {o[0xE]:.4f}, FX3 {o[0xC]:.4f} (monitor {MON_SIG})")
check("mon_node, sends 1000 / 500 / 250: only the monitor (not the pad on bus 0) to FX1 / FX2 / FX3, falling with the send",
      abs(o[0] - 0.35) < 1e-6 and 0 < o[0xC] < o[0xE] < o[0xD] <= MON_SIG + 1e-6, o)

print(f"\n{'FAILED: ' + str(failures) if failures else 'all passed'}")
sys.exit(1 if failures else 0)
