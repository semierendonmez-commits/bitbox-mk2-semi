"""Test SD card for the MK2 simulation: one preset with Sample, Clip, Slicer and Granular pads (bottom row) and four
more Sample pads, from sim/testsignals.py. Preset format as on the micro (Presets/<Name>/preset.xml, checked there).

  python3 -I sim/mk2_card.py [out/sim/mk2_sd.img]
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIM = os.path.join(ROOT, "sim")
TREE = os.path.join(ROOT, "out", "sim", "mk2_tree")
PY = sys.executable

# (row, column, name, cell mode, extra params); row 0 = bottom row on the micro (MK2: to check)
PADS = [
    (0, 0, "Sample", 0, ""),
    (0, 1, "Clip", 1, 'synctype="5" quantsize="3" beatcount="8"'),
    (1, 1, "SP1200", 0, 'interpqual="2" pkdrive="350"'),
    (1, 2, "Stretch", 1, 'synctype="5" quantsize="3" beatcount="8" pkshift="30" pkstretch="25" pkstretchmode="2" '
                         'pkloopfade="10"'),
    (0, 2, "Slicer", 2, ""),
    (0, 3, "Granular", 3, ""),
    (1, 0, "Keys", 0, ""),
    (2, 0, "Row2", 0, ""),
    (3, 0, "Row3", 0, ""),
    (3, 3, "Corner", 0, ""),
]


def main(a):
    """a: [image path] [--fx]  (--fx: Sample pad FX1 Send 100 %, Keys FX2 Send 100 %, for the FX return tests)"""
    fx = "--fx" in a
    out2 = "--out2" in a                       # Sample pad Output = FX1/2 (bus 3: the second jack pair)
    a = [x for x in a if x not in ("--fx", "--out2")]
    sd = os.path.abspath(a[0]) if a else os.path.join(ROOT, "out", "sim", "mk2_sd.img")
    pre = os.path.join(TREE, "Presets", "Test")
    shutil.rmtree(TREE, ignore_errors=True)
    os.makedirs(pre)
    subprocess.run([PY, "-I", os.path.join(SIM, "testsignals.py"), pre], check=True, stdout=subprocess.DEVNULL)
    src = {0: "drums.wav", 1: "drums.wav", 2: "chord.wav", 3: "chord.wav"}
    out = ['<?xml version="1.0" encoding="UTF-8"?>', "<document>", '    <session version="2">']
    for r, c, name, mode, extra in PADS:
        if out2 and name == "Sample":
            extra = (extra + ' outputbus="3"').strip()
        if fx and name in ("Sample", "Keys"):
            extra = (extra + (' fx1send="1000"' if name == "Sample" else ' fx2send="1000"')).strip()
        shutil.copyfile(os.path.join(pre, "chord.wav" if name == "Keys" else src[mode]), os.path.join(pre, name + ".wav"))
        out += [f'        <cell row="{r}" column="{c}" layer="0" filename=".\\{name}.wav" type="sample">',
                f'            <params cellmode="{mode}" {extra}/>'.replace(" />", "/>"),
                "            <slices/>", "        </cell>"]
    out += ["    </session>", "</document>", ""]
    for f in ("drums.wav", "chord.wav"):
        os.remove(os.path.join(pre, f))
    open(os.path.join(pre, "preset.xml"), "w").write("\n".join(out))
    if os.path.exists(sd):
        os.remove(sd)
    subprocess.run([PY, "-I", os.path.join(SIM, "mksd_linux.py"), TREE, sd], check=True)


if __name__ == "__main__":
    main(sys.argv[1:])
