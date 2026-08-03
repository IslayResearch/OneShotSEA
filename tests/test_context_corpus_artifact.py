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


def fail(message: str) -> None:
    raise AssertionError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def close(actual: float, expected: float) -> None:
    if not math.isclose(actual, expected, rel_tol=1e-9, abs_tol=1e-6):
        fail(f"{actual!r} != {expected!r}")


def check_summary(off: dict, on: dict, summary: dict, fields: list[str],
                  label: str) -> None:
    for field in fields:
        if len(off[field]) != 2 or len(on[field]) != 2:
            fail(f"{label}/{field}: expected two runs per variant")
        off_mean = statistics.mean(off[field])
        on_mean = statistics.mean(on[field])
        item = summary[field]
        close(off_mean, item["off_mean"])
        close(on_mean, item["on_mean"])
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
    fields = result["fields"]

    for level_text in ("193", "277"):
        level = result["levels"][level_text]
        expected_indices = [1000030, 1000031, 1000032]
        if level["index_order"] != expected_indices or \
                not level["all_projections_identical"]:
            fail(f"level {level_text}: identity gate failed")
        aggregate_off = {field: [] for field in fields}
        aggregate_on = {field: [] for field in fields}
        for index in expected_indices:
            item = level["indices"][str(index)]
            if not re.fullmatch(r"[0-9a-f]{64}", item["projection_sha256"]):
                fail(f"level {level_text}, index {index}: invalid projection digest")
            check_summary(item["off"], item["on"], item["summary"], fields,
                          f"{level_text}/{index}")
            for field in fields:
                aggregate_off[field].extend(item["off"][field])
                aggregate_on[field].extend(item["on"][field])
        aggregate_summary = level["aggregate_summary"]
        for field in fields:
            off_mean = statistics.mean(aggregate_off[field])
            on_mean = statistics.mean(aggregate_on[field])
            close(off_mean, aggregate_summary[field]["off_mean"])
            close(on_mean, aggregate_summary[field]["on_mean"])
            close(off_mean / on_mean, aggregate_summary[field]["speedup"])

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
