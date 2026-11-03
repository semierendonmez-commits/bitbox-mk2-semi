"""Hardware probe: only the Menu / splash version text (no code). SEMI_TAG names the build."""
import os

TAG = os.environ.get("SEMI_TAG", "T1").encode()


def slot(text, size=16):
    return text + b"\0" * (size - len(text))


PATCHES = [
    (0x080C68EC, slot(b"by 1010music"), slot(b"semi probe")),
    (0x080C68FC, slot(b"2.3.4"), slot(b"2.3.4-s" + TAG)),
]
