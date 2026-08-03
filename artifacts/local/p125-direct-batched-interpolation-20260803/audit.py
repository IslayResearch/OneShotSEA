#!/usr/bin/env python3
"""Authenticate and rederive the p125 batched-interpolation A/B/A."""

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
    "raw/baseline-a.ndjson",
    "raw/baseline-b.ndjson",
    "raw/candidate.ndjson",
    "raw/production-candidate.ndjson",
}
LEVEL_SCHEMA = "oneshotsea.classical-direct-cohort-level.v1"
CURVE_SCHEMA = "oneshotsea.classical-direct-cohort-curve.v1"
SUMMARY_SCHEMA = "oneshotsea.classical-direct-cohort-summary.v1"
EXPECTED_LEVELS = ["61", "67", "71", "73", "79", "83", "89", "97"]
EXPECTED_INDICES = ["2000001", "2000002", "2000003", "2000004"]
EXPECTED_TRACES = {
    "2000001": "-498621923547174620050105080065695461058932825132695425058035790",
    "2000002": "312744557074493258005540218670034986285435355679693042023392238",
    "2000003": "-252845884365417830567895303231394093497235790636298489485509474",
    "2000004": "432966650303160993124127306120296021107647349914430129038843294",
}
LEVEL_SEMANTICS = (
    "global_index", "curve_j", "selected_side", "trace_prior_modulus",
    "trace_prior_residue", "ell", "exact", "trace_residue",
    "atkin_projective_order", "atkin_residue_count",
    "information_microbits", "schoof_residue", "schoof_control_applicable",
)
PRODUCTION_SEMANTICS = (
    "index", "status", "outcome_class", "sound_early_abort",
    "full_point_count", "reached_smoothness", "sea_levels", "direct_first",
    "classical_direct_passes", "classical_direct_level_count",
    "initial_trace_count", "final_trace_candidates", "trace",
)


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


def read_ndjson(path):
    records = []
    for number, line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), 1):
        require(line, f"{path} line {number}: empty line")
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError as error:
            raise AuditError(
                f"{path} line {number}: invalid JSON: {error}") from error
    return records


def resolve_tree(commit, expected_tree, label):
    resolved = subprocess.run(
        ["git", "rev-parse", f"{commit}^{{tree}}"], cwd=REPOSITORY,
        check=False, capture_output=True, text=True)
    require(resolved.returncode == 0,
            f"cannot resolve {label} commit: {resolved.stderr.strip()}")
    equal(resolved.stdout.strip(), expected_tree, f"{label} tree")


def validate_profile(name, declared, target, direct_context):
    path = ROOT / declared["records"]
    equal(digest(path), declared["records_sha256"], f"{name} record digest")
    records = read_ndjson(path)
    levels = [record for record in records if record["schema"] == LEVEL_SCHEMA]
    curves = [record for record in records if record["schema"] == CURVE_SCHEMA]
    summaries = [record for record in records
                 if record["schema"] == SUMMARY_SCHEMA]
    equal(len(records), 37, f"{name} record count")
    equal(len(levels), declared["level_records"], f"{name} level count")
    equal(len(curves), 4, f"{name} curve count")
    equal(len(summaries), 1, f"{name} summary count")
    equal([(record["global_index"], record["ell"]) for record in levels],
          [(index, ell) for index in EXPECTED_INDICES for ell in EXPECTED_LEVELS],
          f"{name} level order")
    summary = summaries[0]
    equal(summary["prime"], target["prime"], f"{name} prime")
    equal(summary["seed"], target["seed"], f"{name} seed")
    equal(summary["range_start"], target["range_start"], f"{name} range")
    equal(int(summary["count"]), target["count"], f"{name} curve count config")
    equal(int(summary["threads"]), target["threads"], f"{name} threads")
    equal(summary["cache_sha256"], direct_context["sha256"],
          f"{name} cache digest")
    equal(int(summary["cache_residency_budget_bytes"]),
          direct_context["resident_budget_bytes"], f"{name} cache budget")
    equal(int(summary["final_cached_retained_payload_bytes"]),
          direct_context["matrix_payload_bytes"], f"{name} matrix payload")
    observed = sum(int(record["evaluation_us"]) for record in levels)
    equal(observed, declared["evaluation_us"], f"{name} level timing sum")
    equal(sum(int(record["evaluation_us"]) for record in curves), observed,
          f"{name} curve timing sum")
    equal(sum(int(level["evaluation_us"]) for level in summary["levels"]),
          observed, f"{name} summary timing sum")
    return levels, int(summary["process_peak_rss_bytes"])


def projection(record, fields):
    return tuple(record[field] for field in fields)


def parse_pari(path):
    parsed = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = dict(token.split("=", 1) for token in line.split())
        parsed[fields["index"]] = fields
    return parsed


