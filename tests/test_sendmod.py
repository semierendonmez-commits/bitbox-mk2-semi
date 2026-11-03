"""Unicorn tests for FX1 / FX2 / FX3 Send as modulation targets (src/sendmod.c, patches/fx3.py): the modulatable list,
the route add wrappers on the real stock route adds (Sample pads 0x080af648, Clip pads 0x080b7500), the per-block send
gains, route removal, the pad's route list check, FX3.

  python3 -I tests/test_sendmod.py     (PK_BUILD=...+fx3+...+cave)
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_src = open(os.path.join(HERE, "test_fidelity.py")).read()
exec(compile(_src[:_src.index("\ne = Emu()\n")], "test_fidelity.py (header)", "exec"))   # Emu, check, IMG, SYM, ...

from unicorn.arm_const import UC_ARM_REG_S0  # noqa: E402

if "sendmod_apply" not in SYM:
    print("no sendmod in this build")
    sys.exit(0)

e = Emu()
BASE = 0x08040000
APP = 0x24015170
STOCK = open(os.path.join(ROOT, "firmware", "BITBOXMK2.bin"), "rb").read()

mods = struct.unpack_from("<28H", IMG, 0x080C97CC - BASE)
stock_mods = struct.unpack_from("<28H", STOCK, 0x080C97CC - BASE)
check("modulatable list: entries 23..25 (repeats stock) -> FX1 / FX2 / FX3 Send, the rest as stock",
      mods[23:26] == (0xBB, 0xBC, 0x1C8) and mods[:23] == stock_mods[:23] and mods[26:] == stock_mods[26:]
      and stock_mods[23:26] == stock_mods[12:13] + stock_mods[11:12] + stock_mods[13:14], [hex(x) for x in mods])

# pad record row 0, col 0 (type 0x45 as at boot): FX1 Send 200, FX2 0, FX3 0
typ = e.new(4)
e.mu.mem_write(typ, b"\x45")
REC = APP + 0x1840
e.call(0x08089E7C, REC, typ)
for pid, v in ((0xBB, 200), (0xBC, 0), (0x1C8, 0)):
    e.call(0x080893B2, REC, pid, v)


def curve(x):
    e.call(0x080A1868, s=(x,))
    return struct.unpack("<f", struct.pack("<I", e.mu.reg_read(UC_ARM_REG_S0)))[0]


def msg(target, slot, code, amount, typ=0x3A):
    m = e.new(0x18)
    e.mu.mem_write(m, bytes([typ]))
    e.w32(m + 8, 0)
    e.w32(m + 0xC, target)
    e.w32(m + 0x10, slot)
    e.mu.mem_write(m + 0x14, struct.pack("<Hh", code, amount))
    return m


SM = 0x2001C480
e.w32(SM, 0)

# --- Sample pad (class A): a real-sized object, cell 0
pad = e.new(0x5B0)
e.w32(pad + 0x18, 0)
snap_mp = bytes(e.mu.mem_read(pad + 0x2FC, 0x34))
e.call(SYM["sendmod_route_a"], pad, msg(0xBB, 0, 0x1D, 500))          # MPAN -> FX1 Send, +0.5
mp_after = bytes(e.mu.mem_read(pad + 0x2FC, 0x34))
cnt = e.r32(pad + 0x54C)
check("route MPAN -> FX1 Send on a Sample pad: as stock no list entry (pad-level code), Filter's modulation object "
      "untouched, one entry in use", cnt == 0 and mp_after == snap_mp and e.r32(SM + 4) == 1, (cnt, e.r32(SM + 4)))

e.wf(pad + 0x134 + 4, [0.4])                                          # MPAN's value
e.wf(pad + 0x558, [0.0, 0.0])
e.call(SYM["sendmod_apply"], pad, 0)
g1, g2 = e.rf(pad + 0x558, 2)
check("block: FX1 gain = curve(0.2 + 0.5 x 0.4) = curve(0.4); FX2 (no route) untouched",
      abs(g1 - curve(0.4)) < 1e-6 and g2 == 0.0, (g1, curve(0.4), g2))
e.wf(pad + 0x134 + 4, [-1.0])
e.call(SYM["sendmod_apply"], pad, 0)
check("source -1: clamped at 0 (gain 0)", e.rf(pad + 0x558, 1)[0] == 0.0, e.rf(pad + 0x558, 1))

# LFO (code 4, per voice): the last playing voice's LFO (+0x518, value +4) -> FX2
voice = e.new(0x600)
e.mu.mem_write(voice, b"\x01")
e.wf(voice + 0x518 + 4, [0.5])
e.w32(pad + 0x2C, voice)
e.w32(pad + 0x6C, 1)
e.call(SYM["sendmod_route_a"], pad, msg(0xBC, 1, 4, 1000))
check("route LFO -> FX2: in the pad's route list (per-voice code)", e.r32(pad + 0x54C) == 1
      and e.r16(pad + 0x4F0) == 0xBC and e.r16(pad + 0x4F2) == 4, (e.r32(pad + 0x54C), hex(e.r16(pad + 0x4F0))))
e.call(SYM["sendmod_apply"], pad, 0)
g2 = e.rf(pad + 0x55C, 1)[0]
check("route LFO -> FX2 Send (slot 1): FX2 gain = curve(0 + 1.0 x 0.5)", abs(g2 - curve(0.5)) < 1e-6, (g2, curve(0.5)))

# removal: source code 0 for FX1 slot 0 -> the knob's gain back; the route list drop (preset change) clears FX2
e.call(SYM["sendmod_route_a"], pad, msg(0xBB, 0, 0, 0))
e.call(SYM["sendmod_apply"], pad, 0)
check("FX1 route removed: FX1 gain back to curve(0.2) (the knob)", abs(e.rf(pad + 0x558, 1)[0] - curve(0.2)) < 1e-6,
      e.rf(pad + 0x558, 1))
e.w32(pad + 0x54C, 0)                                                 # the pad's list emptied
e.call(SYM["sendmod_apply"], pad, 0)
e.call(SYM["sendmod_apply"], pad, 0)
check("route list emptied: FX2 gain back to 0 (knob 0), the entry freed",
      e.rf(pad + 0x55C, 1)[0] == 0.0 and e.r32(SM + 4) == 0, (e.rf(pad + 0x55C, 1), e.r32(SM + 4)))

# --- Clip pad (class B): source object from the stock route add's Filter slot; FX3
padb = e.new(0xBA0)
e.w32(padb + 0x18, 0)
before = bytes(e.mu.mem_read(padb, 0xBA0))
e.call(SYM["sendmod_route_b"], padb, msg(0x1C8, 2, 0x1D, 800))       # MPAN -> FX3 Send, slot 2
after = bytes(e.mu.mem_read(padb, 0xBA0))
used = e.r32(SM + 4)
ent = None
for k in range(24):
    a = SM + 8 + k * (8 + 9 * 12)
    if e.r32(a) == padb:
        ent = a
src = e.r32(ent + 8 + (2 * 3 + 2) * 12) if ent else 0
check("route MPAN -> FX3 Send on a Clip pad: the source object picked by the stock route add (inside the pad), "
      "the pad object as before", used == 1 and padb < src < padb + 0xBA0 and before == after,
      (used, hex(src), before == after))
e.wf(src + 4, [0.5])
on = e.call(SYM["fx3_send_on"], padb) if "fx3_send_on" in SYM else None
lvl = struct.unpack("<f", struct.pack("<I", 0))[0]
e.call(SYM["sendmod_fx3"], padb, s=(0.0,))
lvl = struct.unpack("<f", struct.pack("<I", e.mu.reg_read(UC_ARM_REG_S0)))[0]
check("FX3: level 0 + 0.8 x 0.5 = 0.4; FX3's send counts as on with the knob at 0", abs(lvl - 0.4) < 1e-6
      and e.call(SYM["sendmod_fx3_on"], padb) == 1, (lvl,))
e.call(SYM["sendmod_route_b"], padb, msg(0x1C8, 2, 0, 0))
e.call(SYM["sendmod_apply"], padb, 1)
check("Clip route removed: the entry freed", e.r32(SM + 4) == 0 and e.call(SYM["sendmod_fx3_on"], padb) == 0,
      e.r32(SM + 4))

# other targets go to the stock route add unchanged
e.call(SYM["sendmod_route_a"], pad, msg(0xAA, 0, 0x1D, 500))
check("a Filter route: stock (Filter's modulation object gets MPAN), no entry of ours",
      e.r32(pad + 0x2FC + 8) == pad + 0x134 and e.r32(SM + 4) == 0, (hex(e.r32(pad + 0x2FC + 8)),))

print(f"\n{'FAILED: ' + str(failures) if failures else 'all passed'}")
sys.exit(1 if failures else 0)
