#!/bin/sh
# Sets up the project: reference repos, Python venv, firmware hash check, compiler check.
set -e
cd "$(dirname "$0")"

mkdir -p repos firmware re out
# reference only (feature sources, address notes); nothing from them is built in
[ -d repos/blackbox-mod ] || git clone --depth 1 https://github.com/j3threejay/blackbox-mod.git repos/blackbox-mod
[ -d repos/bitbox-micro-fw-patch ] || git clone --depth 1 https://github.com/modularpatcher/bitbox-micro-fw-patch.git repos/bitbox-micro-fw-patch
[ -d .venv ] || python3 -m venv .venv
.venv/bin/pip -q install capstone unicorn pillow numpy

check() {
  f="$1"; want="$2"
  if [ ! -f "$f" ]; then echo "MISSING: $f"; return; fi
  got=$(shasum -a 256 "$f" 2>/dev/null | cut -d' ' -f1 || sha256sum "$f" | cut -d' ' -f1)
  if [ "$got" = "$want" ]; then echo "OK:      $f"; else echo "WRONG:   $f ($got)"; fi
}
check firmware/BITBOXMK2.bin 35ab7995ad0eca8484f3b28bc76389f10b2037dbb21a4a394e950d9329073e86
# micro 2.3.4, for porting from portkit (tools/micro2mk2.py, tools/port_mk2.py):
check firmware/MICRO.BIN    54ecc3e5c09b9aed7ad8e2d9d1c9f8ee63e06a8351c857304c3ff21fcb9c4365
# optional, only for cross-image matching (tools/fmatch.py, tools/falign.py):
[ -f firmware/BLACKBOX.bin ] && check firmware/BLACKBOX.bin 281ae303d32e5eb52adca7817a648a34e4bd3848017f26673dd9df3905f1341d || true

echo
echo "Compiler:"
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
  arm-none-eabi-gcc --version | head -1
else
  echo "  arm-none-eabi-gcc missing. macOS: brew install --cask gcc-arm-embedded"
  echo "                             Debian/Ubuntu: sudo apt install gcc-arm-none-eabi"
fi
