#!/usr/bin/env python3
"""Regenerate pet assets and compare their semantic pixel content.

PNG byte streams are intentionally not compared: Pillow and zlib versions may
encode identical RGBA pixels differently. The generated C source is compared
byte-for-byte because that is the file linked into the firmware.
"""

from __future__ import annotations

import hashlib
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PNG_ROOT = ROOT / "assets" / "pet"
C_FILE = ROOT / "src" / "assets" / "pet_animations.c"
GENERATOR = ROOT / "tools" / "generate_pet_assets.py"


def pixel_snapshot() -> dict[str, tuple[str, tuple[int, int], str]]:
    snapshot = {}
    for path in sorted(PNG_ROOT.glob("**/*.png")):
        with Image.open(path) as image:
            rgba = image.convert("RGBA")
            snapshot[path.relative_to(ROOT).as_posix()] = (
                image.mode,
                image.size,
                hashlib.sha256(rgba.tobytes()).hexdigest(),
            )
    return snapshot


def main() -> int:
    before_pixels = pixel_snapshot()
    before_c = C_FILE.read_bytes()

    subprocess.run([sys.executable, str(GENERATOR)], cwd=ROOT, check=True)

    after_pixels = pixel_snapshot()
    after_c = C_FILE.read_bytes()

    if before_pixels != after_pixels:
        before_names = set(before_pixels)
        after_names = set(after_pixels)
        print("Generated PNG pixel content changed.", file=sys.stderr)
        for name in sorted(before_names | after_names):
            if before_pixels.get(name) != after_pixels.get(name):
                print(
                    f"  {name}: {before_pixels.get(name)} -> "
                    f"{after_pixels.get(name)}",
                    file=sys.stderr,
                )
        return 1

    if before_c != after_c:
        print(
            "Generated src/assets/pet_animations.c is stale. "
            "Run: python tools/generate_pet_assets.py",
            file=sys.stderr,
        )
        return 1

    print(
        f"asset reproducibility: PASS "
        f"({len(after_pixels)} PNG files, semantic pixel comparison)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
