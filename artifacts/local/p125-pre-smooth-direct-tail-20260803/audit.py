#!/usr/bin/env python3
"""Authenticate and rederive the p125 pre-smooth direct-tail A/B."""

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
    "raw/baseline.ndjson",
    "raw/promoted.ndjson",
}
EXPECTED_COMMIT = "6601389fb87866c68f435974a4056e24c0261935"
EXPECTED_TREE = "4d782b51deb1b873949b0922b79498b882d1b8a0"
EXPECTED_INDICES = ["2000001", "2000002", "2000003", "2000004"]
EXPECTED_INITIAL = [1, 13, 4, 5]
EXPECTED_TRACES = {
    "2000001": "-498621923547174620050105080065695461058932825132695425058035790",
    "2000002": "312744557074493258005540218670034986285435355679693042023392238",
    "2000003": "-252845884365417830567895303231394093497235790636298489485509474",
    "2000004": "432966650303160993124127306120296021107647349914430129038843294",
}


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


def read_ndjson(relative):
    records = []
    for number, line in enumerate(
            (ROOT / relative).read_text(encoding="utf-8").splitlines(), 1):
        require(line, f"{relative} line {number}: empty line")
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError as error:
            raise AuditError(
                f"{relative} line {number}: invalid JSON: {error}") from error
    return records


def parse_pari_counts(result):
    evidence = result["independent_point_counts"]
    path = REPOSITORY / evidence["path"]
    require(path.is_file(), f"missing independent point count {path}")
    equal(digest(path), evidence["sha256"], "PARI point-count digest")
    parsed = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = dict(token.split("=", 1) for token in line.split())
        parsed[fields["index"]] = fields
    return parsed


def validate_run(name, declared, records, prime):
    equal(len(records), 4, f"{name} curve count")
    equal([record["index"] for record in records], EXPECTED_INDICES,
          f"{name} indices")
    initial = [int(record["initial_trace_count"]) for record in records]
    final = [int(record["final_trace_candidates"]) for record in records]
    equal(initial, EXPECTED_INITIAL, f"{name} initial trace counts")
    equal(initial, declared["initial_trace_counts"],
          f"{name} declared initial trace counts")
    equal(final, declared["final_trace_counts"],
          f"{name} declared final trace counts")
    for record in records:
        equal(record["status"], "sound_smoothness_reject",
              f'{name} {record["index"]} status')
        equal(record["outcome_class"], "sound_rejection",
              f'{name} {record["index"]} outcome')
        equal((record["sound_early_abort"], record["reached_smoothness"]),
              (True, True), f'{name} {record["index"]} sound flags')
        if record["trace"] is not None:
            require(abs(int(record["trace"])) <= math.isqrt(4 * prime),
                    f'{name} {record["index"]} trace outside Hasse')
    direct_evaluations = sum(
        int(record["classical_direct_level_count"]) for record in records)
    equal(direct_evaluations, declared["direct_level_evaluations"],
          f"{name} direct evaluations")
    smooth_orders = 2 * sum(final)
    equal(smooth_orders, declared["smooth_orders"],
          f"{name} smooth orders")
    for field in ("smoothness", "sea", "direct_first", "total"):
        observed = sum(int(record["timings_us"][field]) for record in records)
        equal(observed, declared["timings_us"][field],
              f"{name} {field} timing")


