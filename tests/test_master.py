# ported to the bitbox MK2 2.3.4 by tools/port_mk2.py
"""Unicorn tests for patches/master.py (src/master.c, src/master_thunk.S): the settings store with the new entries (real
init 0x08089cbc), the MK2 Tools tabs (Comp, Cmp2, Dist; SSL swap), the compressor's settings on its object,
and the master distortion on a real bus buffer.

  python3 -I tests/test_master.py   (after ./build.sh and patch.py fidelity pages sound reverb fixes master cave)
"""
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_src = open(os.path.join(HERE, "test_fidelity.py")).read()
exec(compile(_src[:_src.index("\ne = Emu()\n")], "test_fidelity.py (header)", "exec"))   # Emu, check, IMG, SYM, ...

from unicorn.arm_const import UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3  # noqa: E402

e = Emu()
e.stub(0x08043B88, lambda uc: uc.reg_write(UC_ARM_REG_R0, e.new(uc.reg_read(UC_ARM_REG_R0))))   # DTCM allocator
APP = 0x24015170
SET = APP + 0x7ED0          # MK2 settings store (micro +0x7fd0); APP is ported below
COMP = {"thresh": 0x1A1, "ratio": 0x1A2, "attack": 0x1A3, "release": 0x1A5, "gain": 0x1A6}
DIST = {"type": 0x1A7, "drive": 0x1A8, "tone": 0x1A9, "mix": 0x1AB, "level": 0x1AC}

def w8(a, v):
    e.mu.mem_write(a, bytes([v]))

def r8(a):
    return e.mu.mem_read(a, 1)[0]

def store_ids(store):
    cnt = struct.unpack("<H", e.mu.mem_read(store + 0xA, 2))[0]
    ent = e.r32(store + 4)
    out = {}
    for k in range(cnt):
        pid, = struct.unpack("<H", e.mu.mem_read(ent + 8 * k, 2))
        out[pid] = struct.unpack("<i", e.mu.mem_read(ent + 8 * k + 4, 4))[0]
    return cnt, out

def get(pid):
    return struct.unpack("<i", struct.pack("<I", e.call(0x08089388, SET, pid)))[0]

def put(pid, v):
    e.call(0x080893B2, SET, pid, v & 0xFFFFFFFF)

# --- settings store: the real init (0x08089cbc) with the hook at its last add
e.stub(0x08045012, lambda uc: None)                       # its last call (not about the store)
e.call(0x08089CBC, SET)
cnt, ids = store_ids(SET)
check("settings store: 36 stock entries + 15 new + FX Out + Saver, View, More Page (55 of 64), 0x17b still there",
      cnt == 55 and 0x17B in ids and 0x96 in ids, cnt)
check("new entries with defaults: compressor = stock (-4 dB, 4:1, 10 ms, 250 ms, -4 dB), distortion Off / 12 dB / "
      "Tone open / Mix 100 % / Level 0 dB",
      [ids.get(COMP[k]) for k in ("thresh", "ratio", "attack", "release", "gain")] == [-4000, 4, 10, 250, -4000]
      and [ids.get(DIST[k]) for k in ("type", "drive", "tone", "mix", "level")] == [0, 12000, 1000, 1000, 0],
      {hex(k): v for k, v in ids.items() if k >= 0x1A0})

# --- MK2 Tools tabs (patches/master.py): labels and ids as data, the SSL swap in master_tools_page (real builder)
LABELS, IDS = 0x080C7EA4, 0x080C7EBC   # [mk2]
labels = [e.cstr(e.r32(LABELS + 4 * k)) for k in range(6)]
check("Tools tab labels: Main, Rec, MIDI, Comp, More, Dist", labels == ["Main", "Rec", "MIDI", "Comp", "More", "Dist"], labels)
tab_ids = [list(struct.unpack_from("<4H", IMG, IDS + 16 * t - 0x08040000)) for t in range(6)]
check("tabs 3..5: Comp [Type, Threshold, Ratio, Mix], Cmp2 [Attack, Release, Gain, Dist Level], "
      "Dist [Type, Drive, Tone, Mix]",
      tab_ids[3:] == [[0x19D, 0x1A1, 0x1A2, 0x19E], [0x1A3, 0x1A5, 0x1A6, 0x1AC], [0x1A7, 0x1A8, 0x1A9, 0x1AB]], tab_ids)
