# Third-party code

## blackbox-mod (Justin Joe)

`src/filter.c` and the BL encoder in `patches/thumb.py` are ported from
[j3threejay/blackbox-mod](https://github.com/j3threejay/blackbox-mod) (Blackbox 3.1.9), MIT licence:

```
MIT License

Copyright (c) 2026 Justin Joe

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## stmlib (Emilie Gillet)

The state-variable filter in `src/filter.c` follows `stmlib/dsp/filter.h` (class `Svf`), Copyright 2014 Emilie Gillet,
MIT licence.

## polyend-tracker-re-semi (OpenTracker fork, Apache-2.0)

GEN's Breakcut procedures, Acid and Turing lanes in `src/gen.c` are ported (rewritten in C for GEN's step grid) from
our own generators in [semierendonmez-commits/polyend-tracker-re-semi](https://github.com/semierendonmez-commits/polyend-tracker-re-semi)
(`src/core/gen/gen_bbcut.cpp`, `gen_acid.cpp`, `gen_turing.cpp`), Apache License 2.0. Those follow Nick Collins'
BBCut as ported by bencodec/BBenCut (the procedures and the SQPusher1 fill table), Logarhythm's TB-3PO (O_C
Hemisphere) and the Music Thing Turing Machine (behaviour, not its circuit). The theme names follow that fork's theme
pack; the colours here are new.

## Codecs

`src/codec.c` implements G.711 mu-law and IMA / DVI ADPCM from the public standards (own code); the idea of codec
delays and lo-fi modes comes from our TRANSCODE / FRAMELOSS plug-ins, which use Frieve-A's EffeTune (MIT).

## 1010music firmware

Not included and not licensed here. The patcher needs your own copy of the stock `MICRO.BIN` 2.3.4 in `firmware/`.
