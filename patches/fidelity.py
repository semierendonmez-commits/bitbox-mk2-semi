# ported to the bitbox MK2 2.3.4 by tools/port_mk2.py
"""Fidelity (Interp list + pad crunch + machine modes) and the 24 dB SVF in place of the stock pad biquad
(src/filter.c). Needs the code cave from ./build.sh, appended by `cave`. Addresses: re/symbols.md."""
import os

from thumb import ROOT, bl, symbols

sym = symbols(os.path.join(ROOT, "out", "cave.elf"))

# (call site, stock callee, cave symbol). Each list covers EVERY call to its stock callee in the image; patch.py checks
# that, so a missed caller fails the build instead of running the stock path unnoticed.
HOOKS = [
    # parameter registration: Interp list 2 -> 14 entries, label "Fidelity:"
    (0x0806B73E, 0x0806A27C, "interp_register"),
    # per-voice filter process: class A (sample, granular), class B (clip, slicer)
    (0x080AE9BC, 0x080B9548, "filter_voice_a"),
    (0x080B6DF4, 0x080B9548, "filter_voice_b"),
    # machine modes: park pad / position / rate on the way into the sample-voice reader, then swap its two kernels
    (0x080AFEEE, 0x080AECA8, "voice_read"),
    (0x080B0134, 0x080AECA8, "voice_read"),
    (0x080B01F6, 0x080AECA8, "voice_read"),
    (0x080AEF5C, 0x080A27A8, "read_normal"),
    (0x080AEF80, 0x080A27A8, "read_normal"),
    (0x080AEFA4, 0x080A27A8, "read_normal"),
    (0x080AEDA8, 0x080A283C, "read_hq"),
    (0x080AEE34, 0x080A283C, "read_hq"),
    (0x080AEE58, 0x080A283C, "read_hq"),
]

# Stock callees whose every caller must be hooked. register_list has many other callers (every list parameter), so it
# is not in here.
EXCLUSIVE = {0x080B9548, 0x080AECA8, 0x080A27A8, 0x080A283C}

PATCHES = [(at, bl(at, stock), bl(at, sym[name])) for at, stock, name in HOOKS]
