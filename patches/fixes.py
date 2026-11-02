"""Bug fixes in the stock MK2 2.3.4 behaviour. Data / immediate edits only, no cave code.

Slicer / Clip "Poly Mode: Mono" not honoured (forum report by semi4136, 1010music: "Slicers in mono do not always honor
the mono setting", logged). Clip and Slicer pads show their own Poly Mode (0x83 "polymodeslice": Mono, Poly 2); Sample
pads use 0x82 ("polymode"). The engine's clip / slicer pad (class B, parameter handler 0x080b7e10) only handles 0x82:
its case sets the mono flag pad +0x1f7 (value 0 -> 1), and 0x83 falls through to "ignore". The app hides this on a
UI change: the dispatcher 0x0807f7bc stores 0x83 and then sends the value to the engine as 0x82 (0x0807fcd6..0x0807fce2).
But a loaded preset sends both saved values, so a slicer whose saved 0x82 is a Poly value (left over from a Sample
pad, or from an older preset) runs polyphonic while its page shows Mono: a new slice does not stop the playing one,
which is what you hear with PlayThru on (slices keep going). Reproduced in the Renode simulation (sim/slicer_card.py
--samplepoly 5: pad +0x1f7 = 0, both voices pad +0x260 / +0x670 active after a kick and a snare slice).

Fix: the class-B handler takes the mono flag from 0x83 and ignores 0x82; the dispatcher sends 0x83 itself.
  - tbh table 0x080b7e40 (index = id - 2): entry 0x83 -> the mono case 0x080b845e, entry 0x82 -> ignore 0x080b8022
  - 0x0807fce0 `movs r4, #0x82` -> `movs r4, #0x83` (store + send under the Clip / Slicer id)
Sample pads (class A, their own handler) keep 0x82 unchanged.
"""
import struct

TBH = 0x080B7E40
MONO_CASE, IGNORE = 0x080B845E, 0x080B8022


def tbh_entry(pid, old_target, new_target):
    at = TBH + 2 * (pid - 2)
    return (at, struct.pack("<H", (old_target - TBH) // 2), struct.pack("<H", (new_target - TBH) // 2))


PATCHES = [
    tbh_entry(0x83, IGNORE, MONO_CASE),
    tbh_entry(0x82, MONO_CASE, IGNORE),
    (0x0807FCE0, bytes.fromhex("8224"), bytes.fromhex("8324")),
]
