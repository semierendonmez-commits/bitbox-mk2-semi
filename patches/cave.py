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
  J = double tap -> default, vertical waveform zoom, CV In scale / offset, CV pitch quantizer
  K = GEN generative sequencer with step conditions
  L = patch RAM moved to the top of DTCM and zeroed at boot (sA..sK did not boot on a unit)
  M = FX1 / FX2 / FX3 Send modulatable; GEN: Pad Sel + numbering from the bottom left, velocity, Breakcut rearranges
      the sample / slices (Cut), Call+Resp melodies (Scale, Range); Tape / BBD delay character
  N = GEN: Resp Pad, Breakcut procedures (BBCut), Acid, Turing, Learn, Turn, Drummap Y; Codec delay, Fidelity Phone /
      ADPCM / u-Law; MOD sources CHAOS / RAND / NET; saver views Barcode / Matrix / Rain / Pulse; colour themes
"""
import os

from thumb import ROOT, STOCK_END, bl, symbols

BUILD = os.environ.get("SEMI_TAG", "N").encode()   # SEMI_TAG: test builds (sT1 ..)


def slot(text, size=16):
    assert len(text) < size
    return text + b"\0" * (size - len(text))


VERSION = b"2.3.4-s" + BUILD
assert len(VERSION) <= 10, "keep the version short (the micro panel showed 10 characters; MK2 Menu: to check)"

sym = symbols(os.path.join(ROOT, "out", "cave.elf"))
SYSTEM_INIT = 0x08054824
HOOKS = [(0x08040518, SYSTEM_INIT, "boot_zero")]     # reset handler's first call: zero the patch RAM first
EXCLUSIVE = {SYSTEM_INIT}

PATCHES = [(at, bl(at, stock), bl(at, sym[name])) for at, stock, name in HOOKS] + [
    (0x080C68EC, slot(b"by 1010music"), slot(b"semi mod")),
    (0x080C68FC, slot(b"2.3.4"), slot(VERSION)),
    # patch RAM: the top 24 KB of DTCM (0x2001a000..0x2001ffff, tests/test_ram.py). The DTCM bump allocator
    # (0x08043b88) stops at 0x1a000 instead of 0x20000; bigger requests fall back to malloc / SDRAM as before.
    (0x08043BA6, bytes.fromhex("b2f5003f"), bytes.fromhex("b2f5d03f")),
    (STOCK_END, b"", open(os.path.join(ROOT, "out", "cave.bin"), "rb").read()),
]
