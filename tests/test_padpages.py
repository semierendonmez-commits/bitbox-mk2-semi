"""Unicorn tests for patches/padpages.py on the patched image (out/fidelity+pages+padpages+sound+reverb+fx3+master+cave/MICRO.BIN).

Runs: the row thunks on their own (result, flags and registers preserved), the stock function bodies with the thunks in
place (which tile they address on page 0 / 1), the page-aware guards, the constructor wrappers (stock constructors
stubbed), and the page switch / encoder logic of the Pads screen with recorded calls. Drawing is not emulated.

  python3 -I tests/test_padpages.py      (after ./build.sh and patch.py fidelity pages padpages sound reverb fx3 master cave)
"""
import os
import struct
import sys

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs
from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_THUMB, Uc
from unicorn.arm_const import (UC_ARM_REG_APSR, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_PC,
                               UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4,
                               UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R12,
                               UC_ARM_REG_SP, UC_CPU_ARM_MAX)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "patches"))
from thumb import BASE, bl_target, symbols  # noqa: E402

IMG = open(os.path.join(ROOT, "out", "fidelity+pages+padpages+sound+reverb+fx3+master+cave", "MICRO.BIN"), "rb").read()
SYM = symbols(os.path.join(ROOT, "out", "cave.elf"))
RET, STACK = 0x24000000, 0x2407F000
PSTATE = 0x2001B110
REGS = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6,
        UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R12]

failures = 0


def check(what, ok, detail=""):
    global failures
    print(("ok   " if ok else "FAIL ") + what + ("" if ok else f"   {detail}"))
    failures += 0 if ok else 1


class Emu:
    def __init__(self):
        mu = self.mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        mu.ctl_set_cpu_model(UC_CPU_ARM_MAX)
        mu.mem_map(BASE, (len(IMG) + 0xFFFF) & ~0xFFFF)
        mu.mem_write(BASE, IMG)
        mu.mem_map(0x24000000, 0x80000)
        mu.reg_write(UC_ARM_REG_C1_C0_2, 0xF << 20)
        mu.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        mu.hook_add(UC_HOOK_CODE, lambda uc, a, *_: uc.emu_stop(), begin=RET, end=RET)
        self.stubs, self.hooked, self.log = {}, set(), []
        self.heap = 0x24030000

    def _hook(self, uc, addr, size, _):
        fn = self.stubs.get(addr)
        if fn is None:
            return
        args = [uc.reg_read(r) for r in REGS[:4]]
        self.log.append((addr, args))
        ret = fn(uc, args)
        uc.reg_write(UC_ARM_REG_R0, ret or 0)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def stub(self, addr, fn=lambda uc, a: 0):
        addr &= ~1
        self.stubs[addr] = fn
        if addr not in self.hooked:
            self.hooked.add(addr)
            self.mu.hook_add(UC_HOOK_CODE, self._hook, begin=addr, end=addr)

    def new(self, size):
        p = self.heap
        self.heap += (size + 15) & ~15
        self.mu.mem_write(p, bytes(size))
        return p

    def w8(self, a, v): self.mu.mem_write(a, bytes([v & 0xFF]))
    def w16(self, a, v): self.mu.mem_write(a, struct.pack("<H", v & 0xFFFF))
    def w32(self, a, v): self.mu.mem_write(a, struct.pack("<I", v & 0xFFFFFFFF))
    def r8(self, a): return self.mu.mem_read(a, 1)[0]
    def r16(self, a): return struct.unpack("<H", self.mu.mem_read(a, 2))[0]
    def r32(self, a): return struct.unpack("<I", self.mu.mem_read(a, 4))[0]

    def call(self, fn, *args, regs=()):
        mu = self.mu
        for r, v in zip(REGS, args):
            mu.reg_write(r, v & 0xFFFFFFFF)
        for r, v in regs:
            mu.reg_write(r, v & 0xFFFFFFFF)
        mu.reg_write(UC_ARM_REG_SP, STACK)
        mu.reg_write(UC_ARM_REG_LR, RET | 1)
        self.log = []
        mu.emu_start(fn | 1, RET, count=2_000_000)
        return mu.reg_read(UC_ARM_REG_R0)


def bl_callees(start, end):
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    md.skipdata = True
    out = set()
    for i in md.disasm(IMG[start - BASE:end - BASE], start):
        if i.mnemonic == "bl":
            t = bl_target(IMG[i.address - BASE:i.address - BASE + 4], i.address)
            if t is not None and t < 0x080E3E90:
                out.add(t)
    return out


e = Emu()


def set_page(p):
    e.w32(PSTATE, 0x50414745)
    e.w8(PSTATE + 4, p)


def msg(code):
    m = e.new(8)
    e.w32(m, 0x080BF4CC)
    e.w16(m + 4, code)
    return m


