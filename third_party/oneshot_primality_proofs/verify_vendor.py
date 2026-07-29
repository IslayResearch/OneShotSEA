#!/usr/bin/env python3
"""Verify the canonical verifier pin and its black-box fixture behavior."""

from __future__ import annotations

import hashlib
from pathlib import Path
import subprocess
import sys


VENDOR_DIR = Path(__file__).resolve().parent
REPOSITORY_ROOT = VENDOR_DIR.parents[1]
VERIFIER = VENDOR_DIR / "voneshot.py"
FIXTURES = REPOSITORY_ROOT / "tests" / "fixtures" / "verifier"
EXPECTED_HASHES = {
    VERIFIER: "e0ba3b8a7ed2ff48bd2fd824642bf67b0954a9f03f57daeb4ac4302691e1b666",
    VENDOR_DIR / "LICENSE": "08580c29b868b2aa04f2823732d20656543cc229bef8c9c8aa89e1eec8fdc7e2",
}

# name, expected exit status, expected stdout, required stderr fragment
CASES = (
    ("valid_n4.cert", 0, "True", ""),
    ("malformed_bad_large_factor.cert", 1, "False", ""),
    ("malformed_corrupt_order.cert", 1, "False", ""),
    ("malformed_invalid_token.cert", 2, "", "must be integers"),
    ("malformed_too_few_fields.cert", 2, "", "usage:"),
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    for path, expected in EXPECTED_HASHES.items():
        actual = sha256(path)
        if actual != expected:
            print(f"hash mismatch for {path}: expected {expected}, got {actual}", file=sys.stderr)
            return 1

    for name, expected_status, expected_stdout, stderr_fragment in CASES:
        arguments = (FIXTURES / name).read_text(encoding="ascii").split()
        completed = subprocess.run(
            [sys.executable, str(VERIFIER), *arguments],
            check=False,
            capture_output=True,
            text=True,
        )
        if (
            completed.returncode != expected_status
            or completed.stdout.strip() != expected_stdout
            or stderr_fragment not in completed.stderr
        ):
            print(
                f"{name}: expected status/stdout/stderr fragment "
                f"{expected_status}/{expected_stdout!r}/{stderr_fragment!r}; got "
                f"{completed.returncode}/{completed.stdout.strip()!r}/{completed.stderr.strip()!r}",
                file=sys.stderr,
            )
            return 1

    print(f"ok: {len(EXPECTED_HASHES)} pinned hashes and {len(CASES)} verifier fixtures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
