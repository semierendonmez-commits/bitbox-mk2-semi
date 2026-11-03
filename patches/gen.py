"""bitbox MK2 2.3.4 GEN generative sequencer (src/gen.c; its settings and pages in src/master.c): more parameter ids and
a bigger settings store.

- The parameter table (0x0806a300: 0x1d1 entries of 28 bytes, ids 0..0x1d0, all used by stock and this project) is
  made 0x2fd entries (ids up to 0x2fc): its allocation and clear (0x0806a30a, 0x0806a31c), the lookup by xml name
  (0x0806c180) and the eight bound checks (register 0x0806a24e, getters 0x0806c004 .. 0x0806c120).
- The settings store (init 0x08089cbc: capacity 0x40, 59 entries in use) gets capacity 0x80 for GEN's 41 settings.
Needs `master` and the cave."""

def h(s):
    return bytes.fromhex(s)

BOUND_R3 = (h("b3f5e87f"), h("b3f53f7f"))           # cmp.w r3, #0x1d0 -> #0x2fc
BOUND_R0 = (h("b0f5e87f"), h("b0f53f7f"))           # cmp.w r0, #0x1d0 -> #0x2fc
SIZE = {0: (h("43f2dc20"), h("45f2ac30")), 2: (h("43f2dc22"), h("45f2ac32")), 3: (h("43f2dc23"), h("45f2ac33"))}

PATCHES = [(0x0806A24E, *BOUND_R3)] \
    + [(a, *BOUND_R0) for a in (0x0806C004, 0x0806C030, 0x0806C05C, 0x0806C088, 0x0806C0B4, 0x0806C0E8, 0x0806C120)] \
    + [(0x0806A30A, *SIZE[0]), (0x0806A31C, *SIZE[2]), (0x0806C180, *SIZE[3])] \
    + [(0x08089CC0, h("4021"), h("8021"))]           # settings store capacity 0x40 -> 0x80
