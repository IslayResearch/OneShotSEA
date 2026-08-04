#!/usr/bin/env python3
"""Authenticate the p125 hybrid quotient-reduction evidence."""

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
    "raw/candidate-a.ndjson",
    "raw/candidate-b.ndjson",
    "raw/control-baseline-a.projection.txt",
    "raw/control-baseline-a.timing.txt",
    "raw/control-baseline-b.projection.txt",
    "raw/control-baseline-b.timing.txt",
    "raw/control-candidate-a.projection.txt",
    "raw/control-candidate-a.timing.txt",
    "raw/control-candidate-b.projection.txt",
    "raw/control-candidate-b.timing.txt",
    "raw/production-checkpoint.json",
    "raw/production-progress.ndjson",
}
LEVEL_SCHEMA = "oneshotsea.classical-direct-cohort-level.v1"
CURVE_SCHEMA = "oneshotsea.classical-direct-cohort-curve.v1"
SUMMARY_SCHEMA = "oneshotsea.classical-direct-cohort-summary.v1"
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
    "exact_classical_direct_levels", "atkin_classical_direct_levels",
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


def close(actual, expected, label, tolerance=1e-12):
    require(abs(actual - expected) <= tolerance,
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


def key_values(path):
    values = {}
    for number, line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), 1):
        require("=" in line, f"{path} line {number}: missing equals")
        key, value = line.split("=", 1)
        require(key not in values, f"{path} line {number}: duplicate {key}")
        values[key] = value
    return values


def resolve_tree(commit, expected_tree, label):
    resolved = subprocess.run(
        ["git", "rev-parse", f"{commit}^{{tree}}"], cwd=REPOSITORY,
        check=False, capture_output=True, text=True)
    require(resolved.returncode == 0,
            f"cannot resolve {label} commit: {resolved.stderr.strip()}")
    equal(resolved.stdout.strip(), expected_tree, f"{label} tree")


def projection(record, fields):
    return tuple(record[field] for field in fields)


def validate_profile(name, declared, result):
    path = ROOT / declared["records"]
    equal(digest(path), declared["records_sha256"], f"{name} digest")
    records = read_ndjson(path)
    levels = [row for row in records if row["schema"] == LEVEL_SCHEMA]
    curves = [row for row in records if row["schema"] == CURVE_SCHEMA]
    summaries = [row for row in records if row["schema"] == SUMMARY_SCHEMA]
    equal(len(records), 9, f"{name} record count")
    equal(len(levels), 4, f"{name} level count")
    equal(len(curves), 4, f"{name} curve count")
    equal(len(summaries), 1, f"{name} summary count")
    equal([row["global_index"] for row in levels], EXPECTED_INDICES,
          f"{name} index order")
    target = result["target"]
    context = result["direct_context"]
    summary = summaries[0]
    equal(summary["prime"], target["prime"], f"{name} prime")
    equal(summary["seed"], target["seed"], f"{name} seed")
    equal(summary["range_start"], target["range_start"], f"{name} range")
    equal(int(summary["count"]), target["count"], f"{name} count")
    equal(int(summary["threads"]), target["threads"], f"{name} threads")
    equal(summary["require_point_four"], target["require_point_four"],
          f"{name} point-four policy")
    equal(summary["cache_sha256"], context["sha256"], f"{name} cache")
    equal(int(summary["cache_residency_budget_bytes"]),
          context["resident_budget_bytes"], f"{name} residency")
    equal(int(summary["final_cached_retained_payload_bytes"]),
          context["profiled_level_payload_bytes"], f"{name} payload")
    equal(int(summary["maximum_prime_candidates"]),
          context["maximum_prime_candidates"], f"{name} prime cap")
    equal(int(summary["maximum_x_candidates_per_surface"]),
          context["maximum_x_candidates_per_surface"], f"{name} x cap")
    equal([row["ell"] for row in levels],
          [str(target["profile_level"])] * 4, f"{name} level")
    require(all(not row["exact"] and row["trace_residue"] is None and
                row["atkin_projective_order"] is not None and
                int(row["atkin_residue_count"]) > 0 for row in levels),
            f"{name}: every target record must be certified Atkin")
    level_summary = summary["levels"]
    equal(len(level_summary), 1, f"{name} summary levels")
    equal((int(level_summary[0]["samples"]),
           int(level_summary[0]["exact"]),
           int(level_summary[0]["atkin"]),
           int(level_summary[0]["unconstrained"])),
          (4, 0, 4, 0), f"{name} classification totals")
    evaluation_us = sum(int(row["evaluation_us"]) for row in levels)
    equal(evaluation_us, declared["evaluation_us"], f"{name} evaluation")
    equal(int(level_summary[0]["evaluation_us"]), evaluation_us,
          f"{name} summary evaluation")
    equal(int(summary["generation_us"]), declared["generation_us"],
          f"{name} generation")
    equal(int(summary["cached_level_load_us"]),
          declared["materialization_us"], f"{name} materialization")
    equal(int(summary["process_peak_rss_bytes"]), declared["peak_rss_bytes"],
          f"{name} peak RSS")
    return levels


