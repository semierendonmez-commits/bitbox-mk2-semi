"""Test card for the Slicer Poly Mode bug (forum report, semi4136, 2.3.4): one Slicer pad on a 2-bar break, 16 slices,
Launch Trigger, Poly Mode Mono, PlayThru on, Quant Size None (or as given), MIDI In ch 1, Root Note 48 (slice k =
note 48 + k).

  python3 -I sim/slicer_card.py <image> [--quant <index 0..13>] [--playthru 0|1] [--poly 0|1] [--samplepoly 0..5]
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIM = os.path.join(ROOT, "sim")
TREE = os.path.join(ROOT, "out", "sim", "slicer_tree")
PY = sys.executable
FRAMES = 192000            # sim/testsignals.py drums(): 2 bars at 120 BPM, 48 kHz


def opt(a, name, default):
    return int(a[a.index(name) + 1]) if name in a else default


def main(a):
    sd = os.path.abspath(a[0])
    quant, playthru, poly = opt(a, "--quant", 13), opt(a, "--playthru", 1), opt(a, "--poly", 0)
    spoly = opt(a, "--samplepoly", -1)
    extra = a[a.index("--extra") + 1] if "--extra" in a else ""      # more pad params, e.g. 'slicersync="1000"'          # polymode (0x82, the Sample pads' Poly Mode) as saved, -1: absent
    pre = os.path.join(TREE, "Presets", "Slicer")
    shutil.rmtree(TREE, ignore_errors=True)
    os.makedirs(pre)
    subprocess.run([PY, "-I", os.path.join(SIM, "testsignals.py"), pre], check=True, stdout=subprocess.DEVNULL)
    tone = "--chord" in a                       # the 4 s chord instead of the break (pitch checks)
    os.replace(os.path.join(pre, "chord.wav" if tone else "drums.wav"), os.path.join(pre, "Break.wav"))
    os.remove(os.path.join(pre, "drums.wav" if tone else "chord.wav"))
    if "--sine" in a:                            # a 4 s 1 kHz sine (pitch checks by zero crossings)
        import math, struct, wave
        with wave.open(os.path.join(pre, "Break.wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(48000)
            w.writeframes(b"".join(struct.pack("<h", int(16000 * math.sin(2 * math.pi * 1000 * k / 48000)))
                                   for k in range(4 * 48000)))
    if "--steps" in a:                           # slice k = a tone of 500 * (k + 1) Hz (which slice played: by frequency)
        import math, struct, wave
        n = 4 * 48000
        with wave.open(os.path.join(pre, "Break.wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(48000)
            w.writeframes(b"".join(struct.pack("<h", int(16000 * math.sin(2 * math.pi * 500 * (k * 16 // n + 1) * k / 48000)))
                                   for k in range(n)))
    frames = 4 * 48000 if ("--chord" in a or "--sine" in a or "--steps" in a) else FRAMES
    slices = "".join(f'<slice pos="{k * frames // 16}"/>' for k in range(16))
    xml = ['<?xml version="1.0" encoding="UTF-8"?>', "<document>", '    <session version="2">',
           '        <cell row="0" column="0" layer="0" filename=".\\Break.wav" type="sample">',
           f'            <params cellmode="{opt(a, '--mode', 2)}" samtrigtype="0" polymodeslice="{poly}" playthru="{playthru}" '
           f'slicerquantsize="{quant}" midimode="1" rootnote="{opt(a, '--root', 48)}"'
           + (f' polymode="{spoly}"' if spoly >= 0 else "") + (" " + extra if extra else "") + '/>',
           f"            <slices>{slices}</slices>", "        </cell>", "    </session>", "</document>", ""]
    open(os.path.join(pre, "preset.xml"), "w").write("\n".join(xml))
    if os.path.exists(sd):
        os.remove(sd)
    subprocess.run([PY, "-I", os.path.join(SIM, "mksd_linux.py"), TREE, sd], check=True)


if __name__ == "__main__":
    main(sys.argv[1:])