# --- row thunks on their own: result, flags, other registers ---------------------------------------------------------
THUNK_REGS = {"row_b_r2_r3": (2, 3), "row_b_r0_r3": (0, 3), "row_b_r5_r5": (5, 5), "row_b_r0_r6": (0, 6),
              "row_a_r7_r3": (7, 3), "row_a_r2_r0": (2, 0)}
ok_all = True
for name, (rd, rn) in THUNK_REGS.items():
    for p in (0, 1):
        for row in (2 * p, 2 * p + 1):
            set_page(p)
            vals = [0x1110 + k for k in range(10)]
            vals[rn] = (row << 4) | 2 | 0x100
            e.mu.reg_write(UC_ARM_REG_APSR, 0x60000000)          # Z and C set
            e.call(SYM[name], *vals)
            got = [e.mu.reg_read(r) for r in REGS]
            want = list(vals)
            want[rd] = row - 2 * p
            flags = e.mu.reg_read(UC_ARM_REG_APSR) & 0xF0000000
            ok_all &= got == want and flags == 0x60000000
check("row thunks: rd = row - 2*page, flags and other registers preserved", ok_all)

set_page(1)
e.call(SYM["row_d_r8_r3"], 0, 0, 0, 0x25, regs=[(UC_ARM_REG_R8, 0)])
r8 = e.mu.reg_read(UC_ARM_REG_R8)
set_page(0)
e.call(SYM["row_d_r8_r3"], 0, 0, 0, 0x25, regs=[(UC_ARM_REG_R8, 0)])
check("screen D thunk: rows swap on page 1 (2 -> 0), unchanged on page 0", r8 == 0 and e.mu.reg_read(UC_ARM_REG_R8) == 2,
      (r8, e.mu.reg_read(UC_ARM_REG_R8)))

# --- stock bodies with the thunks in place: which tile do they address? -----------------------------------------------
TILE_TAKERS = {}


def run_body(entry, body_range, taker, scr, code, p):
    """Run `entry` (a cave guard or a stock function) with all callees in body_range stubbed; return r0 of the first
    call to `taker` (the tile pointer) or None if not called."""
    for t in bl_callees(*body_range):
        e.stub(t)
    e.stub(0x080774D0, lambda uc, a: 0)
    set_page(p)
    e.call(entry, scr, msg(code))
    hits = [args[0] for a, args in e.log if a == taker]
    return hits[0] if hits else None


def tile(scr, base, row, col):
    return scr + base + row * 0x680 + col * 0x1A0


scr = e.new(0x2000)
cases = [  # (name, entry, body range, tile taker, tile base)
    ("Pads update (A)", SYM["padpages_a_update"], (0x080862B6, 0x0808639C), 0x0805DCDC, 0x34),
    ("Pads tile pressed (A)", SYM["padpages_a_touch"], (0x08086482, 0x080865E0), None, 0x34),
    ("screen B update", SYM["padpages_b_update"], (0x08084046, 0x080840C0), 0x0805DCDC, 0x34),
]
for name, entry, rng, taker, base in cases:
    if taker is None:
        continue
    r = [run_body(entry, rng, taker, scr, 0x12, 0), run_body(entry, rng, taker, scr, 0x32, 1),
         run_body(entry, rng, taker, scr, 0x32, 0), run_body(entry, rng, taker, scr, 0x12, 1)]
    check(f"{name}: page 0 pad 0x12 -> tile row 1, page 1 pad 0x32 -> tile row 1, off-page pads skipped",
          r == [tile(scr, base, 1, 2), tile(scr, base, 1, 2), None, None], [hex(x) if x else x for x in r])

# the touch body: stop right after its first row thunk (0x080864ea) and read the row it produced; an off-page pad never
# gets there (guard)
stops = []


def stop_after(uc, a, *_):
    stops.append(uc.reg_read(UC_ARM_REG_R2))
    uc.emu_stop()


h = e.mu.hook_add(UC_HOOK_CODE, stop_after, begin=0x080864EE, end=0x080864EE)
for t in bl_callees(0x08086482, 0x080864EA):
    e.stub(t)
for p, code in ((0, 0x13), (1, 0x33), (1, 0x13)):
    set_page(p)
    e.call(SYM["padpages_a_touch"], scr, msg(code))
e.mu.hook_del(h)
check("Pads tile pressed: row 1 for pad 0x13 on page 0 and pad 0x33 on page 1, off-page pad stopped by the guard",
      stops == [1, 1], stops)

set_page(1)
check("screen B visibility: on page / off page / layer bits",
      [e.call(SYM["padpages_b_visible"], scr, msg(c)) for c in (0x21, 0x01, 0x121)] == [1, 0, 0])

# screen D: stock update with the swap thunk; first tile pointer passed on
d_scr = e.new(0x2600)
for t in bl_callees(0x08069276, 0x080693CC):
    e.stub(t)