def validate_controlled(result):
    declared = result["controlled_benchmark"]
    files = declared["files"]
    totals = {
        "baseline": {"target_cpu": 0, "control_cpu": 0,
                     "target_wall": 0, "control_wall": 0},
        "candidate": {"target_cpu": 0, "control_cpu": 0,
                      "target_wall": 0, "control_wall": 0},
    }
    for phase in result["method"]["controlled_order"]:
        projection_path = ROOT / files[phase][0]
        timing_path = ROOT / files[phase][1]
        equal(digest(projection_path), declared["projection_sha256"],
              f"{phase} controlled projection")
        values = key_values(timing_path)
        equal(values["timing.schema"],
              "oneshotsea.p125-poly-trusted-timing.v1",
              f"{phase} timing schema")
        equal(values["timing.mode"], "frobenius-x-control",
              f"{phase} timing mode")
        equal(int(values["timing.degree"]),
              result["method"]["controlled_target_degree"],
              f"{phase} target degree")
        equal(int(values["timing.control_degree"]),
              result["method"]["controlled_control_degree"],
              f"{phase} control degree")
        equal(int(values["timing.repetitions"]),
              result["method"]["controlled_repetitions_per_phase"],
              f"{phase} repetitions")
        cohort = "baseline" if phase.startswith("baseline") else "candidate"
        totals[cohort]["target_cpu"] += int(values["timing.target_cpu_us"])
        totals[cohort]["control_cpu"] += int(values["timing.control_cpu_us"])
        totals[cohort]["target_wall"] += int(
            values["timing.target_elapsed_us"])
        totals[cohort]["control_wall"] += int(
            values["timing.control_elapsed_us"])

    for cohort in ("baseline", "candidate"):
        values = totals[cohort]
        for suffix, key in (
                ("target_cpu_us", "target_cpu"),
                ("control_cpu_us", "control_cpu"),
                ("target_elapsed_us", "target_wall"),
                ("control_elapsed_us", "control_wall")):
            equal(values[key], declared[f"{cohort}_{suffix}"],
                  f"{cohort} controlled {suffix}")
    baseline_cpu_ratio = (totals["baseline"]["target_cpu"] /
                          totals["baseline"]["control_cpu"])
    candidate_cpu_ratio = (totals["candidate"]["target_cpu"] /
                           totals["candidate"]["control_cpu"])
    baseline_wall_ratio = (totals["baseline"]["target_wall"] /
                           totals["baseline"]["control_wall"])
    candidate_wall_ratio = (totals["candidate"]["target_wall"] /
                            totals["candidate"]["control_wall"])
    close(baseline_cpu_ratio, declared["baseline_cpu_ratio"],
          "baseline CPU ratio")
    close(candidate_cpu_ratio, declared["candidate_cpu_ratio"],
          "candidate CPU ratio")
    close(baseline_wall_ratio, declared["baseline_elapsed_ratio"],
          "baseline wall ratio")
    close(candidate_wall_ratio, declared["candidate_elapsed_ratio"],
          "candidate wall ratio")
    cpu_reduction = 100 * (1 - candidate_cpu_ratio / baseline_cpu_ratio)
    wall_reduction = 100 * (1 - candidate_wall_ratio / baseline_wall_ratio)
    close(cpu_reduction, declared["cpu_ratio_reduction_percent"],
          "controlled CPU ratio reduction")
    close(wall_reduction, declared["elapsed_ratio_reduction_percent"],
          "controlled wall ratio reduction")
    require(cpu_reduction > 0 and wall_reduction > 0,
            "controlled target ratio did not improve")
    require(abs(cpu_reduction - wall_reduction) < 0.1,
            "CPU and wall controlled reductions disagree")
    control_drift = abs(totals["candidate"]["control_cpu"] /
                        totals["baseline"]["control_cpu"] - 1)
    require(control_drift < 0.01,
            "controlled CPU workload drift exceeds one percent")
    return cpu_reduction, wall_reduction


