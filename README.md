# bitbox-mk2-semi

Community firmware for the **1010music bitbox MK2** (firmware 2.3.4), ported from
[portkit](https://github.com/kyrillleier-jpg/portkit-micro) (bitbox micro 2.3.4, MIT), which brings Fidelity /
vintage sampler modes and 24 dB filters from Justin Joe's [blackbox-mod](https://github.com/j3threejay/blackbox-mod).
The micro and the MK2 run the same 1010music framework at the same load base, so portkit's engine code ports by
address translation; the UI is placed anew for the MK2's screens and its two keys (HOME, INFO).

> Unofficial. Not affiliated with or supported by 1010music. Running modified firmware is at your own risk and may
> affect your warranty. Keep your stock `BITBOXMK2.bin` and flash it back before sending a unit in for service.

## Status: build 2.3.4-sB

**emulation** = verified in the Unicorn tests and the Renode simulation of the MK2, **not yet on a unit**.
Nothing here is hardware-tested yet.

| Feature | Where on the MK2 | Status |
|---|---|---|
| **Fidelity**: Interp list Normal, HighQ, SP1200, SP-12, S950, MPC60, SP Raw, 16k..2k | pad → INFO → INFO → **Conf** list (Sample, and now also Clip, Slicer, Granular) | emulation |
| **24 dB filter** (SVF instead of the pad biquad), same Filter / Res dials | Main page | emulation |
| **Drive**, **Filter 2**, **Res 2** (second 24 dB filter in series) | **LFO** page, right-hand group | emulation |
| **Clip Shift** (−100..+100 ms), **Play Len**, **Stretch %** (needs Sync), **Str Mode** Auto / Jump / Smooth (Smooth keeps the pitch), **Loop Fade** | Clip pad → **Pos** list, after Sync | emulation |
| **Sync 1/32**; Play Len / Stretch hidden while Quant / Sync make them ineffective | Pos list | emulation |
| **Reverb types** Plate / Room / Hall / Chamber with ten settings and **Level** | FX2 → INFO (Type, Level shown on Plate) | emulation (Plate page) |
| **CPU load** after the free-memory text | status bar (e.g. `243M  34%`) | emulation (the simulator's load value is not meaningful) |
| **Fix: Clip / Slicer Poly Mode Mono honoured** (stock: a saved Sample Poly Mode overrides it, slices overlap) | Conf list | emulation (Renode + Unicorn) |
| Splash / Menu version **semi mod 2.3.4-sB** | boot, Menu lower right | emulation |

Not ported yet: FX3 (third send), master compressor / distortion, recording sends / monitor (portkit reaches their
pages with the micro's arrow keys; the MK2 needs its own placement), pads 9–16 (not needed: the MK2 has 16 pads).

**Presets:** values are stored by parameter name (`pkdrive`, `pkshift`, ...). Stock presets load as before; the new
controls start at their defaults. The parameter table and ids are identical on micro and MK2 (240 registrations,
`tools/emu_params_mk2.py`), so portkit's ids are free here too.

## Build from source

Linux or macOS, Python 3, `arm-none-eabi-gcc`, the stock MK2 2.3.4 `BITBOXMK2.bin` in `firmware/` (SHA-256
`35ab7995ad0eca8484f3b28bc76389f10b2037dbb21a4a394e950d9329073e86`, from 1010music's download page). For address
work also the micro 2.3.4 `MICRO.BIN` (SHA-256 `54ecc3e5…4365`). Optional: [Renode](https://renode.io) 1.16+.

```sh
./setup.sh
./build.sh                                                   # src/ -> out/cave.elf, out/cave.bin
.venv/bin/python -I patch.py fidelity pages sound reverb fixes cave
                      # -> out/fidelity+pages+sound+reverb+fixes+cave/BITBOXMK2.bin
.venv/bin/python -I tests/test_fidelity.py                   # Unicorn tests on the patched image
.venv/bin/python -I tests/test_reverb.py
.venv/bin/python -I tests/test_ram.py
.venv/bin/python -I tests/test_fixes.py
```

`patch.py` only writes an image when every check passes: stock hash, expected stock bytes at every patch site, zero
repack, every change inside a registered range, every redirected call lands on a function of ours, every caller of a
replaced function is hooked. Build sA: 696,340 bytes (cave 21,444 bytes, ends 0x080EA314; the MK2 bootloader's upper
limit is not known - vwal's 2.3.4+c, 696,708 bytes, runs).

### Simulation (Renode)

```sh
.venv/bin/python -I sim/mk2_card.py out/sim/mk2_sd.img       # test card: one preset, Sample / Clip / Slicer / Granular
cp out/fidelity+pages+sound+reverb+fixes+cave/BITBOXMK2.bin out/mk2/
.venv/bin/python -I sim/run.py --image semi --repl mk2.repl --sd out/sim/mk2_sd.img \
    wait 10 shot pads tap 30 210 0.15 wait 0.8 press K1 wait 1 press K1 wait 1 shot params
```

Keys: `K0` = HOME (PC2), `K1` = INFO (PC3). Touch in screen coordinates (320 x 240, y from the top). The encoder is
not modelled yet. See `sim/README.md` (portkit's micro notes) and the MK2 notes in `re/symbols-mk2.md`.

### Porting more from portkit

`tools/micro2mk2.py` translates micro addresses (code by instruction windows, data by content, RAM by literals);
`re/mk2_map.py` is the curated map; `tools/port_mk2.py survey|apply <files>` rewrites a portkit source through it
(files already ported carry a marker line). Sources without the marker (`src/master.c`, `src/fx3.c`, `src/recmon.c`,
`src/padpages.c`, their thunks and patch sets) are still portkit's micro versions and are not built.

## Credits and licence

Ported from portkit (MIT, portkit contributors); Fidelity modes, the 24 dB filter and the BL encoder from blackbox-mod
by Justin Joe (MIT); the filter follows Emilie Gillet's stmlib (MIT). See `THIRD_PARTY.md`. This project's code: MIT,
see `LICENSE`. Feature ideas also from vwal's [bitbox MkII 2.3.4+c patcher](https://vwal.github.io/bitbox-mkii-patcher/)
(no code taken: that project publishes no source and no licence). The 1010music firmware is not part of this project and
not covered by any licence here. Please share the build instructions, not patched firmware files.