res = []
for p, code in ((0, 0x02), (1, 0x22), (1, 0x02)):
    set_page(p)
    e.call(0x08069268, d_scr, msg(code))
    res.append(sorted({args[0] for a, args in e.log if d_scr <= args[0] < d_scr + 0x2600}))
check("screen D: pad 0x02 on page 0 and pad 0x22 on page 1 use the same (visible) tile; 0x02 on page 1 a hidden one",
      res[0] == res[1] and tile(d_scr, 0x3AC, 0, 2) in res[0] and tile(d_scr, 0x3AC, 2, 2) in res[2],
      [list(map(hex, r)) for r in res])

# --- constructor wrappers ---------------------------------------------------------------------------------------------
e2 = Emu()
e = e2
for stock in (0x08083E40, 0x080696E0, 0x080866B0):
    e.stub(stock, lambda uc, a: a[0])
texts = []
e.stub(0x0805D1DC, lambda uc, a: texts.append(bytes(e.mu.mem_read(a[1], 16)).split(b"\0")[0].decode()))
fx3_calls = []
e.stub(SYM["fx3_init"], lambda uc, a: fx3_calls.append("init"))            # FX3 memory: tests/test_fx3.py
e.stub(SYM["fx3_record_ensure"], lambda uc, a: fx3_calls.append("record"))
e.w32(PSTATE, 0)
scrs = {"ctor_b": e.new(0x2000), "ctor_d": e.new(0x2600), "ctor_a": e.new(0x2000)}
e.w32(scrs["ctor_d"] + 0x2AE8, 0x080BFC9C)   # the Mixer's text button that becomes the toggle
e.w8(PSTATE + 4, 1)
rets = {name: e.call(SYM[name], s, 0x11, 0x22, 0x33) for name, s in scrs.items()}
ok = all(rets[n] == scrs[n] for n in scrs) and e.r32(PSTATE) == 0x50414745 and e.r8(PSTATE + 4) == 0
ok &= [e.r32(PSTATE + 0xC + 4 * k) for k in range(3)] == [scrs["ctor_a"], scrs["ctor_b"], scrs["ctor_d"]]
check("constructor wrappers: stock ctor runs, this returned, screens remembered, page reset to 0; Effects: FX3 init",
      ok and fx3_calls == ["init"], fx3_calls)
m2 = e.r32(PSTATE + 0x18)
check("Mixer: its text button at +0x2ae8 becomes the toggle: click command PAGE_CMD at +0x14, label 'Pads 1-8'",
      m2 == scrs["ctor_d"] + 0x2AE8 and e.r32(m2 + 0x14) == 0x7F01 and e.r32(m2) == 0x080BFC9C and texts == ["Pads 1-8"],
      (hex(m2), hex(e.r32(m2 + 0x14)), texts))

# --- page switch and encoder logic on the Pads screen (B; A = Effects, D = Mixer?) ------------------------------------------------------------------
A, Bs, D = scrs["ctor_a"], scrs["ctor_b"], scrs["ctor_d"]
for s, base, rows in ((A, 0x34, 2), (Bs, 0x34, 2), (D, 0x3AC, 4)):
    for r in range(rows):
        for c in range(4):
            e.w16(tile(s, base, r, c) + 0x38, (r << 4) | c)
updates, posts, notifies = [], [], []
e.stub(0x080862B6, lambda uc, a: updates.append(("A", e.r16(a[1] + 4))))
e.stub(0x08084046, lambda uc, a: updates.append(("B", e.r16(a[1] + 4))))
e.stub(0x08069268, lambda uc, a: updates.append(("D", e.r16(a[1] + 4))))
e.stub(0x08060DF8, lambda uc, a: posts.append((e.r16(a[1]), e.r16(a[1] + 8), e.r32(a[1] + 4))))
e.stub(0x08084154, lambda uc, a: notifies.append(e.r16(a[1])))


def note(nid, code=0):
    n = e.new(0x20)
    e.w16(n, nid)
    e.w32(n + 4, 0x080BF4CC)
    e.w16(n + 8, code)
    return n


def notify(nid, code=0):
    """a notification to the Pads screen (B); the selection arrives as 0x63"""
    del updates[:], posts[:], notifies[:]
    e.call(SYM["padpages_b_notify"], Bs, note(0x63 if nid == 0x112 else nid, code))


def page():
    return e.r8(PSTATE + 4)


def ids(s, base, rows):
    return [e.r16(tile(s, base, r, c) + 0x38) for r in range(rows) for c in range(4)]


notify(0x112, 0x13)                  # selection on pad 8
notify(0x112, 0x00)                  # encoder wraps to pad 1
notify(0x112, 0x22)                  # a pad of the other page
check("selection changes (encoder, other page) never switch the page", page() == 0 and posts == [] and notifies == [0x63],
      (page(), posts))

