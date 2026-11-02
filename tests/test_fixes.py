"""Unicorn tests for patches/fixes.py on the patched MK2 image: the real clip / slicer parameter handler 0x080b7e10 runs
with a pad object and a parameter message (id at msg +0xc, value at +0x10).

  python3 -I tests/test_fixes.py [image]       (default out/fidelity+pages+sound+reverb+fixes+cave/BITBOXMK2.bin)
"""
import os
import struct
import sys

from unicorn import UC_ARCH_ARM, UC_MODE_THUMB, Uc
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1,
                               UC_ARM_REG_R2, UC_ARM_REG_SP, UC_CPU_ARM_MAX)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = 0x08040000
IMG = open(sys.argv[1] if len(sys.argv) > 1 else
           os.path.join(ROOT, "out", "fidelity+pages+sound+reverb+fixes+cave", "BITBOXMK2.bin"), "rb").read()
STOCK = open(os.path.join(ROOT, "firmware", "BITBOXMK2.bin"), "rb").read()
HANDLER, RET = 0x080B7E10, 0x24000000
PAD, MSG = 0x24010000, 0x24020000
MONO = 0x1F7
failures = 0


def check(what, ok, detail=""):
    global failures
    print(("ok   " if ok else "FAIL ") + what + ("" if ok else f"   {detail}"))
    failures += 0 if ok else 1


def handle(img, pid, value, mono_before):
    mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    mu.ctl_set_cpu_model(UC_CPU_ARM_MAX)
    mu.mem_map(BASE, (len(img) + 0xFFFF) & ~0xFFFF)
    mu.mem_write(BASE, img)
    mu.mem_map(0x24000000, 0x80000)
    mu.reg_write(UC_ARM_REG_C1_C0_2, 0xF << 20)
    mu.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    mu.mem_write(PAD + MONO, bytes([mono_before]))
    mu.mem_write(MSG + 0xC, struct.pack("<II", pid, value))
    mu.reg_write(UC_ARM_REG_R0, PAD)
    mu.reg_write(UC_ARM_REG_R1, 0)
    mu.reg_write(UC_ARM_REG_R2, MSG)
    mu.reg_write(UC_ARM_REG_SP, 0x2407F000)
    mu.reg_write(UC_ARM_REG_LR, RET | 1)
    mu.emu_start(HANDLER | 1, RET, count=10000)
    return mu.mem_read(PAD + MONO, 1)[0]


# stock: 0x83 is ignored, 0x82 sets the flag (the bug's mechanism)
check("stock: Clip/Slicer Poly Mode (0x83) Mono leaves the engine poly", handle(STOCK, 0x83, 0, 0) == 0)
check("stock: Sample Poly Mode (0x82) Poly X makes it poly", handle(STOCK, 0x82, 5, 1) == 0)
# fixed
check("fix: 0x83 Mono -> mono flag set", handle(IMG, 0x83, 0, 0) == 1)
check("fix: 0x83 Poly 2 -> mono flag cleared", handle(IMG, 0x83, 1, 1) == 0)
check("fix: a stale 0x82 (Poly X) no longer overrides a Mono slicer", handle(IMG, 0x82, 5, 1) == 1)
check("fix: a stale 0x82 (Mono) no longer overrides a Poly 2 slicer", handle(IMG, 0x82, 0, 0) == 0)
check("fix: the dispatcher sends Clip/Slicer Poly Mode as 0x83 (movs r4, #0x83 at 0x0807fce0)",
      IMG[0x0807FCE0 - BASE:0x0807FCE2 - BASE] == bytes.fromhex("8324"))

print(f"\n{'FAILED: ' + str(failures) if failures else 'all passed'}")
sys.exit(1 if failures else 0)
