# ported to the bitbox MK2 2.3.4 by tools/port_mk2.py
"""The compiled code cave (./build.sh), appended after the stock image, plus the splash / Menu marking a modified
build: line 2 "by 1010music" -> "semi mod", line 3 "2.3.4" -> "2.3.4-s<BUILD>". Slots are 16 bytes (MK2 0x080c68ec,
0x080c68fc; micro 0x080c5760 / 0x080c5770); the version also shows at the lower right of the Menu screen.

Builds (portkit's micro history up to pkZD: portkit-micro CHANGELOG.md):
  A = first MK2 build: cave, CPU load, Fidelity + 24 dB SVF, Drive / Filter 2, clip Shift / Play Len / Stretch /
      Loop Fade / Sync 1/32, reverb types; MK2 page placement (LFO page, Pos list, Conf list)
  B = fix: Clip / Slicer Poly Mode Mono honoured (patches/fixes.py)
  C = master section: compressor settings (Stock / SSL, Mix) and master distortion on Tools tabs Comp / Cmp2 / Dist
  D = FX: return routing (FX Out) and Delay Level, delay types Digital / Tape / BBD with modulation and loop colour,
      two-page delay and reverb cells, Compressor bus (Out1/2, FX1/2, Both), numeric FX sends in the Pos list
  E = FX3: a third send (reverb, every type) on the FX2 cell (second touch: FX 3), FX3 Send in the Pos list
  F = Slicer Sync Grid (slice durations on a 1 bar .. 1/32 grid with Sync On)
  G = Slice Str: Cut / Stretch (WSOLA) / Tape (varispeed) for synced slices
  H = screen saver with a live monitor (scope, meters), wake on touch / keys / MIDI; Tools -> More with pages
  I = MOD sources KEY (keyboard tracking) and ENV (the pad's envelope)
"""
import os

from thumb import ROOT, STOCK_END

BUILD = b"J"


def slot(text, size=16):
    assert len(text) < size
    return text + b"\0" * (size - len(text))


VERSION = b"2.3.4-s" + BUILD
assert len(VERSION) <= 10, "keep the version short (the micro panel showed 10 characters; MK2 Menu: to check)"

PATCHES = [
    (0x080C68EC, slot(b"by 1010music"), slot(b"semi mod")),
    (0x080C68FC, slot(b"2.3.4"), slot(VERSION)),
    (STOCK_END, b"", open(os.path.join(ROOT, "out", "cave.bin"), "rb").read()),
]