def validate_production(result, prime):
    declared = result["production_validation"]
    candidate_path = ROOT / declared["records"]
    equal(digest(candidate_path), declared["records_sha256"],
          "production candidate digest")
    prior_path = REPOSITORY / declared["prior_records"]
    pari_path = REPOSITORY / declared["independent_point_counts"]
    equal(digest(prior_path), declared["prior_records_sha256"],
          "prior production digest")
    equal(digest(pari_path), declared["independent_point_counts_sha256"],
          "PARI point-count digest")
    candidate = read_ndjson(candidate_path)
    prior = read_ndjson(prior_path)
    equal(len(candidate), 4, "production candidate count")
    equal(len(prior), 4, "prior production count")
    equal([record["index"] for record in candidate], EXPECTED_INDICES,
          "production indices")
    equal([projection(record, PRODUCTION_SEMANTICS) for record in candidate],
          [projection(record, PRODUCTION_SEMANTICS) for record in prior],
          "production semantic replay")
    pari = parse_pari(pari_path)
    for record in candidate:
        index = record["index"]
        equal(record["state"]["schedule_sha256"],
              declared["schedule_sha256"], f"production {index} schedule")
        equal(record["trace"], EXPECTED_TRACES[index],
              f"production {index} expected trace")
        equal(record["trace"], pari[index]["trace"],
              f"production {index} PARI trace")
        equal(int(pari[index]["order"]), prime + 1 - int(record["trace"]),
              f"production {index} PARI order")
        require(abs(int(record["trace"])) <= math.isqrt(4 * prime),
                f"production {index} trace outside Hasse")


def main():
    authenticate()
    result = json.loads((ROOT / "result.json").read_text(encoding="utf-8"))
    equal(result["schema"], "oneshotsea.p125-direct-batched-interpolation.v1",
          "result schema")
    resolve_tree(result["implementation"]["commit"],
                 result["implementation"]["tree"], "implementation")
    resolve_tree(result["baseline"]["commit"],
                 result["baseline"]["tree"], "baseline")
    target = result["target"]
    prime = int(target["prime"])
    equal(prime, 10**125 + 237, "target prime")
    equal(prime.bit_length(), target["bits"], "target bits")
    equal([str(level) for level in target["levels"]], EXPECTED_LEVELS,
          "target levels")

    profiles = {}
    rss = {}
    for name in ("baseline_a", "candidate", "baseline_b"):
        profiles[name], rss[name] = validate_profile(
            name, result["runs"][name], target, result["direct_context"])
    baseline_projection = [projection(record, LEVEL_SEMANTICS)
                           for record in profiles["baseline_a"]]
    equal([projection(record, LEVEL_SEMANTICS)
           for record in profiles["candidate"]], baseline_projection,
          "candidate level semantics")
    equal([projection(record, LEVEL_SEMANTICS)
           for record in profiles["baseline_b"]], baseline_projection,
          "second baseline level semantics")
    equal(len(set(rss.values())), 1, "peak RSS equality")

    runs = result["runs"]
    comparison = result["comparison"]
    bracket_mean = (runs["baseline_a"]["evaluation_us"] +
                    runs["baseline_b"]["evaluation_us"]) / 2
    ratio = runs["candidate"]["evaluation_us"] / bracket_mean
    reduction = 100 * (1 - ratio)
    equal(bracket_mean, comparison["baseline_bracket_mean_evaluation_us"],
          "baseline bracket mean")
    require(abs(ratio - comparison["candidate_to_baseline_ratio"]) < 1e-10,
            "candidate/baseline ratio does not rederive")
    require(abs(reduction - comparison["evaluation_us_reduction_percent"])
            < 1e-8, "timing reduction does not rederive")
    require(reduction > 0, "candidate did not improve the bracket mean")
    validate_production(result, prime)

    equal(result["claims"], {
        "direct_specialization_constant_factor_improved": True,
        "production_semantics_preserved": True,
        "certificate_found": False,
        "production_yield_measured": False,
        "end_to_end_speedup_claimed": False,
        "cm_crossover_established": False,
        "asymptotic_exponent_changed": False,
    }, "claim scope")
    print(json.dumps({
        "schema": "oneshotsea.p125-direct-batched-interpolation-audit.v1",
        "ok": True,
        "implementation_commit": result["implementation"]["commit"],
        "level_records": len(profiles["candidate"]),
        "evaluation_us": [runs["baseline_a"]["evaluation_us"],
                          runs["candidate"]["evaluation_us"],
                          runs["baseline_b"]["evaluation_us"]],
        "evaluation_us_reduction_percent": round(reduction, 8),
        "production_traces": len(EXPECTED_TRACES),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
