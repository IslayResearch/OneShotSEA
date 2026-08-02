#!/usr/bin/env python3
"""Materialize and verify selected Andrew Sutherland Weber-f tables.

The upstream archive stores one coefficient from each symmetric pair as
``[a,b] c``. This script pins the archive hash, expands both orientations into
the repository's sparse-row format, and writes a per-file SHA-256 manifest.
The separately pinned source catalog binds every admissible archive level below
1000, so compact level subsets can be authenticated without checking all table
payloads into Git.
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
DEFAULT_SOURCE_CATALOG = DEFAULT_OUTPUT / "SOURCE_CATALOG.txt"
ARCHIVE_URL = "https://math.mit.edu/~drew/webermodpoly/phi1.tar.gz"
ARCHIVE_SHA256 = "4ecc78a3163ba7232d67e3b2f5e678a2dbc038c7ee4a9d2e8c00c9e0b5a58176"
ARCHIVE_MAX_LEVEL = 997
ARCHIVE_MAX_BYTES = 128 * 1024 * 1024
ARCHIVE_DOWNLOAD_TIMEOUT_SECONDS = 60
SOURCE_CATALOG_SHA256 = "031c35989f12d8f93c3a992014d6275edb93a21a3a9c70b4b78ce317e7db5dd5"
SOURCE_CATALOG_HEADER = (
    "# OneShotSEA normalized Weber-f source catalog v1; "
    f"archive_sha256={ARCHIVE_SHA256}"
)
ROW = re.compile(r"\[(\d+),(\d+)\]\s+(-?\d+)")
ARCHIVE_MEMBER = re.compile(r"temp/phi1_(\d+)\.new")


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
        if path.stat().st_size > ARCHIVE_MAX_BYTES:
            raise ValueError("Weber table archive exceeds the byte limit")
        payload = path.read_bytes()
    else:
        with urllib.request.urlopen(
            ARCHIVE_URL, timeout=ARCHIVE_DOWNLOAD_TIMEOUT_SECONDS
        ) as response:
            payload = response.read(ARCHIVE_MAX_BYTES + 1)
    if len(payload) > ARCHIVE_MAX_BYTES:
        raise ValueError("Weber table archive exceeds the byte limit")
    if _sha256(payload) != ARCHIVE_SHA256:
        raise ValueError("Weber table archive SHA-256 mismatch")
    return payload


def _normalized_archive_table(
    archive: tarfile.TarFile,
    members: dict[str, tarfile.TarInfo],
    level: int,
) -> bytes:
    name = f"temp/phi1_{level}.new"
    member = members.get(name)
    if member is None or not member.isfile():
        raise ValueError(f"archive is missing {name}")
    stream = archive.extractfile(member)
    if stream is None:
        raise ValueError(f"cannot read {name}")
    return _format(level, _parse_upstream(level, stream.read()))


def build_source_catalog(payload: bytes) -> bytes:
    """Bind every admissible level in the pinned sub-1000 archive."""

    lines = [SOURCE_CATALOG_HEADER]
    expected_levels = _levels(ARCHIVE_MAX_LEVEL)
    with tarfile.open(fileobj=io.BytesIO(payload), mode="r:gz") as archive:
        members = {member.name: member for member in archive.getmembers()}
        actual_levels = sorted(
            int(match.group(1))
            for name in members
            if (match := ARCHIVE_MEMBER.fullmatch(name)) is not None
        )
        if actual_levels != expected_levels:
            raise ValueError(
                "pinned archive does not contain exactly the admissible "
                "prime levels through 997"
            )
        for level in expected_levels:
            formatted = _normalized_archive_table(archive, members, level)
            lines.append(f"{level} {len(formatted)} {_sha256(formatted)}")
    return ("\n".join(lines) + "\n").encode("ascii")


def _parse_source_catalog(payload: bytes) -> dict[int, tuple[int, str]]:
    if SOURCE_CATALOG_SHA256 and _sha256(payload) != SOURCE_CATALOG_SHA256:
        raise ValueError("Weber source catalog SHA-256 mismatch")
    lines = payload.decode("ascii").splitlines()
    if not lines or lines[0] != SOURCE_CATALOG_HEADER:
        raise ValueError("Weber source catalog has the wrong header")
    records: dict[int, tuple[int, str]] = {}
    admissible = set(_levels(ARCHIVE_MAX_LEVEL))
    for line_number, line in enumerate(lines[1:], 2):
        fields = line.split()
        if len(fields) != 3:
            raise ValueError(
                f"malformed Weber source catalog line {line_number}"
            )
        try:
            level = int(fields[0])
            size = int(fields[1])
        except ValueError as error:
            raise ValueError(
                f"invalid Weber source catalog integer on line {line_number}"
            ) from error
        digest = fields[2]
        if (
            level in records
            or level not in admissible
            or size <= 0
            or re.fullmatch(r"[0-9a-f]{64}", digest) is None
        ):
            raise ValueError(
                f"invalid Weber source catalog record on line {line_number}"
            )
        records[level] = (size, digest)
    if sorted(records) != _levels(ARCHIVE_MAX_LEVEL):
        raise ValueError("Weber source catalog level set is incomplete")
    return records


def _load_source_catalog(path: Path) -> tuple[bytes, dict[int, tuple[int, str]]]:
    payload = path.read_bytes()
    return payload, _parse_source_catalog(payload)


def generate(
    output: Path,
    levels: list[int],
    archive_path: Path | None,
    catalog_path: Path = DEFAULT_SOURCE_CATALOG,
) -> None:
    if not levels or levels != sorted(set(levels)):
        raise ValueError("Weber table levels must be nonempty and increasing")
    admissible = set(_levels(ARCHIVE_MAX_LEVEL))
    if any(level not in admissible for level in levels):
        raise ValueError("requested level is not in the pinned sub-1000 archive")
    payload = _load_archive(archive_path)
    catalog_payload, catalog = _load_source_catalog(catalog_path)
    output.mkdir(parents=True, exist_ok=True)
    expected_filenames = {f"phi_{level}.txt" for level in levels}
    existing_filenames = {path.name for path in output.glob("phi_*.txt")}
    if not existing_filenames.issubset(expected_filenames):
        raise ValueError(
            "output contains Weber tables outside the requested level set"
        )
    records: dict[str, dict[str, object]] = {}
    with tarfile.open(fileobj=io.BytesIO(payload), mode="r:gz") as archive:
        members = {member.name: member for member in archive.getmembers()}
        for level in levels:
            formatted = _normalized_archive_table(archive, members, level)
            expected_size, expected_digest = catalog[level]
            if (len(formatted), _sha256(formatted)) != (
                expected_size,
                expected_digest,
            ):
                raise ValueError(
                    f"normalized level {level} does not match source catalog"
                )
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
        "source_catalog_sha256": _sha256(catalog_payload),
        "conversion": "tools/fetch_weber_tables.py",
        "independent_q_expansion_cross_check": "tools/generate_weber_modpoly.py through level 43",
        "max_level": max(levels),
        "levels": levels,
        "files": records,
    }
    (output / "SOURCE_CATALOG.txt").write_bytes(catalog_payload)
    (output / "MANIFEST.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    )


def verify(output: Path) -> None:
    manifest = json.loads((output / "MANIFEST.json").read_text())
    if manifest["source_archive_sha256"] != ARCHIVE_SHA256:
        raise ValueError("manifest archive SHA-256 is not pinned to this converter")
    catalog_payload, catalog = _load_source_catalog(
        output / "SOURCE_CATALOG.txt"
    )
    if manifest.get("source_catalog_sha256") != _sha256(catalog_payload):
        raise ValueError("manifest source-catalog SHA-256 mismatch")
    expected_names = set(manifest["files"])
    actual_names = {path.name for path in output.glob("phi_*.txt")}
    if actual_names != expected_names:
        raise ValueError("Weber table set does not match the manifest")
    for filename, record in manifest["files"].items():
        payload = (output / filename).read_bytes()
        level = record["level"]
        if (
            filename != f"phi_{level}.txt"
            or catalog.get(level) != (record["bytes"], record["sha256"])
            or _sha256(payload) != record["sha256"]
            or len(payload) != record["bytes"]
        ):
            raise ValueError(f"Weber table verification failed: {filename}")
    levels = sorted(record["level"] for record in manifest["files"].values())
    if (
        not levels
        or manifest.get("levels") != levels
        or manifest.get("max_level") != max(levels)
    ):
        raise ValueError("manifest Weber level selection is inconsistent")


def _requested_levels(maximum: int | None, selection: str | None) -> list[int]:
    if maximum is not None and selection is not None:
        raise ValueError("--max-level and --levels are mutually exclusive")
    if selection is not None:
        try:
            levels = [int(value) for value in selection.split(",")]
        except ValueError as error:
            raise ValueError("--levels must be a comma-separated integer list") from error
        if not levels or levels != sorted(set(levels)):
            raise ValueError("--levels must be nonempty and strictly increasing")
        if any(level not in set(_levels(ARCHIVE_MAX_LEVEL)) for level in levels):
            raise ValueError(
                "--levels contains a level outside the pinned sub-1000 archive"
            )
        return levels
    if maximum is None:
        maximum = 401
    if maximum < 5 or maximum > ARCHIVE_MAX_LEVEL:
        raise ValueError("--max-level must be between 5 and 997")
    return _levels(maximum)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--max-level",
        type=int,
        help="inclusive upper bound; materialize every admissible prime level",
    )
    parser.add_argument(
        "--levels",
        help="comma-separated admissible levels for a compact derived table set",
    )
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--source-catalog", type=Path, default=DEFAULT_SOURCE_CATALOG)
    parser.add_argument("--write-source-catalog", type=Path)
    parser.add_argument("--verify-only", action="store_true")
    arguments = parser.parse_args()
    if arguments.write_source_catalog is not None:
        if (
            arguments.verify_only
            or arguments.max_level is not None
            or arguments.levels is not None
        ):
            parser.error(
                "--write-source-catalog conflicts with selection and "
                "verification options"
            )
        arguments.write_source_catalog.write_bytes(
            build_source_catalog(_load_archive(arguments.archive))
        )
        return 0
    if arguments.verify_only:
        verify(arguments.output)
    else:
        levels = _requested_levels(arguments.max_level, arguments.levels)
        generate(
            arguments.output,
            levels,
            arguments.archive,
            arguments.source_catalog,
        )
        verify(arguments.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
