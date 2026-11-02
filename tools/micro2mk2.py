"""Translate bitbox micro 2.3.4 addresses into the bitbox MK2 2.3.4 image.

Both images are built from the same matrixsw/BoomboxFramework source at the same load base (0x08040000) and differ
mostly by shifted code. Three kinds of address, three methods:

  code  normalised Thumb-2 instruction windows (branch targets and pc-relative offsets masked, struct offsets and
        immediates kept) vote for an offset (MK2 - micro). Votes from the windows around the address.
  data  byte content: the micro bytes at the address, with words that look like flash pointers masked, must occur
        exactly once in the MK2 image.
  ram   every literal-pool word in micro code equal to the address (or within +-0x40 of it, for struct members) is
        found again through its loading instruction's code match; the MK2 literal at the matched place votes.

A result is a lead, not a fact: patch.py still checks the expected stock bytes at every MK2 site, and anything not
confirmed by the tests stays "vermutet" in re/symbols-mk2.md.

  python3 -I tools/micro2mk2.py code 0x080b8420 ...
  python3 -I tools/micro2mk2.py data 0x080c5760 ...
  python3 -I tools/micro2mk2.py ram 0x24013cb0 ...
  python3 -I tools/micro2mk2.py auto <addr> ...      (picks by range)
"""
import os
import re
import struct
import sys
from bisect import bisect_right
from collections import Counter, defaultdict

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = 0x08040000
MICRO = os.path.join(ROOT, "firmware", "MICRO.BIN")
MK2 = os.path.join(ROOT, "firmware", "BITBOXMK2.bin")
W = 6                 # instructions per window
SPAN = 48             # instructions on each side of the address that vote
FLASH = (0x08040000, 0x08100000)

_PCREL = re.compile(r"\[pc, #(-?0x[0-9a-f]+|-?\d+)\]")


def _norm(i):
    op = i.op_str
    if i.mnemonic.startswith(("b", "cb")) and "#" in op:
        op = re.sub(r"#0x[0-9a-f]+", "#T", op)
    if "pc" in op:
        op = re.sub(r"#-?0x[0-9a-f]+|#-?\d+", "#L", op)
    return i.mnemonic + " " + op


class Image:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        self.end = BASE + len(self.data)
        md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
        md.skipdata = True
        self.ins = []          # (addr, norm, mnemonic, op_str, size)
        for i in md.disasm(self.data, BASE):
            self.ins.append((i.address, _norm(i), i.mnemonic, i.op_str, i.size))
        self.addrs = [x[0] for x in self.ins]
        self.windows = defaultdict(list)
        for k in range(len(self.ins) - W):
            self.windows[tuple(x[1] for x in self.ins[k:k + W])].append(k)

    def index(self, addr):
        """index of the instruction containing addr"""
        return max(0, bisect_right(self.addrs, addr) - 1)

    def window(self, k):
        return tuple(x[1] for x in self.ins[k:k + W])

    def word(self, addr):
        return struct.unpack_from("<I", self.data, addr - BASE)[0]

    def literal_refs(self):
        """{literal address: [loading instruction address]} for every pc-relative ldr"""
        refs = defaultdict(list)
        for a, _, m, op, _ in self.ins:
            if not m.startswith("ldr") or "pc" not in op:
                continue
            g = _PCREL.search(op)
            if g:
                t = ((a + 4) & ~3) + int(g.group(1), 0)
                refs[t].append(a)
        return refs


_cache = {}


def images():
    if not _cache:
        _cache["a"] = Image(MICRO)
        _cache["b"] = Image(MK2)
    return _cache["a"], _cache["b"]


