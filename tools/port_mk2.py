"""Port portkit's micro 2.3.4 sources to the bitbox MK2 2.3.4: rewrite every flash / RAM address through one curated
map (re/mk2_map.py), so the MK2 tree keeps portkit's structure and later portkit changes can be ported the same way.

  python3 -I tools/port_mk2.py survey <files...>     every micro address in the files, with the map entry or "MISSING"
  python3 -I tools/port_mk2.py apply <files...>      rewrite in place (refuses while any address is unmapped)
  python3 -I tools/port_mk2.py auto <addr...>        translator leads for new map entries (tools/micro2mk2.py)

Addresses are recognised as 0x + 8 hex digits (optional C suffix u / U), in the flash image, AXI / D2 SRAM or SDRAM
range. Map values keep the case style of the source. Files already ported carry the marker below and are skipped.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(os.path.join(ROOT, "re"))
sys.path.append(os.path.join(ROOT, "tools"))
MARK = "ported to the bitbox MK2 2.3.4 by tools/port_mk2.py"
RX = re.compile(r"0x([0-9a-fA-F]{8})(?=[uU]?\b)")


def in_range(v):
    return 0x08040000 <= v < 0x08100000 or 0x20000000 <= v < 0x40000000 or 0xC0000000 <= v < 0xC4000000


def addrs(path):
    out = {}
    for n, line in enumerate(open(path), 1):
        for m in RX.finditer(line):
            v = int(m.group(1), 16)
            if in_range(v):
                out.setdefault(v, []).append(n)
    return out


def load_map():
    import mk2_map
    return mk2_map.MAP


def fmt(v, like):
    s = f"{v:08x}"
    return "0x" + (s.upper() if any(c in "ABCDEF" for c in like) else s)


def main(a):
    cmd, files = a[0], a[1:]
    if cmd == "auto":
        import micro2mk2 as T
        T.main(["auto"] + files)
        return
    amap = load_map()
    missing = 0
    for f in files:
        text = open(f).read()
        if MARK in text:
            print(f"{f}: already ported")
            continue
        found = addrs(f)
        for v, lines in sorted(found.items()):
            if v not in amap:
                missing += 1
                print(f"MISSING {v:#010x}  {f}:{','.join(map(str, lines))}")
            elif cmd == "survey":
                to, how = amap[v]
                print(f"{v:#010x} -> {to:#010x}  {how:24} {f}:{','.join(map(str, lines[:4]))}")
    if cmd != "apply":
        return
    if missing:
        raise SystemExit(f"{missing} unmapped addresses: nothing written")
    for f in files:
        text = open(f).read()
        if MARK in text:
            continue

        def sub(m):
            v = int(m.group(1), 16)
            return fmt(amap[v][0], m.group(1)) if in_range(v) else m.group(0)
        out = RX.sub(sub, text)
        c = "#" if f.endswith(".py") else "//" if f.endswith(".S") else None
        note = f"{c} {MARK}\n" if c else f"/* {MARK} */\n"
        if f.endswith(".ld"):
            note = f"/* {MARK} */\n"
        open(f, "w").write(note + out)
        print(f"wrote {f}")


if __name__ == "__main__":
    main(sys.argv[1:])