check("stock tabs unchanged (Rec: FX Out in its free slot, patches/fx.py)",
      tab_ids[:3] == [[0x61, 0x78, 0x9C, 0x96], [0x7F, 0x17B, 0xC2, 0x1B5], [0x59, 0x5A, 0xB5, 0xC3]], tab_ids[:3])

def tools_tab(tab):
    msg = e.new(8)
    e.mu.mem_write(msg + 4, struct.pack("<H", 0x40))
    out = e.new(0x80)
    e.call(SYM["master_tools_page"], APP, msg, out, tab)
    return list(struct.unpack("<8H", e.mu.mem_read(out + 2, 16)))[:4], list(struct.unpack("<8i", e.mu.mem_read(out + 0x14, 32)))[:4]

put(0x19D, 0)
ids3, vals3 = tools_tab(3)
ids4, _ = tools_tab(4)
check("Stock: Comp tab shows Ratio (0x1a2) with its value; More (page Comp 2): Page, Attack, Release, Gain",
      ids3 == [0x19D, 0x1A1, 0x1A2, 0x19E] and vals3[2] == get(0x1A2) and ids4 == [0x1BA, 0x1A3, 0x1A5, 0x1A6],
      (ids3, vals3, ids4))
put(0x1BA, 1)
ids4b, _ = tools_tab(4)
put(0x1BA, 2)
ids4c, vals4c = tools_tab(4)
put(0x1BA, 0)
check("More: page Dist 2 = Dist Lvl; page Saver = Saver (default 5 min), View",
      ids4b == [0x1BA, 0x1AC, 0, 0] and ids4c == [0x1BA, 0x1B8, 0x1B9, 0] and vals4c[:3] == [2, 3, 0], (ids4b, ids4c, vals4c))
put(0x19D, 1)
ids3, vals3 = tools_tab(3)
ids4, vals4 = tools_tab(4)
check("SSL: Comp tab shows the ratio switch (0x19f), Cmp2 the attack / release switches (0x17d / 0x17f) with values",
      ids3 == [0x19D, 0x1A1, 0x19F, 0x19E] and ids4[1:3] == [0x17D, 0x17F] and vals3[2] == get(0x19F)
      and vals4[1:3] == [get(0x17D), get(0x17F)], (ids3, vals3, ids4, vals4))
check("SSL defaults: mix glue 2:1 (0), attack 30 ms (5), release Auto (4)", [get(0x19F), get(0x17D), get(0x17F)] == [0, 5, 4])
msg = e.new(8)
e.mu.mem_write(msg + 4, struct.pack("<H", 0x140))
out = e.new(0x80)
e.call(SYM["master_tools_page"], APP, msg, out, 3)
check("other message codes pass through untouched (no swap)", 0x19F not in struct.unpack("<8H", e.mu.mem_read(out + 2, 16)))
put(0x19D, 0)
check("thunk site 0x08098190 is a BL to master_dist_thunk", bl_target(IMG[0x08098190 - 0x08040000:0x08098194 - 0x08040000],  # [mk2]
      0x08098190) == SYM["master_dist_thunk"] & ~1)  # [mk2]

# --- compressor: engine with bus 0, the object (bus index +0x1e = 0, no next node)
N = 128
engine = e.new(0x16000)
bus0 = engine + 0x150E4
e.call(0x080A44B4, bus0, N)
comp = e.new(0x700)

def fill(values):
    e.call(0x080A457C, bus0)
    e.call(0x080A4570, bus0, N)
    il, ir = e.new(4), e.new(4)
    e.call(0x080A467C, bus0, il, ir)
    e.wf(e.r32(il), values)
    e.wf(e.r32(ir), values)

def planes():
    return e.rf(e.r32(bus0 + 8), N), e.rf(e.r32(bus0 + 0xC), N)

