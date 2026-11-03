# bitbox-mk2-semi

Community firmware for the **1010music bitbox MK2** (firmware 2.3.4), ported from
[portkit](https://github.com/kyrillleier-jpg/portkit-micro) (bitbox micro 2.3.4, MIT), which brings Fidelity /
vintage sampler modes and 24 dB filters from Justin Joe's [blackbox-mod](https://github.com/j3threejay/blackbox-mod).
The micro and the MK2 run the same 1010music framework at the same load base, so portkit's engine code ports by
address translation; the UI is placed anew for the MK2's screens and its two keys (HOME, INFO).

**Patch in your browser:** https://semierendonmez-commits.github.io/bitbox-mk2-semi/ (drop your stock 2.3.4
`BITBOXMK2.bin`; the page holds only this project's bytes and checks both SHA-256 sums; nothing is uploaded).
`tools/make_web.py` builds it from the tested build.

> Unofficial. Not affiliated with or supported by 1010music. Running modified firmware is at your own risk and may
> affect your warranty. Keep your stock `BITBOXMK2.bin` and flash it back before sending a unit in for service.

## Status: build 2.3.4-sL

**sL boots on a unit** (first hardware test, 2026-11-03; sA..sK did not: see CHANGELOG). The features themselves are
**emulation** = verified in the Unicorn tests and the Renode simulation of the MK2; hardware checks per feature follow.

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
| **Master compressor** settings: Comp Type Stock / SSL (SSL: ratio 2/4/10, attack 0.1–30 ms, release 0.1–1.2 s / Auto), Threshold, Ratio, Attack, Release, Gain, **Mix** (parallel) | Tools → **Comp** tab and **More** → Comp 2 (Compressor list stays on Main) | emulation |
| **Master distortion** Soft / Hard / Fold / Tube, 2x oversampled: Drive, Tone, Mix, Level (Dist Lvl on Cmp2) | Tools → **Dist** tab | emulation |
| **FX Out**: FX returns to Out1/2 (stock), Dly FX1/2, Rvb FX1/2, FX1/2, Dly1 Rvb2 | Tools → Rec (4th slot) | emulation (Renode audio + Unicorn) |
| **Delay types** Digital / Tape / BBD; page 2 (Type "Tape >"): Mod Rate, Mod Depth, Flutter, Drive, Tone, **Level**, Mod Phase | FX1 cell → INFO | emulation |
| **Reverb page 2** (Type "Hall >"): Bass Mult, Crossover, Mod Rate, Mod Depth, Width, Hi Cut, Lo Cut | FX2 cell → INFO | emulation |
| **Compressor bus**: Off / Out1/2 / FX1/2 / Both | Tools → Main "Compressor:" | emulation |
| **FX1 / FX2 Send** as numeric % rows | pad → Pos list (Multisample: Conf) | emulation |
| **FX3** third send (reverb, all types, two pages): touch the selected FX2 cell again -> **FX 3**; **FX3 Send** in the Pos list | pad grid, FX2 column; INFO | emulation (Renode audio + Unicorn) |
| **Slicer Sync Grid**: Free / 1 bar .. 1/32; with Sync On every slice lasts a whole number of grid steps | Slicer pad → Pos list, after Sync | emulation (Unicorn; Renode UI) |
| **Slice Str**: Cut (stock) / Stretch (time stretch, pitch kept) / Tape (varispeed); slices fill their synced duration | Slicer pad → Pos list, after Sync Grid | emulation (Renode audio + Unicorn) |
| **Screen saver**: sweeping scope of Out1/2 + meters (Scope / Meters / Dark), 1–30 min or Off; wakes on touch (swallowed), keys, MIDI | Tools → More → Page: Saver | emulation (Renode screenshots + Unicorn) |
| **Tools → More** (was Cmp2): Page list Comp 2 / Dist 2 / Saver / CV In / Quantize / Gen … | Tools | emulation |
| **MOD sources KEY** (keyboard tracking) **and ENV** (the pad's ADSR as a source, per pad) | pad → MOD page → Source | emulation (Unicorn: real route code) |
| **Double tap a dial → default value** (modulation untouched; slow taps do nothing) | every dial page (pad pages, FX cells) | emulation (Renode: Pitch, Res) |
| **Vertical waveform zoom**: spread / pinch two fingers vertically on the sample waveform (x1..x32, display only) | pad → INFO (waveform) | emulation (Renode, two-finger touch model) |
| **CV In scale / offset** (0..400 %, −100..+100 %; e.g. 0–5 V over the full range: 200 %, −100 %) | Tools → More → Page: CV In | emulation (Unicorn on the real engine handler, Renode page) |
| **Quantize**: the pitch of pads played from a CV input snaps to a scale (11 scales, root C..B) | Tools → More → Page: Quantize | emulation (Unicorn) |
| **GEN generative sequencer**: 4 lanes play pads on the MK2 clock; Euclid, Breakcut, Call+Resp 3+1, Evolve, Drummap, Random; Epoch arc | Tools → More → Page: Gen, Gen 2, L1–L4 Gen / Shape / Cond | emulation (Unicorn: every algorithm and condition; Renode: pads play from GEN) |
| **Step conditions** per lane: 1:2 … 4:4, Fill / !Fill, First / !First, Prob % | Tools → More → Ln Cond | emulation (Unicorn) |
| Splash / Menu version **semi mod 2.3.4-sL** (boots on a unit) | boot, Menu lower right | emulation |

Not ported yet: the recording sends / monitor (recmon.c), pads 9–16 (not needed: the MK2 has 16 pads).

**Two-page FX cells:** the FX cell page shows eight controls. Delay and reverb carry their second page in the Type
list: "Tape" shows the delay's stock controls with Type in the last slot, "Tape >" the new ones (Type in the same
place). Turn Type one step to flip pages; the sound type stays the same.

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
.venv/bin/python -I patch.py fidelity pages sound reverb fixes master fx fx3 ss modsrc ui gen cave
                      # -> out/fidelity+pages+sound+reverb+fixes+master+fx+fx3+ss+modsrc+ui+gen+cave/BITBOXMK2.bin
.venv/bin/python -I tests/test_fidelity.py                   # Unicorn tests on the patched image
.venv/bin/python -I tests/test_reverb.py
.venv/bin/python -I tests/test_ram.py
.venv/bin/python -I tests/test_fixes.py
.venv/bin/python -I tests/test_master.py
.venv/bin/python -I tests/test_fx.py
.venv/bin/python -I tests/test_gen.py
```

`patch.py` only writes an image when every check passes: stock hash, expected stock bytes at every patch site, zero
repack, every change inside a registered range, every redirected call lands on a function of ours, every caller of a
replaced function is hooked. Build sE: 706,850 bytes (cave 31,954 bytes). The MK2 bootloader's upper limit is not known: vwal's
2.3.4+c (696,708 bytes) is known to run; the flash bank ends at 0x08100000 (786,432 bytes from the load base).

### Simulation (Renode)

```sh
.venv/bin/python -I sim/mk2_card.py out/sim/mk2_sd.img       # test card: one preset, Sample / Clip / Slicer / Granular
cp out/fidelity+pages+sound+reverb+fixes+master+fx+fx3+ss+modsrc+ui+gen+cave/BITBOXMK2.bin out/mk2/
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
