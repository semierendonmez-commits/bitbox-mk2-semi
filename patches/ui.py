"""bitbox MK2 2.3.4 UI additions (src/dialdef.c): double tap on a dial -> its default value (the dial widget's
message handler, vtable slot 0x080c9528). Needs the cave."""
import os
import struct

from thumb import ROOT, bl, symbols

sym = symbols(os.path.join(ROOT, "out", "cave.elf"))

DIAL_MSG = 0x0808FA25                                # dial widget message handler, class vtable 0x080c94f8 [12]
WORDS = [(0x080C9528, DIAL_MSG, "dialdef_msg")]

PATCHES = [(at, struct.pack("<I", old), struct.pack("<I", sym[name] | 1)) for at, old, name in WORDS]

# vertical waveform zoom (src/vzoom.c, src/vzoom_thunk.S)
WAVE_MOVE = 0x08062511                               # waveform widget touch move, vtable 0x080c1494 [5]
WORDS += [(0x080C14A8, WAVE_MOVE, "vzoom_move")]
VCVT = bytes.fromhex("b8eee78a")                     # vcvt.f32.s32 s16, s15 (the tile painter's amplitude scale)
THUNKS = [(at, VCVT, "vzoom_scale_thunk") for at in (0x08062A60, 0x08062B16, 0x08062C18)]

PATCHES = [(at, struct.pack("<I", old), struct.pack("<I", sym[name] | 1)) for at, old, name in WORDS] \
    + [(at, old, bl(at, sym[name])) for at, old, name in THUNKS]

# CV input scale / offset, pitch quantizer (src/cvq.c): the engine's CV event call
HOOKS = [(0x08095C22, 0x080967E8, "cvq_event")]
EXCLUSIVE = {0x080967E8}

PATCHES += [(at, bl(at, stock), bl(at, sym[name])) for at, stock, name in HOOKS]