def parse_pari(path):
    parsed = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = dict(token.split("=", 1) for token in line.split())
        parsed[fields["index"]] = fields
    return parsed


def validate_production(result, prime):
    declared = result["production_validation"]
    records_path = ROOT / declared["records"]
    checkpoint_path = ROOT / declared["checkpoint"]
    equal(digest(records_path), declared["records_sha256"],
          "production record digest")
    equal(digest(checkpoint_path), declared["checkpoint_sha256"],
          "production checkpoint digest")
    prior_path = REPOSITORY / declared["prior_records"]
    pari_path = REPOSITORY / declared["independent_point_counts"]
    equal(digest(prior_path), declared["prior_records_sha256"],
          "prior production digest")
    equal(digest(pari_path), declared["independent_point_counts_sha256"],
          "PARI point-count digest")
    records = read_ndjson(records_path)
    prior = read_ndjson(prior_path)
    equal(len(records), 4, "production record count")
    equal(len(prior), 4, "prior production record count")
    equal([row["index"] for row in records], EXPECTED_INDICES,
          "production index order")
    equal([projection(row, PRODUCTION_SEMANTICS) for row in records],
          [projection(row, PRODUCTION_SEMANTICS) for row in prior],
          "production semantic replay")
    equal([int(row["classical_direct_level_count"]) for row in records],
          declared["direct_level_counts"], "production direct counts")
    equal([int(row["exact_classical_direct_levels"]) for row in records],
          declared["direct_exact_counts"], "production exact counts")
    equal([int(row["atkin_classical_direct_levels"]) for row in records],
          declared["direct_atkin_counts"], "production Atkin counts")
    equal([int(row["sea_levels"]) for row in records],
          declared["weber_level_counts"], "production Weber counts")
    equal(sum(int(row["classical_direct_level_count"]) for row in records),
          declared["direct_level_evaluations"], "production direct total")
    equal(sum(2 * int(row["final_trace_candidates"]) for row in records),
          declared["smooth_orders"], "production smooth orders")
    equal(sum(row["status"] == "sound_smoothness_reject" for row in records),
          declared["sound_rejections"], "production sound rejections")
    pari = parse_pari(pari_path)
    for row in records:
        index = row["index"]
        equal(row["state"]["schedule_sha256"], declared["schedule_sha256"],
              f"production {index} schedule")
        equal(row["trace"], EXPECTED_TRACES[index],
              f"production {index} trace")
        equal(row["trace"], pari[index]["trace"],
              f"production {index} PARI trace")
        equal(int(pari[index]["order"]), prime + 1 - int(row["trace"]),
              f"production {index} PARI order")
        require(abs(int(row["trace"])) <= math.isqrt(4 * prime),
                f"production {index} trace outside Hasse")
    checkpoint = json.loads(checkpoint_path.read_text(encoding="utf-8"))
    equal(checkpoint["schedule_sha256"], declared["schedule_sha256"],
          "checkpoint schedule")
    equal(checkpoint["next_index"], "2000005", "checkpoint next index")
    equal(checkpoint["counters"]["curves_attempted"], "4",
          "checkpoint attempted curves")
    equal(checkpoint["counters"]["rejected_sound_early_abort"], "4",
          "checkpoint sound rejections")
    equal(checkpoint["counters"]["certificates_found"], "0",
          "checkpoint certificates")


