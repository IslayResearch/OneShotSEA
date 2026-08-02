#!/usr/bin/env python3
from __future__ import annotations

import hashlib
from math import gcd, isqrt
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parent
EXPECTED_P = int("1" + "0" * 125) + 237
EXPECTED_COMMIT = "891c9d4362a834d7fba3772ba71a1f93993bd9ec"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def key_values(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key in values:
            raise AssertionError(f"duplicate key in {path.name}:{number}: {key}")
        values[key] = value
    return values


def verify_checksums() -> None:
    recorded: set[str] = set()
    for number, line in enumerate((ROOT / "SHA256SUMS").read_text().splitlines(), 1):
        digest, relative = line.split(maxsplit=1)
        assert re.fullmatch(r"[0-9a-f]{64}", digest), number
        relative = relative.removeprefix("*").removeprefix("./")
        assert relative and relative not in recorded, number
        assert not Path(relative).is_absolute() and ".." not in Path(relative).parts
        target = ROOT / relative
        assert target.is_file() and not target.is_symlink(), relative
        assert sha256(target) == digest, relative
        recorded.add(relative)
    actual = {
        path.name for path in ROOT.iterdir()
        if path.is_file() and path.name != "SHA256SUMS"
    }
    assert recorded == actual, (recorded ^ actual)


def main() -> int:
    if not __debug__:
        raise SystemExit("evidence audit refuses optimized Python (-O)")
    verify_checksums()
    identity = json.loads((ROOT / "identity.json").read_text())
    assert identity["source_commit"] == EXPECTED_COMMIT
    assert identity["binary_sha256"] == sha256(
        ROOT / "benchmark_p125_poly_trusted"
    )
    assert identity["source_catalog_sha256"] == sha256(ROOT / "SOURCE_CATALOG.txt")
    assert identity["magma_launcher_sha256"] == "e62e9d7098bbc60525acea1527b1b514873410147ec5583c1a26768650f8cff8"
    assert identity["magma_runtime_sha256"] == "4047b365a1b8f5468bf0cfca42a62835968a5ecec1b30fb72ddd7205179a969f"

    native_409 = key_values(ROOT / "native-409.stdout")
    native_997 = key_values(ROOT / "native-997.stdout")
    assert native_409["projection.schema"] == "oneshotsea.p125-poly-trusted.v1"
    assert native_409["projection.mode"] == "sea"
    assert int(native_409["prime"]) == EXPECTED_P
    assert native_409["sea.level[0].ell"] == "409"
    assert native_409["sea.level[0].exact"] == "true"
    assert native_409["sea.level[0].trace_residue.present"] == "true"
    assert native_409["sea.level[0].trace_residue.value"] == "19"
    assert native_997["projection.schema"] == "oneshotsea.p125-poly-trusted.v1"
    assert native_997["projection.mode"] == "sea"
    assert int(native_997["prime"]) == EXPECTED_P
    assert native_997["sea.level[0].ell"] == "997"
    assert native_997["sea.level[0].exact"] == "false"
    assert native_997["sea.level[0].trace_residue.present"] == "false"
    assert native_997["sea.level[0].lift_pairs"] == "0"

    magma = json.loads((ROOT / "magma-point-count.json").read_text())
    assert magma["p"] == EXPECTED_P
    assert magma["a"] == int(native_409["generator.curve.a"])
    assert magma["b"] == int(native_409["generator.curve.b"])
    assert magma["a"] == int(native_997["generator.curve.a"])
    assert magma["b"] == int(native_997["generator.curve.b"])
    assert magma["order"] == magma["p"] + 1 - magma["trace"]
    assert abs(magma["trace"]) <= isqrt(4 * magma["p"])
    assert magma["trace"] % 409 == 19
    assert magma["trace"] % 432 == 418
    assert str(magma["order"]) == identity["magma_order"]
    assert str(magma["trace"]) == identity["magma_trace"]
    assert "Magma V2.29-1" in (ROOT / "magma-version.stdout").read_text()

    catalog_lines = (ROOT / "SOURCE_CATALOG.txt").read_text().splitlines()
    assert catalog_lines[0] == (
        "# OneShotSEA normalized Weber-f source catalog v1; "
        f"archive_sha256={identity['source_archive_sha256']}"
    )
    catalog: dict[int, tuple[int, str]] = {}
    for line in catalog_lines[1:]:
        level, byte_count, digest = line.split()
        assert int(level) not in catalog
        assert re.fullmatch(r"[0-9a-f]{64}", digest)
        catalog[int(level)] = (int(byte_count), digest)
    admissible = [
        level for level in range(5, 998)
        if gcd(level, 48) == 1 and all(
            level % divisor for divisor in range(2, isqrt(level) + 1)
        )
    ]
    assert sorted(catalog) == admissible
    assert len(catalog) == 166

    for level in (409, 997):
        manifest = json.loads((ROOT / f"table-{level}-MANIFEST.json").read_text())
        record = manifest["files"][f"phi_{level}.txt"]
        expected = identity[f"table_{level}"]
        assert manifest["levels"] == [level]
        assert manifest["source_archive_sha256"] == identity["source_archive_sha256"]
        assert manifest["source_catalog_sha256"] == identity["source_catalog_sha256"]
        assert record["bytes"] == expected["bytes"]
        assert record["sha256"] == expected["sha256"]
        assert catalog[level] == (record["bytes"], record["sha256"])
        table_identity = hashlib.sha256(
            (
                "oneshotsea.sea-table-manifest.v2\n"
                f"weber-f {level} phi_{level}.txt {record['sha256']}\n"
            ).encode("ascii")
        ).hexdigest()
        native = native_409 if level == 409 else native_997
        assert native["sea.table_manifest_sha256"] == table_identity

    timing_409 = key_values(ROOT / "native-409.stderr")
    timing_997 = key_values(ROOT / "native-997.stderr")
    assert timing_409["timing.schema"] == timing_997["timing.schema"] == (
        "oneshotsea.p125-poly-trusted-timing.v1"
    )
    assert timing_409["timing.mode"] == timing_997["timing.mode"] == "sea"
    legacy = json.loads(
        (ROOT.parent / "p125-weber-catalog-20260802" / "result.json").read_text()
    )
    assert legacy["base_commit"] == EXPECTED_COMMIT
    assert legacy["benchmark_binary_sha256"] == identity["binary_sha256"]
    assert legacy["retained_evidence_bundle"] == (
        "../p125-weber-catalog-magma-20260802"
    )
    legacy_magma = legacy["selected_level_409"]["independent_magma_point_count"]
    assert str(legacy_magma["order"]) == identity["magma_order"]
    assert str(legacy_magma["trace"]) == identity["magma_trace"]
    for legacy_key, timing_key in (
        ("modular_roots", "timing.modular_roots_us"),
        ("normalized_codomain", "timing.normalized_codomain_us"),
        ("bmss", "timing.bmss_us"),
        ("eigenvalue", "timing.eigenvalue_us"),
        ("sea", "timing.sea_us"),
    ):
        assert legacy["selected_level_409"]["timings_us"][legacy_key] == int(
            timing_409[timing_key]
        )
    for legacy_key, timing_key in (
        ("modular_roots", "timing.modular_roots_us"),
        ("sea", "timing.sea_us"),
    ):
        assert legacy["selected_level_997"]["timings_us"][legacy_key] == int(
            timing_997[timing_key]
        )

    print("p125 Weber catalog/Magma evidence audit ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
