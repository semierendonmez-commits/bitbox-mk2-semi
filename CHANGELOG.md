# Changelog

## bitbox-mk2-semi

Builds for the **bitbox MK2** on top of the stock firmware **2.3.4**, named `2.3.4-s<build>`.

### 2.3.4-sN (2026-11-03)

- **Call+Resp across pads** (**Ln Resp → Resp Pad**: Same, Sel, 1–16): the three calls play on the lane's pad, the
  response on another pad (Same = as before).
- **Breakcut procedures** (**Gen 4 → Proc**: Basic (sM's), Classic, ChooseCut, Jungle, WarpCut, SQPusher, CageCut,
  Timeline, Permute, ThrashCut; Nick Collins' BBCut via our OpenTracker fork). Mutate % is each procedure's amount.
  **Rolls**: Off / Normal / Dense / Fade (rolls now play several hits inside a step); **Recut**: new cuts every
  cycle, every 2 / 4 / 8 cycles, or Locked. On Slicer pads the segments follow the pad's real number of slices.
- **Acid lane** (Algo Acid, after TB-3PO): Hits = density (sparse root-heavy … busy melodic), Scale / Range from
  Gen 3, **Gen 5 → Accent %, Slide %** (a slide ties the note into the next); Mutate % re-rolls steps each cycle.
- **Turing lane** (Algo Turing): a 16-bit shift register, **Gen 5 → Lock** (+100 locked loop of Steps, 0 random,
  −100 double length inverted), Hits = density, pitch from Scale / Range.
- **Learn lane** (Algo Learn, **Gen 6 → Learn: On**): hits you play on the lane's pad teach a Markov chain of the
  gaps and where in the cycle you hit; the lane plays variations of it (RAM only, gone after power off).
- **Turn** (**Gen 6 → Turn**: Off / 8 / 16 / 32 bars): the last beat (16 / 32 bars: the last two) of every phrase
  turns: a roll, a stutter of the beat before, a drop (silence) or a fill — from Seed.
- **Drummap X / Y**: Rotate = X, **Gen 6 → Map Y** = Y: four maps per part (new: broken / garage and half-time).
- **Delay type Codec**: each repeat through IMA ADPCM at 4–16 kHz (Tone) — generation loss; Drive drops 10 ms frames
  (packet loss, up to 30 %).
- **Fidelity Phone / ADPCM / u-Law**: 8 kHz mu-law with a 3.4 kHz low-pass / 4-bit ADPCM at 22 kHz / mu-law at 16 kHz.
- **MOD sources CHAOS** (a Lorenz attractor), **RAND** (a new random value on every note, held), **NET** (four
  coupled nonlinear nodes, slower). Sample / Granular pads (as ENV); also usable on the sends.
- **Saver views** Barcode, Matrix, Rain, Pulse (DATASCREEN-style views of the pads and GEN's lanes; Saver → View).
- **Colour themes** (**Saver → Theme**: Stock, Iznik, Acetate, Pirate FM, Oscillo, Riso, Bauhaus, Aurora,
  Concrete, Soundsys): the UI palette's pad, background, selection and accent colours, the DMA2D colour tables
  reloaded; the screen takes them as it redraws.
- Not done yet: "Breaks" auto-slicing and automatic root note detection (they need the slicer's Scan and the sample
  loader worked out first).

### 2.3.4-sM (2026-11-03)

From the first hardware test of sL:
- **FX1 / FX2 / FX3 Send can be modulated.** INFO on the send rows of the Pos list opens the MOD page (stock: the
  sends are not in the modulatable list, nothing happened). Sources: LFO, VEL, KEY, ENV, MVOL, MPAN, PTCH, MODW on
  Sample / Granular pads; on Clip / Slicer pads every source their engine resolves. The send follows knob + amount ×
  source every audio block (clamped 0..100 %), through the stock send curve; removing the route brings the knob back.
- **GEN Pad numbering**: 1 = bottom left, 2 its right … 5 the row above (was: 1 = top left). New first entry **Sel**:
  the lane plays the pad selected on the grid (an FX cell selected: the last selected pad). Defaults: lanes on pads
  1, 2, 5, 9.
- **GEN velocity** now reaches the pads (it was written to the wrong field of the note message).
- **Breakcut rearranges the sound** (new **Gen 3 → Cut: On**, default): every cut plays a segment of the pad: in place,
  a jump to another block of the cut's size, or a stutter of the last cut (Mutate % sets how often); the closing roll
  repeats one segment. Sample pads: the segment = Steps-th of the sample (Start moved in the engine only; the pad's
  Start comes back when Run / Cut is off; the screen and the preset keep it). Slicer pads: step k = slice k (set Steps
  to the number of slices). Note offs at the end of each cut. Cut: Off = triggers only (as before).
- **Call+Resp plays melodies** (**Gen 3 → Scale** (Chromatic … Whole, default Pent Min), **Range** 0–24 semitones,
  default 12): the call is a melody walking over the scale from the pad's pitch (the same in its three cycles), the
  response answers it upside down and ends on the pad's pitch. Range 0 = triggers only. On a Slicer pad the melody
  picks slices.
- **Delay types with their own character at default settings**: Tape is driven (compression), loses highs (~8 kHz)
  and lows (90 Hz), with its own wow and flutter; BBD is dark (~4 kHz), loses lows (140 Hz), drifts, and its clock
  follows the delay time: longer delays hold samples (one more per 200 ms, up to 8) and close the filter — grittier,
  darker long echoes. (sL: at Drive 0 / Depth 0 they were hardly different from Digital.)

### 2.3.4-sL (2026-11-03)

- **Fix: the unit did not boot (black screen after the update) with sA..sK.** A unit starts with garbage in RAM, the
  simulator with zeros; the ENV modulation table was read before anything wrote it and followed a garbage pad pointer
  in the audio block. The patch RAM (now the top 24 KB of DTCM, reserved from the firmware's DTCM allocator) is
  cleared at boot before anything else runs. Found by a hardware bisect (probe builds sT1..sT9) and reproduced in
  Renode, which now fills RAM with a pattern before boot. **First build that boots on a unit** (Menu: 2.3.4-sL).
- Contains everything from sA..sK.

### 2.3.4-sK (2026-11-03)

- **GEN, a generative step sequencer** (the MK2 has no sequencer of its own: the framework's blackbox sequencer code,
  including its "PLAY: %" step field, has no MK2 screen, so nothing in 2.3.4 ever evaluates it). Four lanes play pads
  locked to the MK2's clock (internal tempo or MIDI clock; the song position the Clip sync uses). A hit is the same
  message a touch on the grid sends, so launch mode, choke groups and voices behave as from a touch.
  - Tools → More → **Gen**: Run, Fill, Epoch (Off / 4 / 8 / 16 / 32 bars); **Gen 2**: Seed, Mutate %.
  - Per lane (**L1..L4 Gen / Shape / Cond**): Algo, Pad (1–16, top left = 1), Steps (1–32), Hits, Rotate, Rate
    (1/32, 1/16, 1/8, 1/4, 1/16T, 1/8T), Cond, Prob %, Vel.
  - Algorithms: **Euclid**; **Breakcut** (BBCut-style cuts of 1–4 steps, a new cut each cycle, sometimes a closing
    roll); **Call+Resp** (3 cycles call + 1 response); **Evolve** (mutates each cycle by Mutate %, kept near Hits);
    **Drummap** (own drum maps per lane: kick, snare, hat, percussion; Rotate morphs two variations, Hits = density,
    velocity from the map); **Random**. All repeat with the same Seed and song position.
  - **Step conditions** (per lane, on every hit): 1:2, 2:2, 1:3 … 4:4 (A-th of every B cycles), Fill / !Fill,
    First / !First, then Prob %.
  - **Epoch** (EPOCH-lite): over N bars the density goes ½ → ¾ → full, the last bar is a fill (Fill conditions on).
- Settings store capacity 64 → 128 and parameter ids up to 0x2fc (the stock table ends at 0x1d0) for GEN's 41
  settings. GEN's settings are global (saved with the settings, not per preset).

### 2.3.4-sJ (2026-11-03)

- **Double tap a dial → its default value.** Two taps on the same dial within 350 ms set the parameter's default
  (the default of the record type the edited pad / FX cell / setting belongs to); only that value changes, its
  modulation does not. A drag between the taps or slower taps do nothing. Own code (the idea is also in vwal's
  patcher, whose code is not published under a licence and was not used).
- **Vertical waveform zoom**: on the sample waveform, spread two fingers vertically to make the waveform taller (up
  to x32, clipped at the edges), pinch to bring it back. Display only; the horizontal pinch zoom works as before.
- **CV In scale / offset** (Tools → More → Page "CV In"): every CV input's modulation value becomes
  value x Scale + Offset (Scale 0..400 %, Offset −100..+100 % of the range). Example: a 0–5 V source over the full
  bipolar range = 200 % / −100 %. The pitch (V/oct) of pads played from CV is not scaled.
- **Quantize** (Tools → More → Page "Quantize"): the pitch of pads played from a CV input snaps to a scale: Major,
  Minor, Dorian, Phrygian, Lydian, Mixolydian, Harmonic Minor, Pentatonic Major / Minor, Blues, Whole tone; Root C..B
  (nearest note, ties down).
- Simulator: the touch model has a second finger (`touch2` / `release2`).

### 2.3.4-sI (2026-11-03)

- **Modulation sources KEY and ENV** at the end of the pads' MOD Source list. **KEY**: keyboard tracking (the MIDI note
  relative to C3; the engine already had this per-voice source, without a list entry). **ENV**: the pad's envelope as a
  source (Attack / Decay / Sustain / Release of the pad, gate = any of its voices playing, retriggered by new voices;
  per pad). ENV reaches every target the pad-level sources reach: Level, Gain, Pan, Filter, Res, LFO rate / depth,
  Attack / Decay / Release, and the other stock targets (checked through the real route code in Unicorn).
- Screen saver: also woken by touches on dial pages.

### 2.3.4-sH (2026-11-03)

- **Screen saver with a live monitor**: after 1 / 2 / 5 (default) / 10 / 30 minutes without input (or Off) the screen
  shows a sweeping oscilloscope of Out1/2 with level meters (View: Scope / Meters / Dark). A touch wakes it (that touch
  does nothing else), as do the keys and any MIDI note / message (MIDI clock alone keeps it asleep). The UI's own
  screen is not touched: the saver only points the display at a buffer of its own and back.
- **Tools -> More** (was Cmp2): a Page list (Comp 2: Attack / Release / Gain; Dist 2: Dist Lvl; Saver: Saver / View).
  Turning Page shows the tab again with that page's settings.

### 2.3.4-sG (2026-11-03)

- **Slice Str** (Slicer Pos list, after Sync Grid; with Sync On): how a slice fills its synced duration.
  **Cut** = stock (the slice plays at its pitch and is cut, or leaves a gap). **Stretch** = time stretch (WSOLA
  grains, as Clip's Str Mode Smooth): the pitch stays, the slice lasts exactly the synced duration. **Tape** =
  varispeed: the slice is resampled to the duration (the pitch follows the tempo). With Sync Grid this is the Clip
  mode's tempo sync for slices. Renode (1 kHz sine slices): Cut 0.13 s, Stretch and Tape fill the 0.25 s duration,
  Stretch at the stock pitch, Tape resampled; Unicorn: speed 0.5 -> Stretch keeps 1 kHz, Tape 500 Hz.

### 2.3.4-sF (2026-11-03)

- **Slicer Sync Grid** (forum wish "Slicer mode with the sync features of Clip mode"): a new row after Sync in the
  Slicer's Pos list: Free (stock) or 1 bar, 1/2, 1/4, 1/8, 1/16, 1/32. With Sync On, each slice's duration (stock: the
  slice length scaled by the tempo ratio) is rounded to a whole number of grid steps at the current tempo, at least
  one, so slices played back to back (Slice Seq) stay on the grid instead of drifting.

### 2.3.4-sE (2026-11-03)

- **FX3**: a third send effect, a reverb with every reverb type and both pages (portkit's FX3, MK2 placement). On the
  pad grid touch FX2 once to select it, once more and the cell becomes **FX 3** (again: back to FX2); INFO opens its
  page. **FX3 Send** is the third send row in the Pos list. FX3 goes where the reverb goes (FX Out). It needs 4.5 MB of
  free heap at boot (plate and pre-delay memory), otherwise it stays silent.

### 2.3.4-sD (2026-11-03)

- **FX Out** (Tools -> Rec, its free fourth slot): where the FX returns go: Out1/2 (stock), Dly FX1/2, Rvb FX1/2,
  FX1/2 (both returns on the second jack pair), Dly1 Rvb2 (delay on the FX1 jack, reverb on the FX2 jack). Pads stay
  on their own Output. The returns no longer pass the master compressor / distortion when they leave Out1/2.
- **Delay types and modulation** (FX1 cell): Type Digital (stock) / Tape / BBD, and a second page (Type "Tape >"):
  Mod Rate (0.05-10 Hz), Mod Depth (up to 10 ms), Flutter (6.7 Hz), Drive, Tone, Level (+-12 dB), Mod Phase (L/R).
  The modulation moves the delay line's read target, so every repeat is pitched again through the feedback (wow /
  chorus); Drive and Tone colour what goes onto the line, so repeats get darker and softer (Tape: stronger saturation
  and head loss plus slow drift; BBD: darker, softer clip). Digital with Depth / Flutter 0, Drive 0, Tone 100 % is
  the stock delay. Delay Level is the "FX level" wish for FX1 (the reverb had Level already).
- **Reverb second page**: Type list Plate, Room, Room >, Hall, Hall >, Chamber, Chmbr >. Page 1: Type, Decay,
  Predelay, Damping, Size, Diffusion, Early, Level; page 2: Bass Mult, Crossover, Mod Rate, Mod Depth, Width, Hi Cut,
  Lo Cut (sA showed only the first eight of fifteen). Changing Type shows the page again at once.
- **Compressor bus**: "Compressor:" (Tools -> Main) is a list Off / Out1/2 / FX1/2 / Both. FX1/2 runs the compressor
  on the second jack pair instead; Both adds a second compressor (our SSL-style detector with the Stock or SSL
  settings) on FX1/2. Presets: On = Out1/2.
- **FX1 / FX2 Send** as numeric % rows at the end of the Pos list (Sample / Clip / Slicer / Granular; Multisample: Conf).

### 2.3.4-sC (2026-11-02)

- **Master section** from portkit (src/master.c): the stock master compressor's settings with Comp Type Stock / SSL
  and Mix, and a master distortion (Soft / Hard / Fold / Tube, 2x oversampled). MK2 placement: three new Tools tabs
  **Comp** (Type, Threshold, Ratio, Mix), **Cmp2** (Attack, Release, Gain, Dist Lvl), **Dist** (Type, Drive, Tone,
  Mix); "MIDIIn" shortened to "MIDI" so six tabs fit. SSL swaps the ratio / attack / release knobs for its switches
  (shown on the next tab change). Data-only tabs (the Tools tab builder takes labels and ids from tables).

### 2.3.4-sB (2026-11-02)

- **Fix: Slicer / Clip "Poly Mode: Mono" not honoured.** The engine took the mono flag from the Sample pads' Poly Mode
  (0x82) and ignored the Clip / Slicer one (0x83); a preset whose saved 0x82 was a Poly value made a "Mono" slicer
  play overlapping slices (heard with PlayThru on). Now 0x83 decides, 0x82 is ignored on Clip / Slicer pads
  (patches/fixes.py). Reproduced and checked in the Renode simulation and in tests/test_fixes.py.

### 2.3.4-sA (2026-11-02): first MK2 build (emulation only, not on a unit yet)

Ported from portkit pkZD (micro): Fidelity (+ on Clip / Slicer / Granular Conf lists), 24 dB SVF filter, Drive,
Filter 2 / Res 2 (MK2: LFO page), clip Shift / Play Len / Stretch (Smooth = WSOLA) / Str Mode / Loop Fade / Sync 1/32
(MK2: clip Pos list), reverb types + Level, CPU load (MK2: after the free-memory text in the status bar), parameter
cache. MK2-specific: page placement for the 5 x 36 page tables, status-bar load next to "%dMB", RAM layout in the MK2
gap 0x2406ad98..0x24070dc0 (Renode canary), Renode platform for the MK2 (sim/mk2.repl), address translator and map.
Not yet: FX3, master compressor / distortion, recording sends.

---

## portkit (bitbox micro), the upstream history

portkit for the 1010music bitbox micro, on top of the stock firmware **2.3.4**. Versions are named after the stock
version plus the portkit build (`2.3.4-pkZD`); the splash screen shows it.

Status: **hardware** = tested on a unit, **emulation** = verified in the test suite and the simulator only.

## 2.3.4-pkZD (2026-10-20): first public release

Patch your own stock `MICRO.BIN` 2.3.4 in the browser: <https://kyrillleier-jpg.github.io/portkit-micro/>.
Manual with screenshots: [docs/manual](docs/manual/README.md) (also as PDF).

### Sound

- **Fidelity**: the pad's Interp list becomes Fidelity, with the machine modes SP1200, SP-12, S950, MPC60 and SP Raw
  and fixed sample-rate reductions 16k, 12k, 10k, 8k, 6k, 4k, 2k (12 bit, no anti-aliasing). On Sample pads
  (hardware) and on Clip, Slicer and Granular pads (emulation).
- **24 dB filter**: the pad filter is a 24 dB/octave state-variable filter instead of the stock 12 dB biquad; same
  Filter / Res knobs, clean resonance (emulation).
- **Drive** (tanh saturation before the filters) and **Filter 2 / Res 2** (a second 24 dB filter in series) on the
  main page of every pad mode (emulation).

### Clips

- **Shift**: moves the audio against the loop by −100..+100 ms (hardware).
- **Play Len**: Full or 1/4..32 bars (emulation).
- **Stretch**: tempo ±100 % (hardware), with **Str Mode** Auto / Jump / Smooth; Smooth time-stretches without
  changing the pitch (hardware).
- **Loop Fade**: 0..50 ms fade at the loop seams against clicks (emulation).
- **Sync 1/32**; Play Len and Stretch are hidden while Sync / Quant Size make them ineffective (emulation).

### Effects

- **Reverb types** Plate / Room / Hall / Chamber with ten settings and a Level control (hardware), level-matched to
  the plate (emulation).
- **FX3**: a third send with a second, independent reverb. Tap the selected RVB cell on the Effects screen to switch
  to FX3; the pads then show their FX3 send; right arrow = FX3 settings, again = master EQ (hardware; the current
  layout with FX3 on its own row: emulation).

### Master section

- **Compressor page** (Tools → left arrow): threshold, ratio, attack, release, gain for the stock compressor, plus
  **Comp Type SSL** (ratio 2:1 / 4:1 / 10:1, attack 0.1–30 ms, release 0.1–1.2 s or Auto) and **Mix** for parallel
  compression (emulation).
- **Master distortion** (Tools → right arrow twice): Soft / Hard / Fold / Tube with 2x oversampling, Drive, Tone,
  Mix, Level (emulation).

### Workflow

- **Pads 9–16** as a second page of the pad grid: left arrow on Pads and Effects, top-left field on the Mixer, left
  arrow in the pad picker (hardware; encoder on page 2: emulation).
- **Recording**: a pad's FX1 / FX2 / FX3 sends stay when it is recorded into; the input monitor goes through the
  selected pad's sends (emulation).
- **CPU load** in the status bar (emulation).

### Fixed in this release (since the last test builds)

- **pkZB:** crackling when turning the master distortion or the record gain, recordings that came out as
  crackle only, and monitor sends without effect (pkZ / pkZA). Cause: part of portkit's working memory overlapped
  buffers the firmware uses for the SD card and recording. Moved, and a test now checks every memory address
  portkit uses (emulation).
- **pkZC:** clicks and artefacts while turning Stretch on a playing clip; Auto no longer switches hard between
  stock playback and Smooth when Stretch passes 0 % (emulation).
- **pkZ:** about half the CPU cost of the distortion and 8–14 % less for the reverb types (emulation, measured in
  instructions per audio block).

### Known issues

- Loop Fade: a synced clip played at a different tempo without Smooth stretch can reach its loop jump before the
  fade-out zone; the fade at the loop end is then skipped.
- In the simulator the title "Pads 1-8" overlaps the SD-card space readout in the status bar (not checked on a unit).
- Features marked *emulation* above have not been confirmed on a unit yet. Feedback welcome.

## Test builds (not released)

| Build | Date | Change |
|---|---|---|
| pkZA | 2026-10-14 | sends kept across recording, input monitor through the sends |
| pkZ | 2026-10-12 | lower CPU cost of the DSP |
| pkY | 2026-10-11 | compressor type SSL, compressor Mix |
| pkW, pkX | 2026-10-10 | FX3 on its own FX row; master compressor and distortion pages |
| pkU, pkV | 2026-10-10 | FX3 (third send, second reverb); reverb types level-matched |
| pkT | 2026-10-10 | reverb types Plate / Room / Hall / Chamber |
| pkN – pkS | 2026-10-10 | Stretch fixes, Smooth time stretch (WSOLA), CPU load readout |
| pkJ – pkM | 2026-10-10 | Stretch fixes, Sync 1/32, hide ineffective clip controls |
| pkH, pkI | 2026-10-10 | clip Play Len; clip Stretch |
| pkF, pkG | 2026-10-10 | Drive, Filter 2, clip Shift |
| pk3 – pkE | 2026-10-09/10 | pads 9–16 (paging, pad picker, encoder on page 2) |
| pk2 | 2026-10-09 | Fidelity on Clip, Slicer and Granular pads |
| pk1 | 2026-10-09 | Fidelity and the 24 dB filter |