seen = []
def _comp(uc):                                             # stock DSP stub: records the object, wet = silence
    c = uc.reg_read(UC_ARM_REG_R0)
    seen.append((uc.reg_read(UC_ARM_REG_R1),
                 struct.unpack("<fffI", uc.mem_read(c + 0x428, 12) + uc.mem_read(c + 0x448, 4)),
                 struct.unpack("<I", uc.mem_read(c + 0x44C, 4))[0]))
    for off in (8, 0xC):
        uc.mem_write(struct.unpack("<I", uc.mem_read(bus0 + off, 4))[0], b"\0" * 4 * N)
e.stub(0x080BA43C, _comp)
put(COMP["thresh"], -18000)
put(COMP["ratio"], 6)
put(COMP["attack"], 30)
put(COMP["release"], 600)
put(COMP["gain"], 6500)
fill([0.5] * N)
e.call(SYM["master_comp"], comp, engine)
(eng, (th, ra, mk, at), rel), = seen
check("Stock: -18 dB, 6:1, makeup +6.5 dB, attack 30 ms = 1440, release 600 ms = 28800 samples; engine passed on",
      eng == engine and abs(th + 18) < 1e-4 and ra == 6.0 and abs(mk - 6.5) < 1e-4 and at == 1440 and rel == 28800,
      seen)
put(0x19E, 300)
fill([0.5] * N)
e.call(SYM["master_comp"], comp, engine)
l, r = planes()
check("Mix 30 % (Stock): 0.7 dry + 0.3 wet (the stub's wet is silence) -> 0.35", abs(l[0] - 0.35) < 1e-5 and abs(r[5] - 0.35) < 1e-5,
      (l[0], r[5]))
put(0x19E, 1000)

# Compressor: FX1/2 (0x96 = 2): the same object on bus 3 (its bus id +0x1e = 3 during the call, then back)
ids_seen = []
e.stubs[0x080BA43C] = lambda uc: ids_seen.append(struct.unpack("<H", uc.mem_read(uc.reg_read(UC_ARM_REG_R0) + 0x1E, 2))[0])
e.call(0x080A44B4, engine + 0x150E4 + 3 * 20, N)
put(0x96, 2)
e.call(SYM["master_comp"], comp, engine)
put(0x96, 1)
e.call(SYM["master_comp"], comp, engine)
check("Compressor FX1/2: stock DSP runs on bus 3 (id +0x1e = 3), restored; Out1/2: bus 0",
      ids_seen == [3, 0] and struct.unpack("<H", e.mu.mem_read(comp + 0x1E, 2))[0] == 0, ids_seen)
e.stubs[0x080BA43C] = _comp

# --- SSL type: our DSP on bus 0, the stock DSP not called
put(0x19D, 1)
seen.clear()
e.w32(0x2406D180, 0)                                       # SSL state: fresh
def ssl_run(gen, blocks):
    out = []
    for b in range(blocks):
        fill([gen(b * N + k) for k in range(N)])
        e.call(SYM["master_comp"], comp, engine)
        out += planes()[0]
    return out

def db(x):
    return 20 * math.log10(max(x, 1e-9))

sine = lambda a: (lambda k: a * math.sin(2 * math.pi * 1000 * k / 48000))
def steady_reduction(ratio_idx, att=2, rel=0, thr=-20000, amp=0.5):
    put(COMP["thresh"], thr)
    put(COMP["gain"], 0)
    put(0x19F, ratio_idx)
    put(0x17D, att)
    put(0x17F, rel)
    e.w32(0x2406D180, 0)
    out = ssl_run(sine(amp), 300)                          # 0.8 s
    return db(max(abs(x) for x in out[-4800:])) - db(amp)
red = [steady_reduction(i) for i in (0, 1, 2)]
expect = [(1 / R - 1) * 14 for R in (2, 4, 10)]            # -6 dBFS sine, threshold -20 dB: 14 dB over
print("     SSL steady reduction (2/4/10:1):", [round(x, 2) for x in red], "expected", [round(x, 2) for x in expect])
check("SSL: stock DSP not called; steady reduction of a -6 dBFS sine at -20 dB threshold = (1/R - 1) x 14 dB (+-1.5 dB)",
      not seen and all(abs(a - b) < 1.5 for a, b in zip(red, expect)), (red, expect))

