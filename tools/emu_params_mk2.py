"""Run the MK2 2.3.4 parameter-table init (0x0806a300; micro 0x08069f00) in Unicorn and print every registration:
knob (0x0806a2d4), list (0x0806a27c) and the two other register helpers (0x0806a24c, 0x0806a2a8).
Used to check that this project's parameter ids are free on the MK2.

  python3 -I tools/emu_params_mk2.py firmware/BITBOXMK2.bin [--ids]      (--ids: only the sorted id list)
"""
import struct
import sys

from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_THUMB, Uc
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0,
                               UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_CPU_ARM_MAX)

B = 0x08040000
INIT, ALLOC = 0x0806A300, 0x08043B4C
REG = {0x0806A2D4: "knob", 0x0806A27C: "list", 0x0806A24C: "reg_e4c", 0x0806A2A8: "reg_ea8"}


def run(path):
    d = open(path, "rb").read()
    mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    mu.ctl_set_cpu_model(UC_CPU_ARM_MAX)
    mu.mem_map(B, (len(d) + 0xFFFF) & ~0xFFFF)
    mu.mem_write(B, d)
    mu.mem_map(0x24000000, 0x80000)
    mu.reg_write(UC_ARM_REG_C1_C0_2, 0xF << 20)
    mu.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    ret_at, heap, out = 0x24000000, [0x24040000], []

    def s(a):
        if not (B <= a < B + len(d)):
            return f"<{a:#x}>"
        return d[a - B:a - B + 40].split(b"\0")[0].decode("latin1")

    def ret(uc):
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def alloc(uc, *_):
        n = uc.reg_read(UC_ARM_REG_R0)
        p = heap[0]
        heap[0] += (n + 7) & ~7
        uc.mem_write(p, bytes(n))
        uc.reg_write(UC_ARM_REG_R0, p)
        ret(uc)

    def reg(kind):
        def h(uc, *_):
            r1, r2, r3 = (uc.reg_read(r) for r in (UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3))
            label = r3 if kind == "knob" else r2
            out.append((r1, kind, s(label)))
            ret(uc)
        return h

    mu.hook_add(UC_HOOK_CODE, lambda uc, *_: uc.emu_stop(), begin=ret_at, end=ret_at)
    mu.hook_add(UC_HOOK_CODE, alloc, begin=ALLOC, end=ALLOC)
    for a, k in REG.items():
        mu.hook_add(UC_HOOK_CODE, reg(k), begin=a, end=a)
    mu.reg_write(UC_ARM_REG_R0, 0x24030000)
    mu.reg_write(UC_ARM_REG_SP, 0x2407F000)
    mu.reg_write(UC_ARM_REG_LR, ret_at | 1)
    mu.emu_start(INIT | 1, ret_at, count=5_000_000)
    return out


if __name__ == "__main__":
    regs = run(sys.argv[1])
    if "--ids" in sys.argv:
        print(" ".join(f"{i:#x}" for i in sorted({r[0] for r in regs})))
    else:
        for pid, kind, label in regs:
            print(f"{pid:#05x} {kind:7} {label!r}")
    print(f"{len(regs)} registrations, max id {max(r[0] for r in regs):#x}", file=sys.stderr)
