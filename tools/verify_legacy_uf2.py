#!/usr/bin/env python3
"""Verify UF2 blocks fit the legacy nice!nano/SuperMini bootloader window."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

UF2_BLOCK_SIZE = 512
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
APP_START = 0x26000
APP_END = 0xAD000


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} <firmware.uf2>", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    image = path.read_bytes()
    if not image or len(image) % UF2_BLOCK_SIZE:
        print(f"invalid UF2 length: {len(image)} bytes", file=sys.stderr)
        return 1

    targets: list[tuple[int, int]] = []
    for offset in range(0, len(image), UF2_BLOCK_SIZE):
        block = image[offset : offset + UF2_BLOCK_SIZE]
        magic0, magic1 = struct.unpack_from("<II", block, 0)
        target, payload_size = struct.unpack_from("<II", block, 12)
        magic_end = struct.unpack_from("<I", block, 508)[0]
        if (magic0, magic1, magic_end) != (
            UF2_MAGIC_START0,
            UF2_MAGIC_START1,
            UF2_MAGIC_END,
        ):
            print(f"invalid UF2 magic in block {offset // UF2_BLOCK_SIZE}", file=sys.stderr)
            return 1
        targets.append((target, target + payload_size))

    first = min(start for start, _ in targets)
    last = max(end for _, end in targets)
    if first < APP_START or last > APP_END:
        print(
            f"UF2 range 0x{first:05x}..0x{last:05x} exceeds "
            f"legacy window 0x{APP_START:05x}..0x{APP_END:05x}",
            file=sys.stderr,
        )
        return 1

    print(
        f"legacy UF2 range: PASS (0x{first:05x}..0x{last:05x}, "
        f"{APP_END - last} bytes spare)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
