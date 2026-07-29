#!/usr/bin/env python3
"""Verify hashes of the pinned OneShotFastECPP smooth engine."""

from __future__ import annotations

import hashlib
from pathlib import Path
import sys


VENDOR_DIR = Path(__file__).resolve().parent
EXPECTED_HASHES = {
    "smooth.c": "4fe25ccc9dda43a00b042445e9a43080ec282fdfb7e2570b82a2824ef3aa32cb",
    "smooth.h": "cf3f9d2c9e2354d4cc8643772cccbf997e34e05c5d5267e0b46ab039ad1cea3c",
    "LICENSE": "3bdaafd94e539791c916708454fa54183ff6f4956d8a9414053c1f33a065c30f",
}


def main() -> int:
    for name, expected in EXPECTED_HASHES.items():
        path = VENDOR_DIR / name
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != expected:
            print(
                f"hash mismatch for {path}: expected {expected}, got {actual}",
                file=sys.stderr,
            )
            return 1
    print(f"ok: {len(EXPECTED_HASHES)} pinned upstream hashes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