e.w16(0x24013CB0 + 0x14, 0x02)
del updates[:], posts[:]
btn = e.new(0x8000)
e.w8(btn + 0x7FE4, 2)                      # the button handler's object: Pads screen shown
e.call(SYM["padpages_left"], btn)          # a page button
check("page button: page 1, pad at the same place selected (0x22) via command 0x63",
      page() == 1 and posts == [(0x63, 0x22, 0x080BEE0C)], (page(), posts))
check("page 1: tile ids A and B = pads 9-16, D rows swapped",
      ids(A, 0x34, 2) == [0x20, 0x21, 0x22, 0x23, 0x30, 0x31, 0x32, 0x33] and ids(Bs, 0x34, 2) == ids(A, 0x34, 2)
      and ids(D, 0x3AC, 4)[:8] == ids(A, 0x34, 2) and ids(D, 0x3AC, 4)[8:] == [0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13])
check("page switch refreshes A and B with pads 9-16, D with all 16",
      sorted(c for s, c in updates if s == "A") == [0x20, 0x21, 0x22, 0x23, 0x30, 0x31, 0x32, 0x33]
      and sorted(c for s, c in updates if s == "B") == sorted(c for s, c in updates if s == "A")
      and len([1 for s, c in updates if s == "D"]) == 16, updates)
notify(0x112, 0x01)                  # encoder selects a pad of page 0 while page 1 is shown
check("encoder selection on page 1 does not jump back to page 0", page() == 1 and posts == [])

notify(0x67)
check("'all new' on page 1: stock handler, then Pads tiles refreshed with pads 9-16",
      notifies == [0x67] and sorted(c for s, c in updates if s == "B") == [0x20, 0x21, 0x22, 0x23, 0x30, 0x31, 0x32, 0x33])

check("Mixer label follows the page", texts[-1:] == [("Pads 9-16" if page() else "Pads 1-8")], (texts[-1:], page()))

# --- left arrow (button handler 0x080839e6 -> left_arrow) ---------------------------------------------------------------
app = e.new(0x8100)


def left(screen_id, sel):
    """run left_arrow as the handler would reach it: a 0x48 frame plus pushed r4 / pc below sp"""
    e.w8(app + 0x7FE4, screen_id)
    e.w16(0x24013CB0 + 0x14, sel)       # the app's selected pad
    del posts[:], texts[:]
    sp = STACK - 0x50
    e.w32(sp + 0x48, 0x4444)             # r4
    e.w32(sp + 0x4C, RET | 1)            # pc
    e.mu.reg_write(UC_ARM_REG_R0, app)
    e.mu.reg_write(UC_ARM_REG_SP, sp)
    e.mu.reg_write(UC_ARM_REG_LR, 0x2407E101)   # "continue in the stock handler": must not be taken on the Pads screen
    e.mu.mem_write(0x2407E100, b"\x00\xbe")    # bkpt if reached
    reached = []
    h = e.mu.hook_add(UC_HOOK_CODE, lambda uc, a, *_: (reached.append(a), uc.emu_stop()), begin=0x2407E100, end=0x2407E100)
    e.mu.emu_start(SYM["left_arrow"] | 1, RET, count=2_000_000)
    e.mu.hook_del(h)
    return bool(reached), e.mu.reg_read(UC_ARM_REG_R3), e.mu.reg_read(UC_ARM_REG_R4)


p0 = page()
went_on, r3, r4 = left(2, 0x12 | (p0 << 5))
check("left arrow on Pads: page toggles, same place selected on the other page, handler left via its epilogue",
      not went_on and page() == p0 ^ 1 and posts == [(0x63, 0x12 | ((p0 ^ 1) << 5), 0x080BEE0C)] and r4 == 0x4444
      and texts == [("Pads 9-16" if page() else "Pads 1-8")], (went_on, page(), posts, hex(r4), texts))
p1 = page()
went_on, r3, r4 = left(0x2E, 0x01)
check("left arrow on the Mixer: no page change, stock path continues with r3 = 0x7fe4", went_on and page() == p1 and r3 == 0x7FE4
      and posts == [], (went_on, page(), hex(r3)))

# --- Mixer toggle: its click arrives at the Mixer's input as event 2 with PAGE_CMD at +0xc ---------------------------
def click(cmd):
    n = e.new(0x20)
    e.w16(n, 2)
    e.w32(n + 4, 0x080BF4CC)
    e.w32(n + 0xC, cmd)
    return n


e.stub(0x080691B0, lambda uc, a: 9)
p2 = page()
e.w16(0x24013CB0 + 0x14, 0x03 | (p2 << 5))
del posts[:], texts[:]
r = e.call(SYM["padpages_d_input"], D, click(0x7F01))
check("Mixer toggle click: page flips, label follows, pad at the same place selected (0x63), handled",
      r == 0 and page() == p2 ^ 1 and posts == [(0x63, 0x03 | ((p2 ^ 1) << 5), 0x080BEE0C)]
      and texts == [("Pads 9-16" if page() else "Pads 1-8")], (r, page(), posts, texts))
