#!/usr/bin/env python3
import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def load_json(path):
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def authenticate():
    for line in (ROOT / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
        expected, relative = line.split("  ", 1)
        payload = (ROOT / relative).read_bytes()
        actual = hashlib.sha256(payload).hexdigest()
        assert actual == expected, (relative, expected, actual)


def main():
    authenticate()
    result = load_json(ROOT / "result.json")
    baseline = load_json(ROOT / "raw/weber.progress.ndjson")
    hybrid = load_json(ROOT / "raw/hybrid.progress.ndjson")

    assert baseline["index"] == hybrid["index"] == result["target"]["index"]
    assert baseline["state"]["prime"] == hybrid["state"]["prime"]
    assert baseline["status"] == hybrid["status"] == "sound_smoothness_reject"
    assert baseline["sound_early_abort"] and hybrid["sound_early_abort"]
    assert hybrid["sea_strategy"] == "direct-first"
    assert int(hybrid["classical_direct_level_count"]) == 15
    assert int(hybrid["exact_classical_direct_levels"]) == 4
    assert int(hybrid["atkin_classical_direct_levels"]) == 11
    assert hybrid["trace"] == result["native_table_backed_cap_one"]["trace"]

    baseline_sea = int(baseline["timings_us"]["sea"])
    hybrid_sea = int(hybrid["timings_us"]["sea"])
    baseline_total = int(baseline["timings_us"]["total"])
    hybrid_total = int(hybrid["timings_us"]["total"])
    comparison = result["comparison"]
    assert math.isclose(comparison["sea_speedup"], baseline_sea / hybrid_sea)
    assert math.isclose(
        comparison["total_speedup"], baseline_total / hybrid_total)
    assert comparison["weber_levels_saved"] == (
        int(baseline["sea_levels"]) - int(hybrid["sea_levels"]))

    print("p125 direct-first hybrid artifact: PASS")
    print(f"SEA speedup: {baseline_sea / hybrid_sea:.6f}x")
    print(f"total speedup: {baseline_total / hybrid_total:.6f}x")


if __name__ == "__main__":
    main()