def env_after(att_idx, ms):                                # gain reduction (dB) ms after a step from silence
    put(0x17D, att_idx)
    put(0x17F, 0)
    put(0x19F, 2)
    e.w32(0x2406D180, 0)
    out = ssl_run(sine(0.5), (ms * 48 + N - 1) // N)
    seg = out[-480:]
    return db(max(abs(x) for x in seg)) - db(0.5)
fast, slow = env_after(0, 10), env_after(5, 10)
check("SSL attack: 0.1 ms is fully down after 10 ms, 30 ms is still on the way", fast < slow - 3, (fast, slow))

def recovery(burst_s, rel_idx):                            # reduction 150 ms after a burst of burst_s seconds
    put(0x17D, 2)
    put(0x17F, rel_idx)
    put(0x19F, 2)
    e.w32(0x2406D180, 0)
    nb = int(burst_s * 48000 / N)
    out = ssl_run(lambda k: 0.5 * math.sin(2 * math.pi * 1000 * k / 48000) if k < nb * N else
                  0.01 * math.sin(2 * math.pi * 1000 * k / 48000), nb + 56)
    return db(max(abs(x) for x in out[-480:])) - db(0.01)
short_auto, long_auto = recovery(0.05, 4), recovery(1.5, 4)
print(f"     SSL Auto release: 150 ms after a 50 ms burst {short_auto:.1f} dB, after 1.5 s {long_auto:.1f} dB")
check("SSL Auto release: recovers fast after a short burst, holds after sustained compression", short_auto > long_auto + 3,
      (short_auto, long_auto))

child_calls = []
child = e.new(16)
vt = e.new(16)
e.w32(child, vt)
e.w32(vt + 0xC, 0x0807F001)
e.stub(0x0807F000, lambda uc: child_calls.append((uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1))))
e.w32(comp + 8, child)
fill([0.1] * N)
e.call(SYM["master_comp"], comp, engine)
e.w32(comp + 8, 0)
check("SSL: the next node (object +8, vtable +0xc) is called like the stock DSP does", child_calls == [(child, engine)],
      child_calls)
e.call(0x080A457C, bus0)
e.call(0x080A4570, bus0, N)
e.mu.mem_write(bus0 + 0x10, b"\1")
e.call(SYM["master_comp"], comp, engine)
check("SSL: silent bus stays silent (flag untouched)", e.mu.mem_read(bus0 + 0x10, 1)[0] == 1)
put(0x19D, 0)
put(COMP["thresh"], -4000)
put(COMP["gain"], -4000)

# --- distortion on bus 0
bus = e.new(20)
e.call(0x080A44B4, bus, N)
def run(block_fn, blocks, **params):
    for k, v in params.items():
        put(DIST[k], v)
    outl, outr = [], []
    for b in range(blocks):
        e.call(0x080A457C, bus)
        e.call(0x080A4570, bus, N)
        il, ir = e.new(4), e.new(4)
        e.call(0x080A467C, bus, il, ir)
        xl, xr = block_fn(b)
        e.wf(e.r32(il), xl)
        e.wf(e.r32(ir), xr)
        e.call(SYM["master_dist"], bus)
        pl, pr = e.r32(bus + 8), e.r32(bus + 0xC)
        outl += e.rf(pl, N)
        outr += e.rf(pr, N)
    return outl, outr

def sine(f, a):
    def fn(b):
        x = [a * math.sin(2 * math.pi * f * (b * N + k) / 48000) for k in range(N)]
        return x, x
    return fn

def harm(sig, f, h):
    """amplitude of harmonic h of f (single-bin DFT over whole periods)"""
    L = len(sig) - len(sig) % int(48000 / f)
    re_ = sum(sig[k] * math.cos(2 * math.pi * f * h * k / 48000) for k in range(L))
    im = sum(sig[k] * math.sin(2 * math.pi * f * h * k / 48000) for k in range(L))
    return 2 * math.hypot(re_, im) / L

src = sine(1000, 0.5)
L0, _ = run(src, 8, type=0, drive=12000, tone=1000, mix=1000, level=0)
check("Off: bus 0 unchanged", all(abs(a - b) < 1e-6 for a, b in zip(L0, sum((src(b)[0] for b in range(8)), []))))

