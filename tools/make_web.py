"""Build, test and publish the browser patcher (bitbox MK2): docs/index.html (GitHub Pages) + docs/patches/<slug>.json.

  python3 -I tools/make_web.py [--skip-tests] [--allow-dirty]

1. build.sh + patch.py with every patch set, then the Unicorn tests.
2. tools/webpatch.py: patch = only our bytes (no stock byte; checked), with the result's SHA-256.
3. The page (tools/patcher.html) with the patch inlined: works from GitHub Pages and as a downloaded file.
Same patch format as blackbox-mod's docs/*.json.
"""
import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "patches"))
import webpatch  # noqa: E402
from cave import VERSION  # noqa: E402

PY = sys.executable
SETS = ["fidelity", "pages", "sound", "reverb", "fixes", "master", "fx", "fx3", "ss", "modsrc", "ui", "gen", "cave"]
TESTS = ["test_fidelity", "test_fx", "test_gen", "test_sendmod", "test_ram", "test_master", "test_reverb", "test_fixes"]
STOCK = os.path.join(ROOT, "firmware", "BITBOXMK2.bin")
IMAGE = "BITBOXMK2.bin"
REPO = "https://github.com/semierendonmez-commits/bitbox-mk2-semi"
PORTKIT = "https://github.com/kyrillleier-jpg/portkit-micro"

FEATURES = [
    ["Fidelity", "Interp list Normal, HighQ, SP1200, SP-12, S950, MPC60, SP Raw, 16k..2k on Sample, Clip, Slicer, Granular (Conf)"],
    ["24 dB filter, Drive, Filter 2", "State-variable filter instead of the pad biquad; tanh drive and a second 24 dB filter (LFO page)"],
    ["Clip tools", "Shift, Play Len, Stretch % with Str Mode Auto / Jump / Smooth (pitch kept), Loop Fade, Sync 1/32"],
    ["Slicer sync", "Sync Grid (1 bar .. 1/32) and Slice Str Cut / Stretch / Tape: slices fill their synced duration"],
    ["Delay and reverb", "Delay types Digital / Tape (wow, flutter, drive) / BBD (dark, delay-time clock), reverb types Plate / Room / Hall / Chamber, each with a second page"],
    ["FX3 and FX Out", "Third send (touch the selected FX2 cell again); FX1 / FX2 / FX3 Send modulatable (MOD page); FX returns to Out1/2 or the FX1/2 jacks"],
    ["Master section", "Compressor Stock / SSL with Mix and bus choice, master distortion Soft / Hard / Fold / Tube"],
    ["Modulation", "MOD sources KEY (keyboard tracking) and ENV (the pad's ADSR); CV In scale / offset; CV pitch quantizer"],
    ["GEN sequencer", "4 lanes on the MK2 clock: Euclid, Breakcut (rearranges sample / slices), Call+Resp (melodies), Evolve, Drummap, Random; step conditions; Epoch"],
    ["UI", "Double tap a dial for its default; vertical waveform zoom (two fingers); screen saver with a live scope"],
    ["Fixes and status", "Clip / Slicer Poly Mode Mono honoured; CPU load in the status bar"],
]

