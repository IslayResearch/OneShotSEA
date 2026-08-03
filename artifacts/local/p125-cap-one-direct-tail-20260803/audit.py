#!/usr/bin/env python3
"""Authenticate and rederive the p125 deferred direct-tail evidence."""

import hashlib
import json
import math
import re
import subprocess
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parent
REPOSITORY = ROOT.parents[2]
EXPECTED_FILES = {
    "README.md",
    "audit.py",
    "result.json",
    "raw/cap-one-tail.ndjson",
    "raw/cohort.checkpoint.json",
    "raw/cohort.progress.ndjson",
}
PREFIX = [
    "7", "5", "11", "13", "19", "17", "23", "29", "31", "37",
    "41", "43", "47", "53", "67", "71", "79", "61", "73", "59",
]


class AuditError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise AuditError(message)


def equal(actual, expected, label):
    require(actual == expected,
            f"{label}: expected {expected!r}, observed {actual!r}")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def authenticate():
    entries = {}
    for number, line in enumerate(
            (ROOT / "SHA256SUMS").read_text(encoding="utf-8").splitlines(), 1):
        fields = line.split("  ", 1)
        equal(len(fields), 2, f"SHA256SUMS line {number} format")
        checksum, relative = fields
        require(re.fullmatch(r"[0-9a-f]{64}", checksum) is not None,
                f"SHA256SUMS line {number}: invalid digest")
        path = PurePosixPath(relative)
        require(not path.is_absolute() and ".." not in path.parts,
                f"SHA256SUMS line {number}: unsafe path")
        require(relative not in entries,
                f"SHA256SUMS line {number}: duplicate path")
        entries[relative] = checksum
    equal(set(entries), EXPECTED_FILES, "manifest file set")
    actual = {
        path.relative_to(ROOT).as_posix()
        for path in ROOT.rglob("*")
        if path.is_file() and path.name != "SHA256SUMS" and
        "__pycache__" not in path.parts
    }
    equal(actual, EXPECTED_FILES, "artifact file set")
    for relative, checksum in entries.items():
        equal(digest(ROOT / relative), checksum, f"SHA-256 {relative}")


def validate_source(result):
    implementation = result["implementation"]
    completed = subprocess.run(
        ["git", "rev-parse", f'{implementation["commit"]}^{{tree}}'],
        cwd=REPOSITORY, check=False, capture_output=True, text=True)
    require(completed.returncode == 0,
            f"cannot resolve implementation commit: {completed.stderr.strip()}")
    equal(completed.stdout.strip(), implementation["tree"],
          "implementation tree")


def read_ndjson(path):
    records = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        require(line, f"{path.name} line {number}: empty line")
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError as error:
            raise AuditError(
                f"{path.name} line {number}: invalid JSON: {error}") from error
    return records


def validate_production(result):
    evidence = result["production_cohort"]
    progress_path = ROOT / "raw/cohort.progress.ndjson"
    checkpoint_path = ROOT / "raw/cohort.checkpoint.json"
    equal(digest(progress_path), evidence["progress_sha256"],
          "production progress digest")
    equal(digest(checkpoint_path), evidence["checkpoint_sha256"],
          "production checkpoint digest")
    records = read_ndjson(progress_path)
    equal(len(records), evidence["curves"], "production curve count")
    equal([record["index"] for record in records],
          ["2000001", "2000002", "2000003", "2000004"],
          "production indices")
    equal([int(record["initial_trace_count"]) for record in records],
          evidence["initial_trace_counts"], "initial trace counts")
    suffix_evaluations = 0
    prefix_evaluations = 0
    for record in records:
        equal(record["schema"], "oneshotsea.search-curve.v1",
              f'curve {record["index"]} schema')
        equal(record["status"], "sound_smoothness_reject",
              f'curve {record["index"]} status')
        equal(record["outcome_class"], "sound_rejection",
              f'curve {record["index"]} outcome')
        equal((record["sound_early_abort"], record["reached_smoothness"]),
              (True, True), f'curve {record["index"]} sound flags')
        equal(record["classical_direct_passes"], "1",
              f'curve {record["index"]} direct passes')
        levels = record["classical_direct_levels"]
        equal([level["ell"] for level in levels], PREFIX,
              f'curve {record["index"]} direct prefix')
        equal({level["pass"] for level in levels}, {"1"},
              f'curve {record["index"]} direct pass numbers')
        prefix_evaluations += len(levels)
        suffix_evaluations += sum(
            level["ell"] in {"89", "97"} for level in levels)
    equal(prefix_evaluations, evidence["direct_prefix_evaluations"],
          "production direct prefix evaluations")
    equal(suffix_evaluations, evidence["direct_suffix_evaluations"],
          "production direct suffix evaluations")
    equal(sum(record["status"] == "sound_smoothness_reject"
              for record in records),
          evidence["sound_smoothness_rejections"],
          "production sound rejections")

    checkpoint = json.loads(checkpoint_path.read_text(encoding="utf-8"))
    last_state = records[-1]["state"]
    equal(checkpoint["next_index"], "2000005", "checkpoint cursor")
    equal(last_state["complete"], True, "terminal progress completion")
    equal(checkpoint["schedule_sha256"],
          result["authenticated_inputs"]["search_schedule_sha256"],
          "checkpoint schedule")
    equal(last_state["schedule_sha256"], checkpoint["schedule_sha256"],
          "progress/checkpoint schedule")
    equal(checkpoint["counters"]["curves_attempted"], "4",
          "checkpoint attempted curves")
    equal(checkpoint["counters"]["rejected_sound_early_abort"], "4",
          "checkpoint sound rejection count")


