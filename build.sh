#!/bin/sh
# Compile src/ into the code cave: out/cave.elf + out/cave.bin (bitbox MK2 2.3.4).
# Ported from portkit's micro build. Not built yet on the MK2: fx3.c, recmon.c (their cells need an MK2
# placement) and padpages.c (the MK2 has 16 pads on one page).
set -e
cd "$(dirname "$0")"
mkdir -p out
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16 -Os -ffreestanding -nostdlib \
  -fno-builtin -Wall -Wextra -Werror -T src/cave.ld -Wl,-e,0 -o out/cave.elf src/filter.c src/filter_thunk.S \
  src/clipshift.c src/clipshift_thunk.S src/stretch.c src/load.c src/reverb.c src/reverb_thunk.S src/fastget.c \
  src/master.c src/master_thunk.S src/fx.c src/fx_thunk.S src/fx3.c src/fx3_thunk.S src/slicer.c src/slicer_thunk.S
arm-none-eabi-objcopy -O binary -j .cave out/cave.elf out/cave.bin
arm-none-eabi-size -A out/cave.elf | grep -E 'cave|ram'