r1 = e.call(SYM["padpages_d_input"], D, click(0xBE))
r2 = e.call(SYM["padpages_d_input"], D, note(3, 0x12))
check("other Mixer events (other click commands, tile presses) go to the stock handler", (r1, r2) == (9, 9) and page() == p2 ^ 1)

# --- Effects screen: second touch on the selected RVB cell switches it to FX3 and back; DLY stays stock -------------
e.stub(0x080865E4, lambda uc, a: 5)
labels, touches = [], []
e.stub(0x0805DCDC, lambda uc, a: labels.append(bytes(e.mu.mem_read(a[1], 8)).split(b"\0")[0].decode()))
e.stub(0x08086482, lambda uc, a: touches.append(e.r16(a[1] + 4)))
APPX = 0x24013CB0
e.w16(A + 0xED4 + 0x38, 0x310)
e.w16(A + 0x1BD4, 0x12)                   # the Effects screen's selected pad
e.w16(APPX + 0x28, 0x300)                 # DLY selected
p3 = page()
del posts[:], updates[:]
r = e.call(SYM["padpages_a_input"], A, note(3, 0x300))
check("Effects: second touch on DLY goes to the stock handler (no page switch any more)", r == 5 and page() == p3)
e.w16(APPX + 0x28, 0x310)                 # RVB selected
del posts[:], updates[:]
r = e.call(SYM["padpages_a_input"], A, note(3, 0x310))
check("Effects: second touch on RVB -> FX3: cell id 0x330, label FX3, cell selected (0x111), tiles and sends refreshed",
      r == 0 and e.r16(A + 0xED4 + 0x38) == 0x330 and labels[-1:] == ["FX3"] and posts == [(0x111, 0x330, 0x080BEE0C)]
      and e.r8(PSTATE + 5) == 1 and len(updates) == 8 and touches[-1:] == [0x12],
      (r, hex(e.r16(A + 0xED4 + 0x38)), labels, posts, len(updates), touches))
e.w16(APPX + 0x28, 0x330)
del posts[:]
r = e.call(SYM["padpages_a_input"], A, note(3, 0x330))
check("Effects: second touch on FX3 -> back to RVB (0x310)", r == 0 and e.r16(A + 0xED4 + 0x38) == 0x310
      and labels[-1:] == ["RVB"] and posts == [(0x111, 0x310, 0x080BEE0C)] and e.r8(PSTATE + 5) == 0)
p4 = page()
e.w16(APPX + 0x28, 0x300)
r1 = e.call(SYM["padpages_a_input"], A, note(3, 0x310))   # RVB, not selected
r2 = e.call(SYM["padpages_a_input"], A, note(3, 0x12))    # a pad tile
r3 = e.call(SYM["padpages_a_input"], A, note(1, 0x300))   # another event
check("Effects: unselected cell, pad tiles and other events go to the stock handler", (r1, r2, r3) == (5, 5, 5) and page() == p4)

# send id thunks: 0xbc while RVB, 0x1c8 while FX3
def fx3id(name, fx3):
    e.w8(PSTATE + 5, fx3)
    e.call(SYM[name], 0xA0, 0xA1, 0xA2, 0xA3, regs=[(UC_ARM_REG_R4, 0xB4), (UC_ARM_REG_R5, 0xB5), (UC_ARM_REG_R7, 0xB7)])
    return [e.mu.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2)]
ok = (fx3id("fx3id_r2_r1r4", 0)[1:] == [0xB4, 0xBC] and fx3id("fx3id_r2_r1r4", 1)[1:] == [0xB4, 0x1C8]
      and fx3id("fx3id_r2_r1r5", 1)[1:] == [0xB5, 0x1C8] and fx3id("fx3id_r1_r0r7", 1)[:2] == [0xB7, 0x1C8]
      and fx3id("fx3id_r1_r0r7", 0)[:2] == [0xB7, 0xBC])
e.w8(PSTATE + 5, 0)
check("Effects send-id thunks: FX2 Send (0xbc) normally, FX3 Send (0x1c8) while FX3 is shown; moved registers kept", ok)
p5 = page()
went_on, r3_, r4_ = left(0x30, 0x13 | (p5 << 5))
check("left arrow on the Effects screen: page toggles, pad at the same place selected, handler left",
      not went_on and page() == p5 ^ 1 and posts[-1:] == [(0x63, 0x13 | ((p5 ^ 1) << 5), 0x080BEE0C)], (went_on, page(), posts))

