#!/usr/bin/env python3
"""Audit the retained p125 direct-SEA Atkin MITM comparison."""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path


BUNDLE = Path(__file__).resolve().parent
EXPECTED = {
    "baseline.ndjson": {
        "sha256": "1e229185e3875af0bebe6e032a4f49cf4e25c31e502447c3b2f3060d00ea9cf6",
        "bytes": 148810,
    },
    "factored-mitm.ndjson": {
        "sha256": "4ab42029fe8b63171e016d353af942955d53d2b2dab5ac47c547e5f38cb34e54",
        "bytes": 157923,
    },
}
LEVEL_SCHEMA = "oneshotsea.classical-direct-cohort-level.v1"
CURVE_SCHEMA = "oneshotsea.classical-direct-cohort-curve.v1"
SUMMARY_SCHEMA = "oneshotsea.classical-direct-cohort-summary.v1"
SEMANTIC_LEVEL_KEYS = (
    "global_index",
    "curve_j",
    "selected_side",
    "trace_prior_modulus",
    "trace_prior_residue",
    "ell",
    "exact",
    "trace_residue",
    "atkin_projective_order",
    "atkin_residue_count",
    "information_microbits",
    "schoof_residue",
    "schoof_control_applicable",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def audit_manifest() -> None:
    declared: dict[str, str] = {}
    for line in (BUNDLE / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        assert match is not None, line
        assert match.group(2) not in declared, match.group(2)
        declared[match.group(2)] = match.group(1)
    present = {
        path.name for path in BUNDLE.iterdir()
        if path.is_file() and path.name != "SHA256SUMS"
    }
    assert set(declared) == present, (set(declared), present)
    for name, expected in declared.items():
        assert sha256(BUNDLE / name) == expected, name


def load(name: str) -> tuple[list[dict], list[dict], dict]:
    path = BUNDLE / name
    expected = EXPECTED[name]
    raw = path.read_bytes()
    assert len(raw) == expected["bytes"], (name, len(raw))
    assert sha256(path) == expected["sha256"], name
    rows = [json.loads(line) for line in raw.splitlines()]
    assert len(rows) == 257, (name, len(rows))
    levels = [row for row in rows if row["schema"] == LEVEL_SCHEMA]
    curves = [row for row in rows if row["schema"] == CURVE_SCHEMA]
    summaries = [row for row in rows if row["schema"] == SUMMARY_SCHEMA]
    assert len(levels) == 240, (name, len(levels))
    assert len(curves) == 16, (name, len(curves))
    assert len(summaries) == 1, (name, len(summaries))
    return levels, curves, summaries[0]


def semantic_projection(levels: list[dict]) -> list[tuple[object, ...]]:
    return [tuple(level[key] for key in SEMANTIC_LEVEL_KEYS)
            for level in levels]


def total(summary: dict, field: str) -> int:
    return sum(int(level[field]) for level in summary["levels"])


def main() -> None:
    audit_manifest()
    baseline_levels, _, baseline = load("baseline.ndjson")
    current_levels, _, current = load("factored-mitm.ndjson")

    assert semantic_projection(baseline_levels) == semantic_projection(
        current_levels
    ), "the optimized run changed a direct-level mathematical result"
    assert all(int(level["unconstrained"]) == 0
               for level in current["levels"])
    assert total(current, "schoof_attempts") == 64
    assert total(current, "schoof_validations") == 64
    assert all(
        not level["schoof_control_applicable"]
        or level["schoof_residue"] is not None
        for level in current_levels
    )

    baseline_evaluation = total(baseline, "evaluation_us")
    current_evaluation = total(current, "evaluation_us")
    baseline_rss = int(baseline["process_peak_rss_bytes"])
    current_rss = int(current["process_peak_rss_bytes"])
    assert baseline_evaluation == 263265228
    assert current_evaluation == 33784948
    assert baseline_rss == 1253654528
    assert current_rss == 40943616
    assert baseline_evaluation / current_evaluation > 7.7
    assert baseline_rss / current_rss > 30.0
    print(
        "p125 direct Atkin MITM audit: ok "
        f"(evaluation {baseline_evaluation / current_evaluation:.2f}x, "
        f"peak RSS {baseline_rss / current_rss:.2f}x)"
    )


if __name__ == "__main__":
    main()
