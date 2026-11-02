# bitbox MK2 2.3.4: platform notes

Image `BITBOXMK2.bin` 2.3.4, 674,896 bytes, SHA-256 `35ab7995…3e86`. Load base 0x08040000 (bootloader below, not in
the file), reset 0x08040515, initial SP 0x2406AD98 (micro: 0x240692A8). Status of everything here: **vermutet** unless
marked; the full address list with evidence is `re/mk2_map.py`, portkit's micro notes are `re/symbols.md` (German).

## Relation to the micro

| | micro 2.3.4 | MK2 2.3.4 |
|---|---|---|
| size | 671,376 | 674,896 |
| instruction windows of the micro found once in the MK2 | | 70 % (tools/micro2mk2.py) |
| parameter table | 0x1d1 entries (0x32dc bytes), 240 registrations, max id 0x17b | identical (tools/emu_params_mk2.py, **Emulation**) |
| page tables | 6 pages x 45 ids | 5 pages x 36 ids (Main, Pos, LFO, Gran, Conf; labels 0x080c9804) |
| page types | | Main / LFO / Gran: dial grid, 2 groups of 2 x 2, filled column-wise (tl, bl, tr, br); Pos / Conf: scrolling list (Renode screenshots) |
| keys | 4 (HOME, TOOLS, LEFT, RIGHT) | 2: table 0x2400B2C0 (2 x 0x18), poll 0x08041908; PC2 = HOME, PC3 = INFO (Renode) |
| app object | 0x24013CB0 | 0x24015170 (63/63 literals); pad records app + 0x1840 + row * 0xf0 + col * 0x30 and FX records app + 0x2200 + row * 0x18 same; high offsets differ (dispatcher 0x8007 -> 0x7f07) |
| display | 240 x 320 portrait (UI rotated) | LTDC 320 x 240 landscape, same UI coordinate space |
| FatFS diskio table | 0x080BF030 | 0x080C0200 (init 0x08045CC8, status 0x08045EA4, read 0x08045D68, write 0x08045DD8, ioctl 0x08045E4C) |
| audio task calls | in 0x08093D44, out 0x08093E48 | in 0x080952D8, out 0x08095364 (sites 0x08042354 / 0x08042360 / 0x08042370 / 0x0804237C) |
| page builder | 0x08076D8C, 7 callers | 0x08078EC8, 6 callers (0x08087E26, 0x08087E8C, 0x080912DE, 0x08091EE8, 0x08093A02, 0x080943AC) |

## RAM above the stack (Renode canary, stock MK2)

Filled 0x2406AD98..0x2407FFFF with 0xA5 before boot; boot, preset load, four pads played, dump: firmware writes
0x24070DC0-0x24070E17, 0x24070F40-0x24070F97, 0x240711A0-0x2407939F (SD / file buffers), 0x2407D5C0-0x2407FFFF
(audio / ADC DMA). Free for this project: 0x2406AD98..0x24070DBF (24.6 KB). **Recording not exercised yet.**

## Pad tables (page builder literals)

Clip A 0x080C7BD4, Clip B 0x080C7D3C, Granular A 0x080C7F3C, Granular B 0x080C80A4, Sample A 0x080C84F4, Sample B
0x080C865C, Multisample 0x080C87C4, Slicer A 0x080C8A04, Slicer B 0x080C8B6C. Page n at table + n * 0x48.

## Simulator (sim/mk2.repl)

The micro platform plus a DWT stub: the MK2 busy-waits on DWT CYCCNT (0xE0001004) at 0x08045AE2 / 0x08045AF4,
which Renode 1.16's H743 lacks. On Linux, Renode needs an open stdin (sim/run.py). Boots to the Pads screen with the
virtual SD card, preset loads, touch works (GT911 at I2C1 0x5D, no y flip), audio engine runs, output capture works
(slots 0 / 2 / 4 / 6 carry signal). Encoder not modelled.
