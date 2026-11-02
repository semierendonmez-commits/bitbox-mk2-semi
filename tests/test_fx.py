"""Unicorn tests for patches/fx.py (src/fx.c, src/fx_thunk.S, reverb.c's pages, master.c's compressor bus): the delay
record with the new entries (real init 0x08089e7c), the registrations, the FX return routing and Delay Level on real
bus buffers, the delay cell's page filter, and the hooks' call sites.

  python3 -I tests/test_fx.py     (PK_BUILD=fidelity+pages+sound+reverb+fixes+master+fx+cave)
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_src = open(os.path.join(HERE, "test_fidelity.py")).read()
exec(compile(_src[:_src.index("\ne = Emu()\n")], "test_fidelity.py (header)", "exec"))   # Emu, check, IMG, SYM, ...

from unicorn.arm_const import UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP  # noqa: E402

e = Emu()
e.stub(0x08043B88, lambda uc: uc.reg_write(UC_ARM_REG_R0, e.new(uc.reg_read(UC_ARM_REG_R0))))   # DTCM allocator
APP = 0x24015170
SET = APP + 0x7ED0
DLY = APP + 0x2200                                   # FX cell record row 0: the delay
P_DTYPE, P_DRATE, P_DLEVEL, P_FX_OUT = 0x1AD, 0x1AE, 0x1B3, 0x1B5
BASE = 0x08040000


def ids_of(store):
    cnt = struct.unpack("<H", e.mu.mem_read(store + 0xA, 2))[0]
    ent = e.r32(store + 4)
    return [(struct.unpack("<H", e.mu.mem_read(ent + 8 * k, 2))[0], struct.unpack("<i", e.mu.mem_read(ent + 8 * k + 4, 4))[0])
            for k in range(cnt)]


def put(store, pid, v):
    e.call(0x080893B2, store, pid, v & 0xFFFFFFFF)


# --- delay record: find its type byte (the case with 0xab Ping), then the entries
typ = e.new(4)
rec_ids = []
for t in range(1, 0x4B):
    e.mu.mem_write(typ, bytes([t]))
    e.call(0x08089E7C, DLY, typ)
    rec_ids = ids_of(DLY)
    if any(p == 0xAB for p, _ in rec_ids) and any(p == 0x15 for p, _ in rec_ids):
        break
ids = [p for p, _ in rec_ids]
check(f"delay record (type {t:#x}): stock eight, then Type and the seven new settings with defaults",
      ids == [0x15, 0x16, 0x1B, 0x0E, 0xAC, 0x17, 0xAB, 0x18, P_DTYPE] + list(range(0x1AE, 0x1B5))
      and [v for _, v in rec_ids][8:] == [0, 300, 0, 0, 0, 1000, 500, 500], rec_ids)

# --- registrations
lists, knobs = {}, []


def fake_list(uc):
    sp = uc.reg_read(UC_ARM_REG_SP)
    lists[uc.reg_read(UC_ARM_REG_R1)] = (e.cstr(uc.reg_read(UC_ARM_REG_R2)),
                                         [e.cstr(e.r32(uc.reg_read(UC_ARM_REG_R3) + 4 * k)) for k in range(e.r32(sp))],
                                         e.cstr(e.r32(sp + 4)))


def fake_knob(uc):
    sp = uc.reg_read(UC_ARM_REG_SP)
    knobs.append((uc.reg_read(UC_ARM_REG_R1), uc.reg_read(UC_ARM_REG_R2), e.cstr(uc.reg_read(UC_ARM_REG_R3)),
                  e.r32(sp), e.r32(sp + 4), e.cstr(e.r32(sp + 8))))


e.stub(0x0806A27C, fake_list)
e.stub(0x0806A2D4, fake_knob)
e.call(SYM["fx_register"], 0)
lab = e.new(16)
e.mu.mem_write(lab, b"Compressor:\0")
xml = e.new(16)
e.mu.mem_write(xml, b"globcomp\0")
e.call(SYM["comp_register"], 0, 0x96, 3, lab, stack=(0, 1, xml))
del e.stubs[0x0806A27C], e.stubs[0x0806A2D4]
check("delay Type list: Digital, Dig >, Tape, Tape >, BBD, BBD > (xml dlytype)",
      lists.get(P_DTYPE) == ("Type:", ["Digital", "Dig >", "Tape", "Tape >", "BBD", "BBD >"], "dlytype"), lists.get(P_DTYPE))
check("FX Out list: Out1/2, Dly FX1/2, Rvb FX1/2, FX1/2, Dly1 Rvb2 (xml fxout)",
      lists.get(P_FX_OUT) == ("FX Out:", ["Out1/2", "Dly FX1/2", "Rvb FX1/2", "FX1/2", "Dly1 Rvb2"], "fxout"))
check("Compressor (0x96) is a list now: Off, Out1/2, FX1/2, Both; xml stays globcomp",
      lists.get(0x96) == ("Compressor:", ["Off", "Out1/2", "FX1/2", "Both"], "globcomp"), lists.get(0x96))
check("seven delay knobs 0x1ae..0x1b4, 0..1000",
      [k[0] for k in knobs] == list(range(0x1AE, 0x1B5)) and all(k[1] == 8 and k[3] == 0 and k[4] == 1000 for k in knobs),
      knobs)

# --- settings store: FX Out (default Out1/2)
e.stub(0x08045012, lambda uc: None)
e.call(0x08089CBC, SET)
st = dict(ids_of(SET))
check("settings store: FX Out entry, default 0 (Out1/2)", st.get(P_FX_OUT) == 0, st.get(P_FX_OUT))

# --- FX returns on real buses (engine +0x150e4, 20 bytes each; 0x080a44b4 sets one up with N frames)
N = 128
engine = e.new(0x16000)


def bus(i):
    return engine + 0x150E4 + 20 * i


for i in (0, 3, 4, 5, 0xA, 0xB):
    e.call(0x080A44B4, bus(i), N)


def clear(i):
    e.call(0x080A457C, bus(i))                           # silent


def fill(i, v):
    e.call(0x080A457C, bus(i))
    e.call(0x080A4570, bus(i), N)
    il, ir = e.new(4), e.new(4)
    e.call(0x080A467C, bus(i), il, ir)
    e.wf(e.r32(il), [v] * N)
    e.wf(e.r32(ir), [v] * N)


def level(i):
    if e.mu.mem_read(bus(i) + 0x10, 1)[0]:
        return 0.0
    return e.rf(e.r32(bus(i) + 8), 1)[0]


def route(out, which, lvl=500):
    put(SET, P_FX_OUT, out)
    put(DLY, P_DLEVEL, lvl)
    for i in (0, 3, 4, 5):
        clear(i)
    src = 0xA if which == 0 else 0xB
    fill(src, 0.5)
    e.call(SYM["fx_return"], bus(0), bus(src), engine, which)
    return [round(level(i), 4) for i in (0, 3, 4, 5)]


check("Out1/2: both returns into bus 0 (stock)", route(0, 0) == [0.5, 0, 0, 0] and route(0, 1) == [0.5, 0, 0, 0])
check("Dly FX1/2: delay -> bus 3, reverb -> bus 0", route(1, 0) == [0, 0.5, 0, 0] and route(1, 1) == [0.5, 0, 0, 0])
check("Rvb FX1/2: reverb -> bus 3, delay -> bus 0", route(2, 1) == [0, 0.5, 0, 0] and route(2, 0) == [0.5, 0, 0, 0])
check("FX1/2: both -> bus 3", route(3, 0) == [0, 0.5, 0, 0] and route(3, 1) == [0, 0.5, 0, 0])
check("Dly1 Rvb2: delay -> bus 4 (FX1 jack), reverb -> bus 5 (FX2 jack)",
      route(4, 0) == [0, 0, 0.5, 0] and route(4, 1) == [0, 0, 0, 0.5])
lo, hi = route(0, 0, 0)[0], route(0, 0, 1000)[0]
check("Delay Level 0 / 1000 = -12 / +12 dB on the delay return (reverb untouched: its own Level)",
      abs(lo - 0.5 * 10 ** (-12 / 20)) < 0.01 and abs(hi - 0.5 * 10 ** (12 / 20)) < 0.05 and route(0, 1, 0)[0] == 0.5,
      (lo, hi))
put(SET, P_FX_OUT, 0)
put(DLY, P_DLEVEL, 500)

# --- delay page filter (fx_page from clip_page): page 1 = stock + Type last, page 2 = ours + Type last
def page_ids(tval):
    put(DLY, P_DTYPE, tval)
    lst = e.new(4 + 12 * 0x30)

    def fill_list(uc):
        pids = [0x16, 0x1B, 0x0E, 0xAC, 0x17, 0xAB, 0x18, P_DTYPE] + list(range(0x1AE, 0x1B5))
        e.w32(lst, len(pids))
        for k, pid in enumerate(pids):
            e.mu.mem_write(lst + 4 + 12 * k, struct.pack("<HHHHI", pid, 0, 0, 0, 0))
        uc.reg_write(UC_ARM_REG_R0, 1)
    e.stub(0x08078EC8, fill_list)
    m = e.new(8)
    e.mu.mem_write(m + 4, struct.pack("<H", 0x300))
    e.call(SYM["clip_page"], APP, m, lst, 0)
    del e.stubs[0x08078EC8]
    return [struct.unpack_from("<H", e.mu.mem_read(lst + 4 + 12 * k, 2))[0] for k in range(e.r32(lst))]


p1, p2 = page_ids(2), page_ids(3)
check("delay page 1 (Tape): Delay, Feedback, Cutoff, Width, Beat, Ping, Filter, Type",
      p1 == [0x16, 0x1B, 0x0E, 0xAC, 0x17, 0xAB, 0x18, P_DTYPE], p1)
check("delay page 2 (Tape >): the seven new settings, Type last (same place as on page 1)",
      p2 == list(range(0x1AE, 0x1B5)) + [P_DTYPE], p2)
put(DLY, P_DTYPE, 0)

# --- delay write: Digital with Drive 0 and Tone open leaves the signal alone; Tape colours it
obj = e.new(0x224)
e.mu.mem_write(obj + 0x220, b"\0")
e.w32(0x2406D040, 0)                                       # fresh modulation state
wrote = []
e.stub(0x080A915C, lambda uc: wrote.append(e.rf(e.r32(uc.reg_read(UC_ARM_REG_R1) + 8), 4)))
e.stub(0x080A89D4, lambda uc: uc.reg_write(UC_ARM_REG_R0, 1))
e.call(SYM["delay_core"], obj, 0)
fill(0xA, 0.9)
e.call(SYM["delay_write"], obj + 0x30, bus(0xA), 1)
put(DLY, P_DTYPE, 2)
put(DLY, 0x1B1, 1000)
fill(0xA, 0.9)
e.call(SYM["delay_core"], obj, 0)
e.call(SYM["delay_write"], obj + 0x30, bus(0xA), 1)
check("delay write: Digital passes the line input untouched; Tape + Drive saturates it (< 0.9)",
      len(wrote) == 2 and abs(wrote[0][0] - 0.9) < 1e-6 and wrote[1][3] < 0.85, wrote)
put(DLY, P_DTYPE, 0)
put(DLY, 0x1B1, 0)

# --- delay read: the target length moves by the modulation for the read only, then comes back
line = obj + 0x30
e.w32(line + 0x10, 4800)
e.w32(line + 0x14, 96000)
seen_t = []
e.stub(0x080A91F4, lambda uc: seen_t.append(e.r32(uc.reg_read(UC_ARM_REG_R0) + 0x10)))
put(DLY, 0x1AF, 1000)                                      # Mod Depth 100 %
e.call(SYM["delay_core"], obj, 0)
e.call(SYM["delay_read"], line, bus(0xA), 1)
check("delay read: Mod Depth moves the target for the read (4800 -> more), restored after",
      seen_t and seen_t[0] > 4800 and e.r32(line + 0x10) == 4800, (seen_t, e.r32(line + 0x10)))
put(DLY, 0x1AF, 0)

# --- hook sites
def bl_at(at):
    return bl_target(IMG[at - BASE:at - BASE + 4], at)


check("returns: bl at 0x0809816a / 0x0809817a -> fx_ret1_thunk / fx_ret2_thunk",
      bl_at(0x0809816A) == SYM["fx_ret1_thunk"] & ~1 and bl_at(0x0809817A) == SYM["fx_ret2_thunk"] & ~1)
check("delay line reads (4) and writes (5) hooked",
      all(bl_at(a) == SYM["delay_read"] & ~1 for a in (0x080A8CE4, 0x080A8CF2, 0x080A8DD4, 0x080A8EB6))
      and all(bl_at(a) == SYM["delay_write"] & ~1 for a in (0x080A8DC0, 0x080A8DCA, 0x080A8DFC, 0x080A8EE8, 0x080A8EF2)))
check("Tools -> Rec tab: FX Out in the fourth slot",
      struct.unpack_from("<4H", IMG, 0x080C7EBC + 16 - BASE) == (0x7F, 0x17B, 0xC2, P_FX_OUT))

# --- FX3 (fx3.c, patches/fx3.py)
if "fx3_set_cell" in SYM:
    check("FX3: pad mixes, grid label, set-cell hooked; check thunks; row-3 records made reverb records (8 sites)",
          bl_at(0x080B3658) == SYM["fx3_mix_a"] & ~1 and bl_at(0x080B916E) == SYM["fx3_mix_b"] & ~1
          and bl_at(0x08072130) == SYM["fx3_label"] & ~1 and bl_at(0x08080AC8) == SYM["fx3_set_cell"] & ~1
          and bl_at(0x080B2438) == SYM["fx3_check_a"] & ~1 and bl_at(0x080B8EB4) == SYM["fx3_check_b"] & ~1
          and all(IMG[a - BASE:a - BASE + 2] == bytes.fromhex("4423") for a in
                  (0x0807A1F8, 0x0807E25C, 0x08083788, 0x0808ACD0, 0x0808B0E4, 0x0808DF38, 0x0807CC36, 0x0807CC5C)))
    widget = e.new(0x1A0)
    e.stub(0x0805D8CC, lambda uc: None)
    fx2 = e.new(8)
    e.mu.mem_write(fx2, b"FX 2\0")
    e.w32(0x2406CE00, 0)
    e.stub(0x08043BD0, lambda uc: uc.reg_write(UC_ARM_REG_R0, 0))      # no heap: FX3 stays off, the cell still works
    e.call(SYM["fx3_label"], widget, fx2)
    cells = []
    e.stub(0x0807D0C8, lambda uc: cells.append(struct.unpack("<H", uc.mem_read(uc.reg_read(UC_ARM_REG_R1) + 4, 2))[0]))
    app_cell = APP + 0x28
    msg = e.new(0x20)
    def touch(cur, cell):
        e.mu.mem_write(app_cell, struct.pack("<H", cur))
        e.mu.mem_write(msg + 4, struct.pack("<H", cell))
        e.call(SYM["fx3_set_cell"], APP, msg)
        return cells[-1], struct.unpack("<H", e.mu.mem_read(widget + 0x38, 2))[0]
    check("FX3 cell: first touch selects FX2; second touch -> FX3 (widget id 0x330); again -> FX2; other cells pass",
          touch(0x100, 0x310)[0] == 0x310 and touch(0x310, 0x310) == (0x330, 0x330) and touch(0x330, 0x330) == (0x310, 0x310)
          and touch(0x310, 0x300)[0] == 0x300)

# --- Slicer Sync Grid (slicer.c): the duration rounded to whole grid steps at the transport's tempo
if "slicer_len" in SYM:
    tr = e.new(0x40)
    e.mu.mem_write(tr + 0x18, struct.pack("<f", 120.0))
    padobj = e.new(0x300)
    grid = {"g": 0}
    e.stub(SYM["pad_setting"] & ~1, lambda uc: uc.reg_write(UC_ARM_REG_R0, grid["g"] if uc.reg_read(UC_ARM_REG_R1) == 0x1B6 else 0))
    def slen(x, g):
        grid["g"] = g
        lo, hi = struct.unpack("<II", struct.pack("<d", x))
        return struct.unpack("<i", struct.pack("<I", e.call(SYM["slicer_len"], lo, hi, padobj, tr)))[0]
    check("Sync Grid Free: as __aeabi_d2iz (13000.7 -> 13000); 1/16 at 120 BPM: 13000 -> 12000 (2 x 6000), "
          "2000 -> 6000 (at least one step); 1 bar: 100000 -> 96000",
          slen(13000.7, 0) == 13000 and slen(13000.7, 5) == 12000 and slen(2000.0, 5) == 6000 and slen(100000.0, 1) == 96000,
          (slen(13000.7, 0), slen(13000.7, 5), slen(2000.0, 5), slen(100000.0, 1)))
    del e.stubs[SYM["pad_setting"] & ~1]
    check("slice voice start: bl at 0x080b5aaa -> slicer_len_thunk", bl_at(0x080B5AAA) == SYM["slicer_len_thunk"] & ~1)

print(f"\n{'FAILED: ' + str(failures) if failures else 'all passed'}")
sys.exit(1 if failures else 0)