# --- periodic tile state loops (real code; state query 0x08078b94 recorded) -------------------------------------------
def state_codes(fn, p, scr):
    e.w8(PSTATE + 4, p)
    seen = []
    e.stub(0x08078B94, lambda uc, a: seen.append(e.r16(a[2] + 4)))
    for t in (0x0805E06C, 0x0805DFCE, 0x08060C32):
        e.stub(t)
    e.call(fn, scr, 0)
    return seen


pads_scr, fx_scr, mix_scr = e.new(0x2000), e.new(0x2000), e.new(0x2600)
P0, P1 = [0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13], [0x20, 0x21, 0x22, 0x23, 0x30, 0x31, 0x32, 0x33]
check("Pads tile state loop: pads 1-8 on page 0, 9-16 on page 1",
      state_codes(0x08084838, 0, pads_scr) == P0 and state_codes(0x08084838, 1, pads_scr) == P1)
check("Effects tile state loop: pads 1-8 on page 0, 9-16 on page 1",
      state_codes(0x08086B50, 0, fx_scr) == P0 and state_codes(0x08086B50, 1, fx_scr) == P1)
check("Mixer tile state loop (16 tiles, rows swapped): visible rows show the page",
      state_codes(0x08069DD0, 0, mix_scr) == P0 + P1 and state_codes(0x08069DD0, 1, mix_scr) == P1 + P0,
      (state_codes(0x08069DD0, 1, mix_scr),))

# --- title bar (real snprintf) ---------------------------------------------------------------------------------------
titles = []
for p in (0, 1):
    e.w8(PSTATE + 4, p)
    for name in (0x080C5654, 0x080C5E18):           # "Pads", "FX"
        buf = e.new(0x18)
        e.call(SYM["padpages_title"], buf, 0x18, name)
        titles.append(bytes(e.mu.mem_read(buf, 0x18)).split(b"\0")[0].decode())
check("title: Pads / FX with the page", titles == ["Pads 1-8", "FX 1-8", "Pads 9-16", "FX 9-16"], titles)

# --- real firmware: Mixer constructor, then the real click of its text button -----------------------------------------
r = Emu()
r.stub(0x08042DC0, lambda uc, a: r.new(a[0] * max(1, a[1])))
r.w32(PSTATE, 0)
mixer = r.new(0x3000)
r.call(SYM["ctor_d"], mixer)
fld = r.r32(PSTATE + 0x18)
rect = struct.unpack("<4i", r.mu.mem_read(fld + 4, 16))
check("real Mixer: toggle is its text button {161,186,158,35}, command PAGE_CMD", fld == mixer + 0x2AE8
      and rect == (161, 186, 158, 35) and r.r32(fld + 0x14) == 0x7F01, (hex(fld), rect))
clickable = r.r8(fld + 0x9C)
r.w8(fld + 0x9C, 1)                                 # clickable (the device highlights it, so it is)
rposts = []
r.stub(0x08069268, lambda uc, a: 0)                 # the Mixer's tile refresh needs app data the emulator lacks
r.w16(0x24013CB0 + 0x14, 0x02)
r.call(0x0805CCA0, fld)                             # the text button's release: click -> event 2 -> Mixer vt+0x30
check(f"real click path: text button release reaches padpages_d_input and flips the page (clickable byte was {clickable})",
      r.r8(PSTATE + 4) == 1, r.r8(PSTATE + 4))

# --- encoder stepping (0x08076cec replaced at 0x0807ca30 / 0x0807ca56) ----------------------------------------------
def step(fn, code, d):
    out, sel = e.new(8), e.new(8)
    e.w32(sel, 0x080BF4CC)
    e.w16(sel + 4, code)
    e.call(fn, out, 0x24013CB0, sel, d & 0xFFFFFFFF)
    return e.r32(out), e.r16(out + 4)


for t in (0x080751F8, 0x080BC9C8):
    e.stub(t)
e.call(0x0807FF90)                         # the firmware's own init of the pad order table (0x24013c30: 16 pads, rows)
e.w8(PSTATE + 4, 0)
same = all(step(SYM["padpages_step"], c, d) == step(0x08076CEC, c, d) for c in P0 for d in (1, -1))
check("encoder step on page 0: identical to the stock function for all pads, both directions", same)
e.w8(PSTATE + 4, 1)
seq, c = [], 0x20
for _ in range(9):
    c = step(SYM["padpages_step"], c, 1)[1]
    seq.append(c)
check("encoder step on page 1: runs through pads 9-16 and wraps", seq == P1[1:] + [0x20, 0x21], [hex(x) for x in seq])
check("encoder step on page 1 from a page-0 selection: first / last pad of page 1",
      step(SYM["padpages_step"], 0x03, 1)[1] == 0x20 and step(SYM["padpages_step"], 0x03, -1)[1] == 0x33)