def main():
    authenticate()
    result = json.loads((ROOT / "result.json").read_text(encoding="utf-8"))
    equal(result["schema"], "oneshotsea.p125-direct-hybrid-reduction.v1",
          "result schema")
    resolve_tree(result["implementation"]["commit"],
                 result["implementation"]["tree"], "implementation")
    resolve_tree(result["implementation"]["benchmark_commit"],
                 result["implementation"]["benchmark_tree"], "benchmark")
    resolve_tree(result["baseline"]["commit"], result["baseline"]["tree"],
                 "baseline")
    prime = int(result["target"]["prime"])
    equal(prime, 10**125 + 237, "target prime")
    equal(prime.bit_length(), result["target"]["bits"], "target bits")

    profiles = {}
    for name in ("baseline_a", "candidate_a", "candidate_b", "baseline_b"):
        profiles[name] = validate_profile(name, result["runs"][name], result)
    expected_semantics = [projection(row, LEVEL_SEMANTICS)
                          for row in profiles["baseline_a"]]
    for name in ("candidate_a", "candidate_b", "baseline_b"):
        equal([projection(row, LEVEL_SEMANTICS) for row in profiles[name]],
              expected_semantics, f"{name} level semantics")

    runs = result["runs"]
    comparison = result["profile_comparison"]
    baseline_evaluation = (runs["baseline_a"]["evaluation_us"] +
                           runs["baseline_b"]["evaluation_us"])
    candidate_evaluation = (runs["candidate_a"]["evaluation_us"] +
                            runs["candidate_b"]["evaluation_us"])
    baseline_generation = (runs["baseline_a"]["generation_us"] +
                           runs["baseline_b"]["generation_us"])
    candidate_generation = (runs["candidate_a"]["generation_us"] +
                            runs["candidate_b"]["generation_us"])
    baseline_materialization = (runs["baseline_a"]["materialization_us"] +
                                runs["baseline_b"]["materialization_us"])
    candidate_materialization = (runs["candidate_a"]["materialization_us"] +
                                 runs["candidate_b"]["materialization_us"])
    equal(baseline_evaluation, comparison["baseline_evaluation_us"],
          "baseline evaluation total")
    equal(candidate_evaluation, comparison["candidate_evaluation_us"],
          "candidate evaluation total")
    equal(baseline_generation, comparison["baseline_generation_us"],
          "baseline generation total")
    equal(candidate_generation, comparison["candidate_generation_us"],
          "candidate generation total")
    equal(baseline_materialization,
          comparison["baseline_materialization_us"],
          "baseline materialization total")
    equal(candidate_materialization,
          comparison["candidate_materialization_us"],
          "candidate materialization total")
    raw_reduction = 100 * (1 - candidate_evaluation / baseline_evaluation)
    generation_reduction = 100 * (
        1 - candidate_generation / baseline_generation)
    close(raw_reduction, comparison["raw_target_timer_reduction_percent"],
          "raw target reduction")
    close(generation_reduction,
          comparison["generation_control_reduction_percent"],
          "generation control reduction")
    equal(comparison["general_speedup_inference_allowed"], False,
          "generalized profile timing claim")

    method = result["method"]
    equal(method["baseline_scalar_submul_calls_per_full_reduction"],
          result["target"]["profile_degree"] *
          method["degree_90_full_quotient_coefficients"],
          "baseline scalar submul count")
    equal(method["hybrid_scalar_submul_calls_per_full_reduction"],
          (method["degree_90_full_quotient_coefficients"] -
           method["packed_block_coefficients"]) *
          result["target"]["profile_degree"],
          "hybrid scalar submul count")
    equal(method["scalar_submul_calls_replaced_per_full_reduction"],
          method["baseline_scalar_submul_calls_per_full_reduction"] -
          method["hybrid_scalar_submul_calls_per_full_reduction"],
          "replaced scalar submul count")
    equal(method["packed_convolutions_per_hybrid_block"], 2,
          "hybrid packed convolution count")
    cpu_reduction, wall_reduction = validate_controlled(result)
    validate_production(result, prime)

    equal(result["claims"], {
        "hybrid_top_block_exact": True,
        "controlled_degree_90_ratio_improved": True,
        "degree_95_hybrid_rejected": True,
        "full_reciprocal_degree_96_path_preserved": True,
        "production_semantics_preserved": True,
        "certificate_found": False,
        "production_yield_measured": False,
        "whole_cohort_speedup_claimed": False,
        "end_to_end_speedup_claimed": False,
        "cm_crossover_established": False,
        "asymptotic_exponent_changed": False,
    }, "claim scope")
    print(json.dumps({
        "schema": "oneshotsea.p125-direct-hybrid-reduction-audit.v1",
        "ok": True,
        "implementation_commit": result["implementation"]["commit"],
        "atkin_records": 16,
        "controlled_cpu_ratio_reduction_percent": round(cpu_reduction, 8),
        "controlled_wall_ratio_reduction_percent": round(wall_reduction, 8),
        "production_traces": 4,
        "general_speedup_claimed": False,
    }, sort_keys=True))


if __name__ == "__main__":
    main()