def main():
    authenticate()
    result = json.loads((ROOT / "result.json").read_text(encoding="utf-8"))
    equal(result["schema"], "oneshotsea.p125-pre-smooth-direct-tail.v1",
          "result schema")
    equal(result["implementation"]["commit"], EXPECTED_COMMIT,
          "implementation commit")
    equal(result["implementation"]["tree"], EXPECTED_TREE,
          "implementation tree")
    resolved = subprocess.run(
        ["git", "rev-parse", f"{EXPECTED_COMMIT}^{{tree}}"],
        cwd=REPOSITORY, check=False, capture_output=True, text=True)
    require(resolved.returncode == 0,
            f"cannot resolve implementation commit: {resolved.stderr.strip()}")
    equal(resolved.stdout.strip(), EXPECTED_TREE, "resolved tree")

    prime = int(result["target"]["prime"])
    equal(prime, 10**125 + 237, "target prime")
    equal(prime.bit_length(), 416, "target bits")
    equal(result["target"]["indices"], EXPECTED_INDICES, "target indices")
    equal(result["direct_policy"]["promoted_min_trace_count"], 2,
          "promotion threshold")

    runs = result["runs"]
    baseline = read_ndjson(runs["baseline"]["records"])
    promoted = read_ndjson(runs["promoted"]["records"])
    equal(digest(ROOT / runs["baseline"]["records"]),
          runs["baseline"]["records_sha256"], "baseline record digest")
    equal(digest(ROOT / runs["promoted"]["records"]),
          runs["promoted"]["records_sha256"], "promoted record digest")
    validate_run("baseline", runs["baseline"], baseline, prime)
    validate_run("promoted", runs["promoted"], promoted, prime)
    equal([int(record["final_trace_candidates"]) for record in baseline],
          EXPECTED_INITIAL, "baseline preserves cap-16 sets")
    equal([int(record["final_trace_candidates"]) for record in promoted],
          [1, 1, 1, 1], "promoted singleton sets")
    equal([int(record["classical_direct_level_count"]) for record in baseline],
          [20, 20, 20, 20], "baseline direct counts")
    equal([int(record["classical_direct_level_count"]) for record in promoted],
          [20, 22, 22, 21], "promoted direct counts")
    equal([record["direct_first"]["attempts"] for record in promoted],
          ["1", "2", "2", "2"], "promoted attempt counts")

    pari = parse_pari_counts(result)
    for record in promoted:
        equal(record["trace"], EXPECTED_TRACES[record["index"]],
              f'promoted {record["index"]} exact trace')
        equal(record["trace"], pari[record["index"]]["trace"],
              f'promoted {record["index"]} PARI trace')
        equal(int(pari[record["index"]]["order"]),
              prime + 1 - int(record["trace"]),
              f'promoted {record["index"]} PARI order')

    comparison = result["comparison"]
    equal(runs["promoted"]["direct_level_evaluations"] -
          runs["baseline"]["direct_level_evaluations"],
          comparison["extra_direct_level_evaluations"],
          "extra direct work")
    equal(runs["baseline"]["smooth_orders"] -
          runs["promoted"]["smooth_orders"],
          comparison["avoided_smooth_orders"], "avoided smooth orders")
    equal(runs["baseline"]["timings_us"]["smoothness"] -
          runs["promoted"]["timings_us"]["smoothness"],
          comparison["smoothness_us_reduction"],
          "smoothness timing reduction")
    equal(runs["baseline"]["timings_us"]["total"] -
          runs["promoted"]["timings_us"]["total"],
          comparison["total_us_reduction"], "total timing reduction")
    equal(comparison["randomized"], False, "timing randomization claim")
    equal(result["claims"], {
        "same_initial_trace_sets": True,
        "promoted_exact_traces_match_independent_counts": True,
        "all_rejections_sound": True,
        "certificate_found": False,
        "production_yield_measured": False,
        "general_speedup_claimed": False,
        "cm_crossover_established": False,
        "asymptotic_exponent_changed": False,
    }, "claim scope")
    print(json.dumps({
        "schema": "oneshotsea.p125-pre-smooth-direct-tail-audit.v1",
        "ok": True,
        "implementation_commit": EXPECTED_COMMIT,
        "initial_trace_counts": EXPECTED_INITIAL,
        "smooth_orders": [runs["baseline"]["smooth_orders"],
                          runs["promoted"]["smooth_orders"]],
        "direct_level_evaluations": [
            runs["baseline"]["direct_level_evaluations"],
            runs["promoted"]["direct_level_evaluations"],
        ],
        "independent_system": result["independent_point_counts"]["system"],
    }, sort_keys=True))


if __name__ == "__main__":
    main()
