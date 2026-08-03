#!/usr/bin/env python3
"""Audit the compact prepared quotient-context p125 corpus A/B evidence."""

from __future__ import annotations

import hashlib
import json
import math
import re
import statistics
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUNDLE = ROOT / "artifacts/local/p125-context-corpus-20260803"
RESULT = BUNDLE / "result.json"
FIELDS = (
    "generation_us",
    "sea_us",
    "total_us",
    "source_lifts_us",
    "modular_roots_us",
    "normalized_codomain_us",
    "bmss_us",
    "eigenvalue_us",
)


def fail(message: str) -> None:
    raise AssertionError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def close(actual: float, expected: float) -> None:
    if not math.isclose(actual, expected, rel_tol=1e-9, abs_tol=1e-6):
        fail(f"{actual!r} != {expected!r}")


def check_summary(runs: list[dict], summary: dict, label: str) -> None:
    for field in FIELDS:
        off = [float(run["timing"][field]) for run in runs
               if run["variant"] == "off"]
        on = [float(run["timing"][field]) for run in runs
              if run["variant"] == "on"]
        if not off or len(off) != len(on):
            fail(f"{label}/{field}: variant run counts differ")
        off_mean = statistics.mean(off)
        on_mean = statistics.mean(on)
        item = summary[field]
        close(off_mean, item["off_mean"])
        close(on_mean, item["on_mean"])
        if on_mean == 0:
            if item["speedup"] is not None:
                fail(f"{label}/{field}: zero-stage speedup is not null")
        else:
            close(off_mean / on_mean, item["speedup"])


def main() -> None:
    checksum_line = (BUNDLE / "SHA256SUMS").read_text(encoding="utf-8").strip()
    match = re.fullmatch(r"([0-9a-f]{64})  \./result\.json", checksum_line)
    if not match or sha256(RESULT) != match.group(1):
        fail("context-corpus result checksum mismatch")

    result = json.loads(RESULT.read_text(encoding="utf-8"))
    if result.get("schema") != "oneshotsea.p125-context-corpus-ab.v2":
        fail("unexpected context-corpus schema")
    if result["common_configuration"]["run_order_per_index"] != \
            ["off", "on", "on", "off"]:
        fail("context-corpus run order changed")

    for level_text in ("193", "277"):
        level = result["levels"][level_text]
        expected_indices = [1000030, 1000031, 1000032]
        if level["index_order"] != expected_indices or \
                not level["all_projections_identical"]:
            fail(f"level {level_text}: identity gate failed")
        aggregate_runs: list[dict] = []
        for index in expected_indices:
            item = level["indices"][str(index)]
            if not re.fullmatch(r"[0-9a-f]{64}", item["projection_sha256"]):
                fail(f"level {level_text}, index {index}: invalid projection digest")
            runs = item["runs"]
            if [(run["variant"], run["sequence"]) for run in runs] != \
                    [("off", 1), ("on", 1), ("on", 2), ("off", 2)]:
                fail(f"level {level_text}, index {index}: run order mismatch")
            check_summary(runs, item["summary"], f"{level_text}/{index}")
            aggregate_runs.extend(runs)
        check_summary(aggregate_runs, level["aggregate_summary"],
                      f"{level_text}/aggregate")

    level193 = result["levels"]["193"]["aggregate_summary"]
    level277 = result["levels"]["277"]["aggregate_summary"]
    if not (0.99 < level193["sea_us"]["speedup"] < 1.01):
        fail("level-193 control is no longer neutral")
    if not (level277["sea_us"]["speedup"] > 1.01 and
            level277["eigenvalue_us"]["speedup"] > 1.05):
        fail("level-277 crossover no longer clears its retained gate")
    print("context-corpus artifact audit: ok")


if __name__ == "__main__":
    main()
