"""SD card image for the simulation from a directory tree, on Linux (FAT32 in an MBR partition; sfdisk + mtools).
sim/mksd.py does the same on macOS with hdiutil.

  python3 -I sim/mksd_linux.py <tree dir> <image> [--size-mb 256]
"""
import os
import subprocess
import sys

START = 2048          # first partition sector, as most card formatters


def main(a):
    size = 256
    if "--size-mb" in a:
        size = int(a[a.index("--size-mb") + 1])
        a = a[:a.index("--size-mb")]
    tree, img = os.path.abspath(a[0]), os.path.abspath(a[1])
    with open(img, "wb") as f:
        f.truncate(size << 20)
    subprocess.run(["sfdisk", "-q", img], input=f"{START},,c\n", text=True, check=True)
    part = f"{img}@@{START * 512}"
    subprocess.run(["mkfs.vfat", "-F", "32", "-n", "BITBOX", "--offset", str(START), img,
                    str(((size << 20) - START * 512) // 1024)], check=True, stdout=subprocess.DEVNULL)
    if os.listdir(tree):
        subprocess.run(["mcopy", "-s", "-i", part] + [os.path.join(tree, n) for n in sorted(os.listdir(tree))] + ["::"],
                       check=True)
    print(f"wrote {img} ({size} MB)")


if __name__ == "__main__":
    main(sys.argv[1:])