# --- Pads screen encoder lists (real boot init 0x080847c4, real lookup 0x080840c4, real encoder case of 0x08084154) ---
k = Emu()
k.call(0x080847C4)
k.w32(PSTATE, 0x50414745)
LISTS = 0x24021DC8


def enc_lists():
    return [k.r16(LISTS + 8 * i + 4) for i in range(16)]


stock_lists = enc_lists()
kobj = k.new(0x8100)
k.w8(kobj + 0x7FE4, 2)
k.call(SYM["padpages_left"], kobj)                            # page 1
p1_lists = enc_lists()
k.call(SYM["padpages_left"], kobj)                            # page 0
check("encoder lists: stock order on page 0, the same order with pads 9-16 on page 1, back on page 0",
      stock_lists == [0x10, 0x11, 0x12, 0x13, 0, 1, 2, 3, 0x10, 0, 0x11, 1, 0x12, 2, 0x13, 3]
      and p1_lists == [c | 0x20 for c in stock_lists] and enc_lists() == stock_lists, [hex(c) for c in p1_lists])
k.call(SYM["padpages_left"], kobj)                            # page 1
bscr = k.new(0x2000)
idx = []
k.stub(0x0805ECE4, lambda uc, a: idx.append((a[0] - bscr, a[1])))
k.w16(bscr + 0xD38, 0x21)
k.call(0x080840C4, bscr)
check("Pads screen finds a page-1 selection (0x21) in both encoder lists", idx == [(0xD40, 5), (0xD58, 3)], idx)
kposts = []
k.stub(0x0805F114, lambda uc, a: k.w32(bscr + 0xD4C, 6))      # the encoder moved the row-wise list to index 6
k.stub(0x08060DF8, lambda uc, a: kposts.append((k.r16(a[1]), k.r16(a[1] + 8))))
km = k.new(0x20)
k.w16(km, 0x32)
k.w32(km + 0xC, 0)
k.w32(km + 0x10, 1)
k.call(0x08084154, bscr, km)
check("encoder on page 1 (real Pads notify, message 0x32): selects pad 0x22 of page 1", kposts == [(0x63, 0x22)], kposts)

# --- pad picker (real constructor and placing; allocator and pad-name lookup stubbed) -----------------------------------
q = Emu()
q.stub(0x08042DC0, lambda uc, a: q.new(a[0] * max(1, a[1])))
NAMES_BY_CODE = {0x00: b"KICK01.wav", 0x21: b"LOOPLONGNAME.wav", 0x33: b"HAT.wav"}
q.stub(0x080792E8, lambda uc, a: q.mu.mem_write(a[2], NAMES_BY_CODE.get(q.r16(a[1] + 4), b"") + b"\0"))
q.w32(PSTATE, 0)
pk = q.new(0x700)
q.call(SYM["ctor_picker"], pk)
check("picker constructor wrapper: picker registered", q.r32(PSTATE + 0x1C) == pk, hex(q.r32(PSTATE + 0x1C)))
foot = pk + 0x594
check("stock picker: footer unused (command -1) and hidden", q.r32(pk + 0x5A8) == 0xFFFFFFFF and q.r8(foot + 0x30) == 1)
q.mu.mem_write(pk + 4, struct.pack("<4i", 0, 0, 320, 220))
pt = q.new(8)
q.mu.mem_write(pt, struct.pack("<2i", 60, 200))
q.w16(0x24013CB0 + 0x14, 0x00)
m = q.new(8)
q.w32(m, 0x080BF4CC)
q.w16(m + 4, 0x12)
q.call(SYM["padpages_picker_select"], pk, m)                 # the owner marks its pad (0x12)
q.call(0x080734D6, pk, pt)                                    # real placing -> padpages_picker_place
codes = lambda: [q.r16(pk + 0x34 + r * 0x2B0 + c * 0xAC + 0xA4) for r in range(2) for c in range(4)]
frect = struct.unpack("<4i", q.mu.mem_read(foot + 4, 16))
check("placing on page 0: footer stays stock (hidden, its rect, its command), tiles pads 1-8",
      q.r8(foot + 0x30) == 1 and frect == (56, 0, 264, 64) and q.r32(foot + 0x14) == 0xFFFFFFFF
      and codes() == [0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13], (q.r8(foot + 0x30), frect, hex(q.r32(foot + 0x14)), codes()))
# left arrow while this picker is open on the current screen
ui, owner = q.new(0x300), q.new(0x100)
q.w32(0x24000DC8, ui)
q.w32(ui + 0x280 + 0x34, owner)
q.w32(pk + 0x2C, owner)
q.w8(pk + 0x30, 0)
obj = q.new(0x8000)
q.w8(obj + 0x7FE4, 0x33)                                      # some slot screen, not Pads
marks = []
q.stub(0x0805D1F0, lambda uc, a: marks.append((a[0], a[1])))
q.stub(0x080732B0, lambda uc, a: marks.append(("clear", 0)))
r = q.call(SYM["padpages_left"], obj)
check("left arrow with the picker open on the current screen: taken, page 1, tiles pads 9-16",
      r == 1 and q.r8(PSTATE + 4) == 1 and codes() == [0x20, 0x21, 0x22, 0x23, 0x30, 0x31, 0x32, 0x33], (r, codes()))