def code(addr, span=SPAN):
    """(mk2 addr, votes, runner-up votes, exact): exact = the instruction at addr normalises identically"""
    a, b = images()
    i = a.index(addr)
    votes = Counter()
    for k in range(max(0, i - span), min(len(a.ins) - W, i + span)):
        for kb in b.windows.get(a.window(k), ()):
            votes[(kb - k)] += 1          # offset in instruction indices
    if not votes:
        return None, 0, 0, False
    (dk, n), *rest = votes.most_common(2)
    j = i + dk
    if not 0 <= j < len(b.ins):
        return None, 0, 0, False
    out = b.ins[j][0] + (addr - a.ins[i][0])
    exact = a.ins[i][1] == b.ins[j][1] and a.ins[i][0] == addr
    return out, n, (rest[0][1] if rest else 0), exact


def data(addr, length=32):
    """(mk2 addr, matches): the content at addr, flash-pointer words masked, found in MK2"""
    a, b = images()
    o = addr - BASE
    blob = a.data[o:o + length]
    mask = bytearray(b"\xff" * len(blob))
    for k in range((4 - addr % 4) % 4, len(blob) - 3, 4):
        w = struct.unpack_from("<I", blob, k)[0]
        if FLASH[0] <= (w & ~1) < FLASH[1] or 0x24000000 <= w < 0x24080000:
            mask[k:k + 4] = b"\0\0\0\0"
    # the longest unmasked run is the anchor
    best, run, start = (0, 0), 0, 0
    for k, m in enumerate(mask):
        if m:
            if run == 0:
                start = k
            run += 1
            if run > best[1]:
                best = (start, run)
        else:
            run = 0
    s, n = best
    if n < 6:
        return None, 0
    anchor = blob[s:s + n]
    hits = []
    p = b.data.find(anchor)
    while p >= 0:
        cand = p - s
        if cand >= 0 and all(not mask[k] or b.data[cand + k] == blob[k] for k in range(len(blob))):
            hits.append(BASE + cand)
        p = b.data.find(anchor, p + 1)
    if len(hits) == 1:
        return hits[0], 1
    return (hits[0] if hits else None), len(hits)


def ram(addr, slack=0x40):
    """(mk2 addr, votes, total): through every micro literal near addr and its loader's code match"""
    a, b = images()
    if "lit_a" not in _cache:
        _cache["lit_a"] = a.literal_refs()
    votes = Counter()
    total = 0
    for lit, loaders in _cache["lit_a"].items():
        if not (BASE <= lit < a.end - 3):
            continue
        v = a.word(lit)
        if not (addr - slack <= v <= addr + slack):
            continue
        for ld in loaders[:1]:
            m, n, _, exact = code(ld, span=16)
            if m is None or n < 8 or not exact:
                continue
            gb = _PCREL.search(b.ins[b.index(m)][3])
            if not gb:
                continue
            lit_b = ((m + 4) & ~3) + int(gb.group(1), 0)
            if BASE <= lit_b < b.end - 3:
                total += 1
                votes[b.word(lit_b) - v] += 1
    if not votes:
        return None, 0, 0
    d, n = votes.most_common(1)[0]
    return addr + d, n, total


def kind(addr):
    if 0x20000000 <= addr < 0x40000000 or addr >= 0xC0000000:
        return "ram"
    a, _ = images()
    # rodata starts where the instruction stream turns into data; strings and tables live above the code
    return "data" if addr >= RODATA else "code"


RODATA = 0x080C0000     # micro: code ends below, strings / tables / vtables above (0x080bf030 is the diskio table)


def main(argv):
    mode, addrs = argv[0], [int(x, 16) for x in argv[1:]]
    for x in addrs:
        k = kind(x) if mode == "auto" else mode
        if k == "code":
            m, n, r, ex = code(x)
            print(f"code {x:#010x} -> {m:#010x}  votes {n} (next {r}) {'exact' if ex else 'NOT EXACT'}" if m else
                  f"code {x:#010x} -> ?")
        elif k == "data":
            m, n = data(x)
            print(f"data {x:#010x} -> {m:#010x}  matches {n}" if m else f"data {x:#010x} -> ?  matches {n}")
        else:
            m, n, t = ram(x)
            print(f"ram  {x:#010x} -> {m:#010x}  votes {n}/{t}" if m else f"ram  {x:#010x} -> ?")


if __name__ == "__main__":
    main(sys.argv[1:])
