"""Patch RAM map (bitbox MK2 2.3.4): every fixed address the cave code uses must lie in a registered region, regions
must not overlap, and all must stay in the block reserved for it.

Since sL the patch RAM is the top 24 KB of DTCM, 0x2001a000..0x2001ffff, reserved by lowering the firmware's DTCM
allocator limit (0x08043b88: bump allocator over 0x20000000, 0x20000 -> 0x1a000; bigger requests fall back to malloc,
then SDRAM, as before) and zeroed at boot before anything else runs (src/boot.S, at the reset handler's first call;
patches/cave.py). Before (sA..sK) it sat in AXI RAM above the initial stack pointer (0x2406b000..0x2406f940, inside
the arena of the firmware's top-down DMA allocator 0x08046cb8, which Renode showed reaching only 0x24070dc0) and was
never cleared. Renode starts with zeroed RAM, a unit does not: modsrc.c's ENV table (a pad pointer per slot) was read
before anything wrote it, and the builds that run it every block (master's master_dist) did not boot on a unit
(hardware bisect: sT3 boots, sT8 = sT3 + master does not; Renode with RAM filled with a pattern shows the same
black screen). sim/run.py now fills DTCM and AXI RAM with a pattern before boot (SIM_ZERO_RAM=1: zeros).

  python3 -I tests/test_ram.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FREE_LO, FREE_HI = 0x2001A000, 0x20020000          # DTCM top, reserved (DTCM allocator limit 0x1a000)

REGIONS = [                                         # start, size, owner (micro address in portkit)
    (0x2001A000, 0x800, "fastget.c parameter cache (256 x 8)  [micro 0x2406a000]"),
    (0x2001A800, 0x300, "master.c distortion state  [0x2406a800]"),
    (0x2001AB00, 0x20, "recmon.c monitor buffer struct  [0x2406ab00] (not built yet)"),
    (0x2001AD00, 0x10, "PARK  [0x2001b100]"),
    (0x2001AD80, 0x40, "clipshift.c diagnostics  [0x2001b180]"),
    (0x2001ADC0, 0x40, "load.c meter  [0x2001b1c0]"),
    (0x2001B000, 0xA00, "stretch.c grain states  [0x2001b400]"),
    (0x2001BBF0, 0x10, "reverb.c object pointer  [0x2001bff0]"),
    (0x2001BC00, 0x100, "reverb.c RS1 (FX2)  [0x2001c000]"),
    (0x2001BD00, 0x100, "reverb.c RS2 (FX3)  [0x2001c100]"),
    (0x2001BE00, 0x100, "fx3.c state  [0x2001c200]"),
    (0x2001C000, 0x10, "master.c page state  [0x2001c400] (micro only, unused on the MK2)"),
    (0x2001C040, 0x80, "fx.c delay modulation / loop filter state  (semi)"),
    (0x2001C0C0, 0x40, "ss.c screen saver state  (semi)"),
    (0x2001C200, 0x100, "modsrc.c ENV sources (16 pads x 0x10)  (semi)"),
    (0x2001C300, 0x40, "dialdef.c double tap state + scratch record  (semi)"),
    (0x2001C340, 0x10, "vzoom.c waveform gain  (semi)"),
    (0x2001C380, 0x100, "gen.c GEN sequencer state (4 lanes, hit log)  (semi)"),
    (0x2001C480, 0xB80, "sendmod.c send modulation routes (24 pads x 3 sends x 3 slots)  (semi)"),
    (0x2001F400, 0xBA0, "sendmod.c a pad object's copy while a send route is resolved  (semi)"),
    (0x2001C180, 0x10, "master.c SSL state  [0x2001c580]"),
    (0x2001C190, 0x10, "master.c SSL state for bus 3 (Compressor: Both)  (semi)"),
    (0x2001E840, 0x100, "slicer.c Slice Str speeds (16 pads x 4 slots)  (semi)"),
    (0x2001E000, 0x840, "fx3.c FX3 send / return bus records and planes  (semi)"),
    (0x2001D000, 0x1000, "master.c Mix dry copy / recmon.c monitor signal (2 x 512 floats)"),
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
        for m in re.finditer(r"0x((?:2406|2001)[0-9a-fA-F]{4})u?\b", code):
            consts.setdefault(int(m.group(1), 16), []).append(f"{f}:{ln}")
outside = {hex(a): w for a, w in consts.items() if not any(s <= a < s + n for s, n, _ in regs)}
check(f"every patch RAM constant in the built sources ({len(consts)}) lies in a registered region", not outside, outside)

print(f"\n{'FAILED: ' + str(failures) if failures else 'all passed'}")
sys.exit(1 if failures else 0)