CONFIG = {
    "lead": "Fidelity modes, 24 dB filters, clip and slicer sync tools, new delay / reverb types, a third FX send, a master "
            "section, new modulation sources, a generative sequencer and more for the 1010music bitbox MK2. Patch your own "
            "stock firmware right here; nothing is uploaded.",
    "stockName": IMAGE,
    "stockHint": "The unmodified bitbox MK2 2.3.4 from 1010music (the <code>BITBOXMK2.bin</code> inside the download zip).",
    "stockSource": "Download the bitbox MK2 2.3.4 firmware from 1010music and use the <code>BITBOXMK2.bin</code> inside the zip.",
    "outName": IMAGE,
    "install": [
        "Back up the whole microSD card, and keep your stock <code>BITBOXMK2.bin</code>.",
        "Copy the patched file to the root of the card (not in a folder); the name must be exactly <code>BITBOXMK2.bin</code>.",
        "With the unit off, insert the card, hold <b>INFO</b> while powering on and let go when the update message appears.",
        "Leave the power on until it restarts by itself (about 30 seconds).",
        f"Press <b>HOME</b> until the Menu: the lower right shows <b>{VERSION.decode()}</b> (the splash says <b>semi mod</b>).",
        "To go back: the same steps with the stock <code>BITBOXMK2.bin</code> (the Menu then shows 2.3.4).",
        "Unofficial firmware: boots on a unit since sL, the features are being checked on hardware. Use at your own risk.",
    ],
    "footer": f'<a href="{REPO}">bitbox-mk2-semi</a> · MIT · ported from <a href="{PORTKIT}">portkit-micro</a> (MIT), '
              f'with findings and code from blackbox-mod by Justin Joe (MIT, <a href="{REPO}/blob/main/THIRD_PARTY.md">'
              f'notices</a>) · not affiliated with 1010music',
}


def run(title, cmd):
    print(f"## {title}", flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-tests", action="store_true")
    ap.add_argument("--allow-dirty", action="store_true")
    a = ap.parse_args(argv)
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    if dirty and not a.allow_dirty:
        sys.exit("uncommitted changes: commit first (the page names the commit it was built from), or --allow-dirty")
    commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()

    run("Compile", ["sh", "build.sh"])
    run("Patch (all sets)", [PY, "-I", "patch.py"] + SETS)
    if not a.skip_tests:
        for t in TESTS:
            run(t, [PY, "-I", f"tests/{t}.py"])

    version = VERSION.decode()
    stock = open(STOCK, "rb").read()
    built = open(os.path.join(ROOT, "out", "+".join(SETS), IMAGE), "rb").read()
    from thumb import STOCK_END, symbols
    sym = symbols(os.path.join(ROOT, "out", "cave.elf"))
    allow = [(sym["mod_codes"] - STOCK_END - 4, sym["mod_codes"] - STOCK_END + 2 * 21,
              "modsrc.c mod_codes: the stock MOD source code list (written out in our source) + KEY, ENV; with the zero padding before it")]
    patch = webpatch.make("bitbox-mk2-semi", version, "2.3.4", stock, built, features=FEATURES, allow=allow)
    patch.update(title=f"{version}: everything", source_commit=commit + ("-dirty" if dirty else ""),
                 summary="All patch sets; the combination the tests cover.")
    assert webpatch.apply(patch, stock)["image"] == built

    os.makedirs(os.path.join(ROOT, "docs", "patches"), exist_ok=True)
    jpath = os.path.join(ROOT, "docs", "patches", f"{patch['slug']}.json")
    json.dump(patch, open(jpath, "w"), indent=1)
    page = open(os.path.join(ROOT, "tools", "patcher.html")).read()
    for k, v in (("__TITLE__", f"bitbox-mk2-semi {version} patcher · bitbox MK2"),
                 ("__H1__", f"bitbox-mk2-semi <span>{version}</span> for bitbox MK2"),
                 ("__CONFIG__", json.dumps(CONFIG)),
                 ("__PATCHES__", json.dumps([patch], separators=(",", ":")))):
        assert page.count(k) == 1, k
        page = page.replace(k, v)
    open(os.path.join(ROOT, "docs", "index.html"), "w").write(page)
    print(f"docs/index.html ({len(page):,} bytes), {os.path.relpath(jpath, ROOT)}: {len(patch['writes'])} writes, "
          f"{len(patch['append']) // 2} appended; output sha256 {patch['output_sha256'][:16]}, from {patch['source_commit']}")
    print("open docs/index.html in a browser to try it; commit docs/ to publish")


if __name__ == "__main__":
    main(sys.argv[1:])