check("after the switch the owner's pad (0x12, page 0) is not marked on page 1", ("clear", 0) in marks
      and not [a for a, b in marks if a != "clear" and b], marks)
q.w8(pk + 0x30, 1)                                            # picker closed
r = q.call(SYM["padpages_left"], obj)
check("left arrow with the picker closed: not taken (stock 'back')", r == 0 and q.r8(PSTATE + 4) == 1)
q.w8(pk + 0x30, 0)
q.w32(ui + 0x280 + 0x34, q.new(0x10))                         # another screen is current
check("left arrow with an open picker of another screen: not taken", q.call(SYM["padpages_left"], obj) == 0)
q.w32(ui + 0x280 + 0x34, owner)
q.w16(m + 4, 0x31)
q.call(SYM["padpages_picker_select"], pk, m)                 # the owner marks pad 0x31 (page 1)
del marks[:]
q.call(SYM["padpages_left"], obj)                             # page 0
q.call(SYM["padpages_left"], obj)                             # page 1 again: the owner's pad is re-marked
check("owner's marked pad is marked again after switching back to its page",
      [a for a, b in marks if a != "clear" and b][-1:] == [pk + 0x34 + 0x2B0 + 0xAC], marks[-3:])
seen_text = {}
q.stub(0x0805D1DC, lambda uc, a: seen_text.__setitem__(a[0], bytes(q.mu.mem_read(a[1], 16)).split(b"\0")[0].decode()))
q.call(0x080734D6, pk, pt)
tiles = [pk + 0x34 + r * 0x2B0 + c * 0xAC for r in range(2) for c in range(4)]
check("page 1 picker: tiles show pad names (no extension, 5 chars), footer untouched",
      seen_text.get(tiles[1]) == "LOOPL" and seen_text.get(tiles[7]) == "HAT" and seen_text.get(tiles[0]) == ""
      and foot not in seen_text, {hex(k): v for k, v in seen_text.items()})
# the owner marks a pad of the other page while the picker is open: the page follows
q.w16(m + 4, 0x02)
q.call(SYM["padpages_picker_select"], pk, m)
check("owner marks pad 0x02 while the picker is open on page 1: page 0 shown", q.r8(PSTATE + 4) == 0 and codes()[:4] == [0, 1, 2, 3])
q.w8(pk + 0x30, 1)
q.w16(m + 4, 0x23)
q.call(SYM["padpages_picker_select"], pk, m)
check("... while it is closed: page stays", q.r8(PSTATE + 4) == 0)
q.w16(m + 4, 0x31)
q.call(SYM["padpages_picker_select"], pk, m)
q.call(0x080734D6, pk, pt)                                    # opening: follows the owner's last mark (0x31, page 1)
check("opening the picker shows the page of the owner's marked pad", q.r8(PSTATE + 4) == 1)
q.w8(pk + 0x30, 0)
q.w32(foot + 0x14, 0x18C)                                     # an owner that uses the footer ("Pads" / "Seq")
seen_text.clear()
q.call(0x080734D6, pk, pt)
check("picker whose owner uses the footer: footer left alone (text, size)", q.r32(foot + 0x14) == 0x18C
      and foot not in seen_text and struct.unpack("<4i", q.mu.mem_read(foot + 4, 16)) == (56, 0, 264, 64))
q.stub(0x0805D1F0, lambda uc, a: marks.append((a[0], a[1])))
del marks[:]
q.unhook = None
pm = q.new(8)
q.w32(pm, 0x080BF4CC)
q.w16(pm + 4, 0x32)
q.call(0x080732D6, pk, pm)
q.w16(pm + 4, 0x02)
q.call(0x080732D6, pk, pm)
check("picker 'mark selected' on page 1: pad 0x32 marks tile row 1 col 2, pad 0x02 (page 0) marks nothing",
      marks == [(pk + 0x34 + 0x2B0 + 2 * 0xAC, 1)], [(hex(a) if isinstance(a, int) else a, b) for a, b in marks])
plays = []
q.stub(0x0805D304, lambda uc, a: plays.append((a[0], a[1])))
q.w16(pm + 4, 0x31)
q.call(0x08073312, pk, pm)
check("picker 'mark playing' on page 1: only the tile of pad 0x31 (row 1 col 1)",
      [a for a, b in plays if b] == [pk + 0x34 + 0x2B0 + 0xAC] and len(plays) == 8, plays)

print(f"\n{'FAILED: ' + str(failures) if failures else 'all passed'}")
