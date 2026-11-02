"""Apply byte patches to the stock bitbox MK2 2.3.4 firmware and write an SD-card-ready BITBOXMK2.bin.

  python3 -I patch.py fidelity pages sound reverb cave        (run ./build.sh first)

Each patchset in patches/ is a module defining PATCHES = [(addr, old_bytes, new_bytes), ...]. Every patch verifies the
stock bytes before writing, so a patch built for the wrong firmware fails loudly. After patching, verify():
  - zero repack: applying nothing reproduces the stock image bit for bit
  - every changed byte lies inside a registered patch range (or the appended cave)
  - every rewritten BL (HOOKS, THUNKS) and every rewritten table word (WORDS) lands on a function symbol inside the
    cave, and every caller of an EXCLUSIVE stock callee is hooked
Nothing is written unless all checks pass. The stock file is never modified.
"""
import hashlib
import importlib
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, "patches"))
from thumb import BASE, STOCK_END, bl_target, symbols  # noqa: E402

STOCK = os.path.join(ROOT, "firmware", "BITBOXMK2.bin")
STOCK_SHA256 = "35ab7995ad0eca8484f3b28bc76389f10b2037dbb21a4a394e950d9329073e86"   # official MK2 2.3.4


def load_stock():
    data = open(STOCK, "rb").read()
    assert hashlib.sha256(data).hexdigest() == STOCK_SHA256, "stock firmware hash mismatch"
    assert BASE + len(data) == STOCK_END, "stock image size changed"
    return data


def apply(stock, names):
    data = bytearray(stock)
    ranges = []
    for name in names:
        for addr, old, new in importlib.import_module(name).PATCHES:
            o = addr - BASE
            if o == len(data):
                assert old == b"", f"{name}: append at {addr:#x} must have empty old bytes"
                data += new
                ranges.append((addr, addr + len(new)))
                print(f"{name}: {addr:#010x} append {len(new)} bytes")
                continue
            assert 0 <= o and o + len(old) <= len(stock), f"{name}: {addr:#x} outside the stock image"
            assert data[o:o + len(old)] == old, f"{name}: stock bytes differ at {addr:#x}"
            assert len(old) == len(new), f"{name}: size change at {addr:#x}"
            assert not any(a < addr + len(new) and addr < b for a, b in ranges), f"{name}: overlapping patch at {addr:#x}"
            data[o:o + len(new)] = new
            ranges.append((addr, addr + len(new)))
            print(f"{name}: {addr:#010x} {old.hex()} -> {new.hex()}")
    return data, ranges


def all_bl_sites(img, end):
    """(site, target) for every BL encoding in [BASE, end), at every halfword."""
    out = []
    for o in range(0, end - BASE - 3, 2):
        t = bl_target(bytes(img[o:o + 4]), BASE + o)
        if t is not None:
            out.append((BASE + o, t))
    return out


def verify(stock, data, ranges, names):
    # zero repack
    zero, _ = apply(stock, [])
    assert bytes(zero) == stock, "zero repack differs from the stock image"
    # changed bytes only inside registered ranges
    for o in range(len(stock)):
        if data[o] != stock[o]:
            a = BASE + o
            assert any(lo <= a < hi for lo, hi in ranges), f"unregistered change at {a:#x}"
    cave_end = BASE + len(data)
    # detours: rewritten BLs land on function symbols in the cave
    elf = os.path.join(ROOT, "out", "cave.elf")
    funcs = {v & ~1 for k, v in symbols(elf).items()} if os.path.exists(elf) else set()
    hooked = {}
    for name in names:
        mod = importlib.import_module(name)
        for at, stock_callee, _ in getattr(mod, "HOOKS", []):
            t = bl_target(bytes(data[at - BASE:at - BASE + 4]), at)
            assert t is not None and STOCK_END <= t < cave_end and t in funcs, f"{name}: hook at {at:#x} -> {t}"
            hooked.setdefault(stock_callee, set()).add(at)
        # every caller of an exclusive callee is hooked (scan the stock image, which is where the callers are)
        for callee in getattr(mod, "EXCLUSIVE", ()):
            callers = {s for s, t in all_bl_sites(stock, STOCK_END) if t == callee}
            missing = callers - hooked.get(callee, set())
            assert not missing, f"{name}: unhooked callers of {callee:#x}: {sorted(hex(m) for m in missing)}"
            # and nothing in the patched stock part still calls it
            still = {s for s, t in all_bl_sites(data, STOCK_END) if t == callee}
            assert not still, f"{name}: {callee:#x} still called from {sorted(hex(s) for s in still)}"
        # instruction thunks: the site is now a BL into a cave function
        for at, _, _ in getattr(mod, "THUNKS", []):
            t = bl_target(bytes(data[at - BASE:at - BASE + 4]), at)
            assert t is not None and STOCK_END <= t < cave_end and t in funcs, f"{name}: thunk at {at:#x} -> {t}"
        # table words (vtable slots): a Thumb pointer to a cave function
        for at, _, _ in getattr(mod, "WORDS", []):
            v = int.from_bytes(data[at - BASE:at - BASE + 4], "little")
            assert v & 1 and STOCK_END <= (v & ~1) < cave_end and (v & ~1) in funcs, f"{name}: word at {at:#x} = {v:#x}"
    print("verify: zero repack ok, all changes registered, detours land in the cave, exclusive callers hooked")


def check_cave_fresh():
    """Refuse a cave older than its sources (a failed ./build.sh leaves the old out/cave.elf behind)."""
    elf = os.path.join(ROOT, "out", "cave.elf")
    src = os.path.join(ROOT, "src")
    newest = max(os.path.getmtime(os.path.join(src, f)) for f in os.listdir(src))
    assert os.path.exists(elf) and os.path.getmtime(elf) >= newest, "out/cave.elf is older than src/: run ./build.sh"


def main(names):
    if "cave" in names:
        check_cave_fresh()
    stock = load_stock()
    data, ranges = apply(stock, names)
    verify(stock, data, ranges, names)
    out_dir = os.path.join(ROOT, "out", "+".join(names))
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, "BITBOXMK2.bin")
    open(out, "wb").write(data)
    print(f"wrote {out} ({len(data)} bytes, sha256 {hashlib.sha256(data).hexdigest()})")


if __name__ == "__main__":
    main(sys.argv[1:])