def parse_pari_counts(result):
    evidence = result["independent_point_counts"]
    path = REPOSITORY / evidence["path"]
    require(path.is_file(), f"missing independent point count {path}")
    equal(digest(path), evidence["sha256"], "PARI point-count digest")
    parsed = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = {}
        for token in line.split():
            require("=" in token, f"malformed PARI token {token!r}")
            key, value = token.split("=", 1)
            require(key and key not in fields,
                    f"duplicate or empty PARI key {key!r}")
            fields[key] = value
        parsed[fields["index"]] = fields
    return parsed


def validate_replay(result, prime):
    evidence = result["cap_one_replay"]
    path = ROOT / "raw/cap-one-tail.ndjson"
    equal(digest(path), evidence["records_sha256"], "replay digest")
    records = read_ndjson(path)
    equal(len(records), 4, "replay record count")
    fixtures = {fixture["index"]: fixture for fixture in evidence["fixtures"]}
    pari = parse_pari_counts(result)
    baseline_us = 0
    direct_us = 0
    tail_loads = 0
    for record in records[:-1]:
        equal(record["schema"], "oneshotsea.p125-cap-one-tail.v1",
              f'replay {record["index"]} schema')
        fixture = fixtures[record["index"]]
        equal(int(record["early_candidates"]), fixture["early_candidates"],
              f'replay {record["index"]} early candidates')
        equal(int(record["baseline_last_weber_level"]),
              fixture["baseline_last_weber_level"],
              f'replay {record["index"]} baseline stop')
        equal(int(record["tail_last_weber_level"]),
              fixture["tail_last_weber_level"],
              f'replay {record["index"]} tail stop')
        equal(record["early_last_weber_level"],
              record["tail_last_weber_level"],
              f'replay {record["index"]} no later Weber work')
        equal(int(record["tail_direct_level_count"]),
              fixture["tail_direct_level_count"],
              f'replay {record["index"]} direct suffix count')
        equal(record["candidates_after_tail"], "1",
              f'replay {record["index"]} singleton')
        equal(record["trace"], fixture["trace"],
              f'replay {record["index"]} trace')
        trace = int(record["trace"])
        require(abs(trace) <= math.isqrt(4 * prime),
                f'replay {record["index"]} trace is outside Hasse')
        equal(pari[record["index"]]["trace"], record["trace"],
              f'replay {record["index"]} PARI trace')
        equal(int(pari[record["index"]]["order"]), prime + 1 - trace,
              f'replay {record["index"]} PARI order')
        equal(record["timings_us"]["tail_weber_continuation"], "0",
              f'replay {record["index"]} tail Weber timing')
        baseline_us += int(record["timings_us"]["baseline_continuation"])
        direct_us += int(record["timings_us"]["direct_tail"])
        tail_loads += int(record["tail_direct_level_count"])
    equal(baseline_us, evidence["baseline_continuation_us"],
          "baseline continuation timing sum")
    equal(direct_us, evidence["direct_tail_us"],
          "direct suffix timing sum")

    summary = records[-1]
    equal(summary["schema"],
          "oneshotsea.p125-cap-one-tail-summary.v1", "replay summary schema")
    expected_loads = len(PREFIX) * len(fixtures) + tail_loads
    equal(int(summary["cache_level_load_count"]), expected_loads,
          "replay lazy context loads")
    equal(expected_loads, evidence["cache_level_load_count"],
          "reported replay context loads")
    equal(int(summary["cache_peak_resident_contexts"]),
          evidence["cache_peak_resident_contexts"],
          "replay peak context residency")


def main():
    authenticate()
    result = json.loads((ROOT / "result.json").read_text(encoding="utf-8"))
    equal(result["schema"], "oneshotsea.p125-cap-one-direct-tail.v1",
          "result schema")
    validate_source(result)
    prime = int(result["target"]["prime"])
    equal(prime, 10**125 + 237, "target prime")
    equal(prime.bit_length(), result["target"]["bits"], "target bits")
    equal([str(level) for level in result["direct_policy"]["early_levels"]],
          PREFIX, "declared early schedule")
    equal(result["direct_policy"]["cap_one_tail_levels"], [89, 97],
          "declared direct suffix")
    equal(result["direct_policy"]["cap_one_tail_count"], 2,
          "declared direct suffix length")
    validate_production(result)
    validate_replay(result, prime)
    equal(result["claims"], {
        "production_rejections_skip_direct_suffix": True,
        "cap_one_exact_traces_preserved": True,
        "certificate_found": False,
        "production_yield_measured": False,
        "controlled_speedup_claimed": False,
        "cm_crossover_established": False,
        "asymptotic_exponent_changed": False,
    }, "claim scope")
    print(json.dumps({
        "schema": "oneshotsea.p125-cap-one-direct-tail-audit.v1",
        "ok": True,
        "implementation_commit": result["implementation"]["commit"],
        "production_suffix_evaluations": 0,
        "replay_cache_loads": result["cap_one_replay"]["cache_level_load_count"],
        "baseline_stops": [fixture["baseline_last_weber_level"]
                           for fixture in result["cap_one_replay"]["fixtures"]],
        "tail_stops": [fixture["tail_last_weber_level"]
                       for fixture in result["cap_one_replay"]["fixtures"]],
        "independent_system": result["independent_point_counts"]["system"],
    }, sort_keys=True))


if __name__ == "__main__":
    main()
