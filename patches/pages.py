"""bitbox MK2 2.3.4: Fidelity (Interp, 0xd2) on the Conf list of Clip, Slicer and Granular pads, right after Excl
Group as on Sample pads (stock has it only on Sample / Multisample). Data only.

MK2 page tables: 5 pages x 36 halfword parameter ids per pad mode (two variants per mode), selected in the page
builder 0x08078ec8; page 4 is Conf (a list). Clip / Slicer / Granular pads store 0xd2 like Sample pads (the full pad
record, as on the micro; see re/symbols.md "Pad-Seiten").
"""
import struct

P_INTERP, P_EXCL = 0xD2, 0xA6
LAUNCH, OUTPUT, PAN, MIDI_IN, ROOT, PAD_NOTE = 0x75, 0x77, 0x45, 0x4D, 0xB2, 0xD6
POLY_CS, POLY_S = 0x83, 0x82

CONF = 4 * 36 * 2


def insert_after_excl(table, ids):
    """Conf page of `table` holds `ids` then zeros; put Interp after Excl Group. Checks one spare zero slot."""
    at = table + CONF
    old = struct.pack(f"<{len(ids) + 1}H", *ids, 0)
    k = ids.index(P_EXCL) + 1
    new_ids = ids[:k] + [P_INTERP] + ids[k:]
    return (at, old, struct.pack(f"<{len(new_ids)}H", *new_ids))


CLIP = [LAUNCH, OUTPUT, PAN, P_EXCL, POLY_CS, MIDI_IN, PAD_NOTE]
SLICER_A = [LAUNCH, OUTPUT, PAN, P_EXCL, POLY_CS, MIDI_IN, PAD_NOTE]
SLICER_B = [LAUNCH, OUTPUT, PAN, P_EXCL, POLY_S, MIDI_IN, PAD_NOTE]
GRANULAR = [LAUNCH, OUTPUT, PAN, P_EXCL, MIDI_IN, ROOT, PAD_NOTE]

PATCHES = [
    insert_after_excl(0x080C7BD4, CLIP),
    insert_after_excl(0x080C7D3C, CLIP),
    insert_after_excl(0x080C8A04, SLICER_A),
    insert_after_excl(0x080C8B6C, SLICER_B),
    insert_after_excl(0x080C7F3C, GRANULAR),
    insert_after_excl(0x080C80A4, GRANULAR),
]
