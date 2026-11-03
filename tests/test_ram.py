"""Patch RAM map (bitbox MK2 2.3.4): every fixed address the cave code uses must lie in a registered region, regions
must not overlap, and all must stay in the gap the firmware leaves free above its initial stack pointer.

Measured in the Renode simulation of the MK2 (sim/mk2.repl; stock image, 0x2406ad98..0x2407ffff filled with 0xa5
before boot; boot, preset load, pads played): the firmware's own use above the stack starts at 0x24070dc0
(0x24070dc0-0x24070e17 and 0x24070f40-0x24070f97, 0x240711a0-0x2407939f: SD / file buffers, 0x2407d5c0..: audio /
ADC DMA). Same pattern as the micro (0x2406f4e0.. there, portkit pkZB). Recording was not exercised in that run: the
recording path must be checked on the canary as well before this layout counts as measured.

  python3 -I tests/test_ram.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FREE_LO, FREE_HI = 0x2406AD98, 0x24070DC0          # initial SP .. first firmware use (Renode canary, MK2)

REGIONS = [                                         # start, size, owner (micro address in portkit)
    (0x2406B000, 0x800, "fastget.c parameter cache (256 x 8)  [micro 0x2406a000]"),
    (0x2406B800, 0x300, "master.c distortion state  [0x2406a800]"),
    (0x2406BB00, 0x20, "recmon.c monitor buffer struct  [0x2406ab00] (not built yet)"),
    (0x2406BD00, 0x10, "PARK  [0x2406c100]"),
    (0x2406BD80, 0x40, "clipshift.c diagnostics  [0x2406c180]"),
    (0x2406BDC0, 0x40, "load.c meter  [0x2406c1c0]"),
    (0x2406C000, 0xA00, "stretch.c grain states  [0x2406c400]"),
    (0x2406CBF0, 0x10, "reverb.c object pointer  [0x2406cff0]"),
    (0x2406CC00, 0x100, "reverb.c RS1 (FX2)  [0x2406d000]"),
    (0x2406CD00, 0x100, "reverb.c RS2 (FX3)  [0x2406d100]"),
    (0x2406CE00, 0x100, "fx3.c state  [0x2406d200]"),
    (0x2406D000, 0x10, "master.c page state  [0x2406d400] (micro only, unused on the MK2)"),
    (0x2406D040, 0x80, "fx.c delay modulation / loop filter state  (semi)"),
    (0x2406D0C0, 0x40, "ss.c screen saver state  (semi)"),
    (0x2406D200, 0x100, "modsrc.c ENV sources (16 pads x 0x10)  (semi)"),
    (0x2406D300, 0x40, "dialdef.c double tap state + scratch record  (semi)"),
    (0x2406D340, 0x10, "vzoom.c waveform gain  (semi)"),
    (0x2406D180, 0x10, "master.c SSL state  [0x2406d580]"),
    (0x2406D190, 0x10, "master.c SSL state for bus 3 (Compressor: Both)  (semi)"),
    (0x2406F840, 0x100, "slicer.c Slice Str speeds (16 pads x 4 slots)  (semi)"),
    (0x2406F000, 0x840, "fx3.c FX3 send / return bus records and planes  (semi)"),
    (0x2406E000, 0x1000, "master.c Mix dry copy / recmon.c monitor signal (2 x 512 floats)"),
]

failures = 0


def check(what, ok, detail=""):
    global failures
    print(("ok   " if ok else "FAIL ") + what + ("" if ok else f"   {detail}"))
    failures += 0 if ok else 1


regs = sorted(REGIONS)
check(f"regions inside the free gap {FREE_LO:#x}..{FREE_HI - 1:#x}",
      all(FREE_LO <= a and a + n <= FREE_HI for a, n, _ in regs),
      [(hex(a), hex(a + n), o) for a, n, o in regs if not (FREE_LO <= a and a + n <= FREE_HI)])
check("regions do not overlap",
      all(a1 + n1 <= a2 for (a1, n1, _), (a2, _, _) in zip(regs, regs[1:])),
      [(o1, o2) for (a1, n1, o1), (a2, _, o2) in zip(regs, regs[1:]) if a1 + n1 > a2])

BUILT = re.findall(r"src/[a-z0-9_]+\.[cS]", open(os.path.join(ROOT, "build.sh")).read())
consts = {}
for f in BUILT:
    for ln, line in enumerate(open(os.path.join(ROOT, f)), 1):
        code = re.sub(r"\"[^\"]*\"", "", line.split("//")[0])   # no string literals
        if code.lstrip().startswith(("*", "/*")):
            continue                                        # comments
        code = re.sub(r"/\*.*?\*/", "", code)                # trailing comments
        for m in re.finditer(r"0x(2406[0-9a-fA-F]{4})u?\b", code):
            consts.setdefault(int(m.group(1), 16), []).append(f"{f}:{ln}")
outside = {hex(a): w for a, w in consts.items() if not any(s <= a < s + n for s, n, _ in regs)}
check(f"every patch RAM constant in the built sources ({len(consts)}) lies in a registered region", not outside, outside)

print(f"\n{'FAILED: ' + str(failures) if failures else 'all passed'}")
sys.exit(1 if failures else 0)