e.w32(0x2406B800, 0)                                       # fresh state
L, R = run(src, 12, type=1, drive=0, tone=1000, mix=0, level=0)
ref = sum((src(b)[0] for b in range(12)), [])
check("Mix 0: the dry signal, delayed by the filters' 15 samples (aligned with the wet path)",
      all(abs(L[k] - ref[k - 15]) < 1e-5 for k in range(15, len(L))), max(abs(L[k] - ref[k - 15]) for k in range(15, len(L))))

for name, t in (("Soft", 1), ("Hard", 2), ("Fold", 3), ("Tube", 4)):
    e.w32(0x2406B800, 0)
    L, R = run(sine(1000, 0.25), 20, type=t, drive=0, tone=1000, mix=1000, level=0)
    s = L[N * 4:]
    h1 = harm(s, 1000, 1)
    lin = abs(h1 - 0.25) < 0.02 if t != 4 else 0.15 < h1 < 0.3
    e.w32(0x2406B800, 0)
    L, R = run(sine(1000, 0.5), 40, type=t, drive=18000, tone=1000, mix=1000, level=0)
    s = L[N * 24:]                                          # after the DC blocker (Tube) has settled
    h1, h2, h3 = harm(s, 1000, 1), harm(s, 1000, 2), harm(s, 1000, 3)
    dc = sum(s) / len(s)
    print(f"     {name}: 18 dB drive -> h1 {h1:.3f}, h2 {h2:.4f}, h3 {h3:.4f}, peak {max(map(abs, s)):.3f}, dc {dc:.4f}")
    ok = all(math.isfinite(v) for v in L + R) and max(map(abs, s)) < 1.3 and h3 > 0.02 and L == R and abs(dc) < 0.01
    if t == 4:
        ok = ok and h2 > 0.02                              # asymmetric: even harmonics
    check(f"{name}: 0 dB drive, -12 dB sine passes ~linearly; 18 dB drive: bounded, odd harmonics, no DC", lin and ok)

# aliasing: 7 kHz at 24 dB drive (hard clip); harmonics above 24 kHz fold back, 2x oversampling must keep them down
e.w32(0x2406B800, 0)
L, _ = run(sine(7000, 0.5), 30, type=2, drive=24000, tone=1000, mix=1000, level=0)
s = L[N * 6:]
f_alias = [abs(48000 - 7000 * 5)]                          # 5th harmonic 35 kHz -> 13 kHz
a_alias = harm(s[:len(s) - len(s) % 48], 13000 / 1000 * 1000, 1)
a_fund = harm(s[:len(s) - len(s) % 48], 7000, 1)
print(f"     Hard 7 kHz +24 dB: fundamental {a_fund:.3f}, alias at 13 kHz {a_alias:.4f} ({20 * math.log10(a_alias / a_fund + 1e-9):.1f} dB)")
check("2x oversampling: the 5th harmonic's alias (13 kHz) at least 20 dB below the fundamental",
      a_alias < a_fund * 0.1)

# Tone and Level
e.w32(0x2406B800, 0)
Lo, _ = run(sine(8000, 0.1), 16, type=1, drive=0, tone=1000, mix=1000, level=0)
e.w32(0x2406B800, 0)
Lt, _ = run(sine(8000, 0.1), 16, type=1, drive=0, tone=300, mix=1000, level=0)
e.w32(0x2406B800, 0)
Ll, _ = run(sine(1000, 0.1), 16, type=1, drive=0, tone=1000, mix=1000, level=-6000)
a_o, a_t = harm(Lo[N * 4:], 8000, 1), harm(Lt[N * 4:], 8000, 1)
a_l = harm(Ll[N * 4:], 1000, 1)
print(f"     Tone 300 at 8 kHz: {20 * math.log10(a_t / a_o):.1f} dB; Level -6 dB: {20 * math.log10(a_l / 0.1):.2f} dB")
check("Tone 300: 8 kHz damped by > 6 dB; Level -6 dB: -6 dB (+-0.3)",
      a_t < a_o * 0.5 and abs(20 * math.log10(a_l / 0.1) + 6) < 0.3)

put(DIST["type"], 0)
print(f"\n{'FAILED: ' + str(failures) if failures else 'all passed'}")
sys.exit(1 if failures else 0)
