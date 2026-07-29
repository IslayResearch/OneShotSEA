#!/usr/bin/env python3
"""Fetch, normalize, and verify Andrew Sutherland's Weber-f tables.

The upstream archive stores one coefficient from each symmetric pair as
``[a,b] c``. This script pins the archive hash, expands both orientations into
the repository's sparse-row format, and writes a per-file SHA-256 manifest.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
from math import gcd
from pathlib import Path
import re
import tarfile
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "data" / "modpoly" / "weber_f"
ARCHIVE_URL = "https://math.mit.edu/~drew/webermodpoly/phi1.tar.gz"
ARCHIVE_SHA256 = "4ecc78a3163ba7232d67e3b2f5e678a2dbc038c7ee4a9d2e8c00c9e0b5a58176"
ROW = re.compile(r"\[(\d+),(\d+)\]\s+(-?\d+)")


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _is_prime(value: int) -> bool:
    return value >= 2 and all(
        value % divisor for divisor in range(2, int(value**0.5) + 1)
    )


def _levels(maximum: int) -> list[int]:
    return [
        level
        for level in range(5, maximum + 1)
        if _is_prime(level) and gcd(level, 48) == 1
    ]


def _parse_upstream(level: int, payload: bytes) -> list[tuple[int, int, int]]:
    terms: list[tuple[int, int, int]] = []
    seen: dict[tuple[int, int], int] = {}
    for line_number, raw_line in enumerate(payload.decode("ascii").splitlines(), 1):
        match = ROW.fullmatch(raw_line.strip())
        if match is None:
            raise ValueError(f"malformed upstream level-{level} row {line_number}")
        x_degree, y_degree, coefficient = map(int, match.groups())
        for key in ((x_degree, y_degree), (y_degree, x_degree)):
            previous = seen.get(key)
            if previous is not None and previous != coefficient:
                raise ValueError(f"inconsistent symmetric coefficient at level {level}")
            seen[key] = coefficient
        terms.append((x_degree, y_degree, coefficient))
    if seen.get((level + 1, 0)) != 1 or seen.get((level, level)) != -1:
        raise ValueError(f"level {level} has the wrong Weber normalization")
    for (x_degree, y_degree), coefficient in seen.items():
        if coefficient and (level * x_degree + y_degree - level - 1) % 24:
            raise ValueError(f"level {level} violates Weber sparsity")
    return terms


def _format(level: int, terms: list[tuple[int, int, int]]) -> bytes:
    lines = [f"# Weber-f modular polynomial Phi_{level}^f(X,Y); r=q^(1/48)."]
    for x_degree, y_degree, coefficient in terms:
        lines.append(f"{x_degree} {y_degree} {coefficient}")
        if x_degree != y_degree:
            lines.append(f"{y_degree} {x_degree} {coefficient}")
    return ("\n".join(lines) + "\n").encode("ascii")


def _load_archive(path: Path | None) -> bytes:
    if path is not None:
        payload = path.read_bytes()
    else:
        with urllib.request.urlopen(ARCHIVE_URL) as response:
            payload = response.read()
    if _sha256(payload) != ARCHIVE_SHA256:
        raise ValueError("Weber table archive SHA-256 mismatch")
    return payload


def generate(output: Path, maximum: int, archive_path: Path | None) -> None:
    payload = _load_archive(archive_path)
    output.mkdir(parents=True, exist_ok=True)
    records: dict[str, dict[str, object]] = {}
    with tarfile.open(fileobj=io.BytesIO(payload), mode="r:gz") as archive:
        members = {member.name: member for member in archive.getmembers()}
        for level in _levels(maximum):
            name = f"temp/phi1_{level}.new"
            member = members.get(name)
            if member is None or not member.isfile():
                raise ValueError(f"archive is missing {name}")
            stream = archive.extractfile(member)
            if stream is None:
                raise ValueError(f"cannot read {name}")
            formatted = _format(level, _parse_upstream(level, stream.read()))
            filename = f"phi_{level}.txt"
            (output / filename).write_bytes(formatted)
            records[filename] = {
                "level": level,
                "sha256": _sha256(formatted),
                "bytes": len(formatted),
            }
    manifest = {
        "format": "full symmetric sparse rows: x_degree y_degree integer_coefficient",
        "normalization": "f=zeta_48^-1*eta((z+1)/2)/eta(z), r=q^(1/48)",
        "source": ARCHIVE_URL,
        "source_page": "https://math.mit.edu/~drew/WeberModPolys.html",
        "source_archive_sha256": ARCHIVE_SHA256,
        "conversion": "tools/fetch_weber_tables.py",
        "independent_q_expansion_cross_check": "tools/generate_weber_modpoly.py through level 43",
        "max_level": maximum,
        "files": records,
    }
    (output / "MANIFEST.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    )


def verify(output: Path) -> None:
    manifest = json.loads((output / "MANIFEST.json").read_text())
    if manifest["source_archive_sha256"] != ARCHIVE_SHA256:
        raise ValueError("manifest archive SHA-256 is not pinned to this converter")
    expected_names = set(manifest["files"])
    actual_names = {path.name for path in output.glob("phi_*.txt")}
    if actual_names != expected_names:
        raise ValueError("Weber table set does not match the manifest")
    for filename, record in manifest["files"].items():
        payload = (output / filename).read_bytes()
        if _sha256(payload) != record["sha256"] or len(payload) != record["bytes"]:
            raise ValueError(f"Weber table verification failed: {filename}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--max-level", type=int, default=401)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--verify-only", action="store_true")
    arguments = parser.parse_args()
    if arguments.verify_only:
        verify(arguments.output)
    else:
        if arguments.max_level < 5 or not _is_prime(arguments.max_level):
            raise ValueError("--max-level must be a prime at least 5")
        generate(arguments.output, arguments.max_level, arguments.archive)
        verify(arguments.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
