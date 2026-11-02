# ported to the bitbox MK2 2.3.4 by tools/port_mk2.py
"""The compiled code cave (./build.sh), appended after the stock image, plus the splash / Menu marking a modified
build: line 2 "by 1010music" -> "semi mod", line 3 "2.3.4" -> "2.3.4-s<BUILD>". Slots are 16 bytes (MK2 0x080c68ec,
0x080c68fc; micro 0x080c5760 / 0x080c5770); the version also shows at the lower right of the Menu screen.

Builds (portkit's micro history up to pkZD: portkit-micro CHANGELOG.md):
  A = first MK2 build: cave, CPU load, Fidelity + 24 dB SVF, Drive / Filter 2, clip Shift / Play Len / Stretch /
      Loop Fade / Sync 1/32, reverb types; MK2 page placement (LFO page, Pos list, Conf list)
"""
import os

from thumb import ROOT, STOCK_END

BUILD = b"A"


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
