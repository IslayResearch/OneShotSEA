#!/usr/bin/env python3
"""Audit the retained controlled p125 direct-SEA combined-patch A/B."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Any


BUNDLE = Path(__file__).resolve().parent
U64_MAX = (1 << 64) - 1
PRIME = str(10**125 + 237)
LEVELS = ["5", "7", "11", "13", "17", "19", "23", "29", "31",
          "37", "41", "43", "47", "53", "59"]
INDICES = [str(index) for index in range(2_000_000, 2_000_016)]
CACHE_SHA256 = "b31c858c5398d7b284ad7b003ce4647211de8dec0412421f90ed226f2926ecfd"
BASELINE_COMMIT = "bbcd04d2c27d87f582f4d579caaacd9d4278ee8e"
BASELINE_TREE = "5e203de1b77ddd89f9393a8c01e225d2c2f09e2c"
CANDIDATE_COMMIT = "13cd3a906167700b795b36abaae51c6433017fc8"
CANDIDATE_TREE = "8cdaa62a5465416e7b9bfcdde32f9e8f016e9a41"
HARNESS_SHA256 = "2ac700e044795325463ee462cd44bdd39c3919e594225cf4632257b2fe89cd27"

EXPECTED_RAW = {
    "baseline.ndjson": {
        "bytes": 158555,
        "sha256": "7a28b7602fcb00ccae88f64b5d388fb3c16826be62a74cdc982f2fc2594696de",
    },
    "combined-candidate.ndjson": {
        "bytes": 157932,
        "sha256": "38eeeb936a45972977d232dfa011e9a1c9fc3ecc0a3f2ded93b3550e600d619e",
    },
}

LEVEL_SCHEMA = "oneshotsea.classical-direct-cohort-level.v1"
CURVE_SCHEMA = "oneshotsea.classical-direct-cohort-curve.v1"
SUMMARY_SCHEMA = "oneshotsea.classical-direct-cohort-summary.v1"
RESULT_SCHEMA = "oneshotsea.p125-direct-atkin-combined-comparison.v2"

LEVEL_KEYS = {
    "schema", "global_index", "curve_j", "selected_side",
    "trace_prior_modulus", "trace_prior_residue", "ell", "exact",
    "trace_residue", "atkin_projective_order", "atkin_residue_count",
    "information_microbits", "evaluation_us", "materialization_count",
    "materialization_us", "schoof_residue", "schoof_control_applicable",
    "schoof_us", "process_peak_rss_bytes",
}
CURVE_KEYS = {
    "schema", "global_index", "levels", "generation_us", "evaluation_us",
    "materialization_us", "schoof_us", "profile_us",
    "process_peak_rss_bytes",
}
SUMMARY_KEYS = {
    "schema", "prime", "seed", "range_start", "count", "threads",
    "require_point_four", "maximum_prime_candidates",
    "maximum_x_candidates_per_surface", "schoof_through", "cache_sha256",
    "cache_index_us", "cache_residency_budget_bytes",
    "cached_level_load_count", "cached_level_load_us",
    "cached_context_evictions", "final_cached_retained_contexts",
    "final_cached_retained_payload_bytes", "process_peak_rss_bytes",
    "generation_us", "elapsed_us", "levels",
    "warm_information_per_cost_order", "observed_information_per_cost_order",
    "claim_scope",
}
SUMMARY_LEVEL_KEYS = {
    "ell", "samples", "exact", "atkin", "unconstrained",
    "information_microbits", "evaluation_us", "materializations",
    "materialization_us", "schoof_attempts", "schoof_validations",
    "schoof_us",
}
SEMANTIC_LEVEL_KEYS = (
    "global_index", "curve_j", "selected_side", "trace_prior_modulus",
    "trace_prior_residue", "ell", "exact", "trace_residue",
    "atkin_projective_order", "atkin_residue_count",
    "information_microbits", "schoof_residue", "schoof_control_applicable",
)
CONFIG_KEYS = (
    "prime", "seed", "range_start", "count", "threads",
    "require_point_four", "maximum_prime_candidates",
    "maximum_x_candidates_per_surface", "schoof_through", "cache_sha256",
    "cache_residency_budget_bytes",
)
EXPECTED_CONFIG = {
    "prime": PRIME,
    "seed": "202607300000",
    "range_start": "2000000",
    "count": "16",
    "threads": "1",
    "require_point_four": True,
    "maximum_prime_candidates": "10000000",
    "maximum_x_candidates_per_surface": "1000000",
    "schoof_through": "13",
    "cache_sha256": CACHE_SHA256,
    "cache_residency_budget_bytes": "1000000000",
}
SHARED_ARGV = [
    "./build/profile_classical_direct_cohort", "--p", PRIME,
    "--seed", "202607300000", "--range-start", "2000000", "--count", "16",
    "--threads", "1", "--require-point4", "1", "--cache",
    "/private/tmp/p125-direct-low-5-59.ctx", "--cache-sha256", CACHE_SHA256,
    "--cache-resident-bytes", "1000000000", "--schoof-through", "13",
    "--maximum-prime-candidates", "10000000", "--maximum-x-candidates",
    "1000000", *LEVELS,
]


class AuditError(RuntimeError):
    """Evidence violates the audited contract."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AuditError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def decimal(value: Any, label: str, *, u64: bool = True) -> int:
    require(isinstance(value, str), f"{label}: expected decimal string")
    require(re.fullmatch(r"0|[1-9][0-9]*", value) is not None,
            f"{label}: non-canonical decimal")
    parsed = int(value)
    if u64:
        require(parsed <= U64_MAX, f"{label}: exceeds uint64")
    return parsed


def exact_keys(row: dict[str, Any], expected: set[str], label: str) -> None:
    require(set(row) == expected,
            f"{label}: keys differ: missing={expected - set(row)}, "
            f"extra={set(row) - expected}")


def audit_manifest() -> None:
    declared: dict[str, str] = {}
    for line_number, line in enumerate(
            (BUNDLE / "SHA256SUMS").read_text(encoding="utf-8").splitlines(), 1):
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        require(match is not None, f"SHA256SUMS:{line_number}: malformed")
        name = match.group(2)
        require(name not in declared, f"SHA256SUMS: duplicate {name}")
        declared[name] = match.group(1)
    present = {
        path.name for path in BUNDLE.iterdir()
        if path.is_file() and path.name != "SHA256SUMS"
    }
    require(set(declared) == present,
            f"manifest membership differs: declared={set(declared)}, present={present}")
    for name, expected in declared.items():
        require(sha256(BUNDLE / name) == expected,
                f"manifest digest mismatch: {name}")


def audit_provenance() -> dict[str, Any]:
    provenance = json.loads((BUNDLE / "provenance.json").read_text("utf-8"))
    require(provenance["schema"] ==
            "oneshotsea.p125-direct-atkin-combined-provenance.v1",
            "provenance schema")
    comparison = provenance["comparison"]
    require(comparison["baseline"] == {
        "commit": BASELINE_COMMIT, "tree": BASELINE_TREE},
        "baseline source identity")
    require(comparison["candidate"]["commit"] == CANDIDATE_COMMIT,
            "candidate commit identity")
    require(comparison["candidate"]["tree"] == CANDIDATE_TREE,
            "candidate tree identity")
    require(comparison["candidate"]["parent_commit"] == BASELINE_COMMIT,
            "candidate parent identity")
    require(comparison["candidate_components"] == [
        "factored CRT meet-in-the-middle exact counting and bounded enumeration",
        "uniform irreducible factor-degree certificate replacing full splitting",
        "baby-step/giant-step modular composition used by the certificate",
    ], "candidate component attribution")

    harness = provenance["shared_harness"]
    require(harness["source_commit"] == CANDIDATE_COMMIT,
            "harness source commit")
    require(harness["path"] == "tools/profile_classical_direct_cohort.cpp",
            "harness path")
    require(harness["sha256"] == HARNESS_SHA256, "harness digest")

    compiler = provenance["build"]["compiler"]
    require(compiler == {
        "path": "/usr/bin/clang++",
        "sha256": "179301dcb41ea78accc3fa0048a7e6f6710d891945a751a34addd622020c1818",
        "version": "Apple clang version 21.0.0 (clang-2100.1.1.101)",
        "target": "arm64-apple-darwin25.5.0",
    }, "compiler provenance")
    require(provenance["build"]["cppflags"] ==
            ["-Iinclude", "-isystem", "/opt/homebrew/opt/gmp/include"],
            "CPPFLAGS provenance")
    require(provenance["build"]["cxxflags"] == [
        "-O2", "-g", "-std=c++20", "-Wall", "-Wextra", "-Wpedantic",
        "-Wconversion", "-Wshadow",
    ], "CXXFLAGS provenance")
    require(provenance["build"]["link_flags"] == [
        "-L/opt/homebrew/opt/gmp/lib", "-lgmpxx", "-lgmp"],
        "link flags provenance")
    libraries = provenance["build"]["libraries"]
    require(libraries[0]["sha256"] ==
            "1125ffbace543d5bc01769bbe4a89ec56907313506f10d6c654c1aa8f9b582d1",
            "libgmpxx digest")
    require(libraries[1]["sha256"] ==
            "14123464af436d67ef69114810aa9e1e74de50e4097166fe8c110397b3ba6961",
            "libgmp digest")
    require(provenance["build"]["system_dyld_shared_cache"] == {
        "path": "/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/"
                "dyld_shared_cache_arm64e",
        "bytes": 573440,
        "sha256":
            "2d5ec323938a842298610eb66a51e2307a2e10a220b26432672c7c4f8e7256a9",
        "covers": ["/usr/lib/libc++.1.dylib", "/usr/lib/libSystem.B.dylib"],
    }, "system runtime shared-cache identity")

    host = provenance["host"]
    canonical_host = json.dumps(
        host["fingerprint_payload"], sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    require(hashlib.sha256(canonical_host).hexdigest() ==
            host["fingerprint_sha256"], "host fingerprint")
    require(host["fingerprint_sha256"] ==
            "4d54249c3b6147b2fda4fdf4f64f28e6383c0971b948431bd761d7badab7104b",
            "captured host identity")

    invocation = provenance["shared_invocation"]
    require(invocation["argv"] == SHARED_ARGV, "shared invocation argv")
    require(invocation["cache"] == {
        "path": "/private/tmp/p125-direct-low-5-59.ctx",
        "bytes": 30203068,
        "sha256": CACHE_SHA256,
    }, "shared cache identity")

    expected_runs = {
        "baseline": {
            "raw": "baseline.ndjson",
            "completed_at_utc": "2026-08-03T08:58:37Z",
            "working_directory":
                "/private/tmp/oneshotsea-atkin-controlled.LB2f5d/baseline",
            "raw_bytes": 158555,
            "raw_sha256": EXPECTED_RAW["baseline.ndjson"]["sha256"],
            "profiler_binary_bytes": 842544,
            "profiler_binary_sha256":
                "80ed27a664e150599cb48be04009e181f2ae2671a0e2661031e8ea752e3ab147",
            "static_library_bytes": 23067384,
            "static_library_sha256":
                "61ef70cd3f53bf6e809e32e071fbfac97ee065e301be2fec792f4559b855d37d",
        },
        "candidate": {
            "raw": "combined-candidate.ndjson",
            "completed_at_utc": "2026-08-03T09:02:15Z",
            "working_directory":
                "/private/tmp/oneshotsea-atkin-controlled.LB2f5d/candidate",
            "raw_bytes": 157932,
            "raw_sha256": EXPECTED_RAW["combined-candidate.ndjson"]["sha256"],
            "profiler_binary_bytes": 866144,
            "profiler_binary_sha256":
                "760d8fea87588ef189c8eaeb8cf981e62e5cbd200e0d3d91bf85e918c53a8dc6",
            "static_library_bytes": 23525048,
            "static_library_sha256":
                "45d9bbe9d5ac73fa4bcc06e02acdd0c45819090dbb3442982d4471651e5d90e2",
        },
    }
    require(provenance["runs"] == expected_runs, "run artifact identities")
    return provenance


def raw_aggregate(levels: list[dict[str, Any]]) -> dict[str, int]:
    return {
        "samples": len(levels),
        "exact": sum(row["exact"] for row in levels),
        "atkin": sum(not row["exact"] and
                     row["atkin_projective_order"] is not None for row in levels),
        "unconstrained": sum(not row["exact"] and
                             row["atkin_projective_order"] is None for row in levels),
        "information_microbits": sum(decimal(
            row["information_microbits"], "raw information") for row in levels),
        "evaluation_us": sum(decimal(row["evaluation_us"], "raw evaluation")
                             for row in levels),
        "materializations": sum(decimal(
            row["materialization_count"], "raw materialization count")
                                for row in levels),
        "materialization_us": sum(decimal(
            row["materialization_us"], "raw materialization time")
                                  for row in levels),
        "schoof_attempts": sum(row["schoof_residue"] is not None
                               for row in levels),
        "schoof_validations": sum(row["schoof_control_applicable"]
                                  for row in levels),
        "schoof_us": sum(decimal(row["schoof_us"], "raw Schoof time")
                         for row in levels),
    }


def validate_level(row: dict[str, Any], name: str, index: str, ell: str) -> None:
    label = f"{name}: index {index}, ell {ell}"
    exact_keys(row, LEVEL_KEYS, label)
    require(row["schema"] == LEVEL_SCHEMA, f"{label}: schema")
    require(row["global_index"] == index, f"{label}: global index")
    require(row["ell"] == ell, f"{label}: level order")
    decimal(row["curve_j"], f"{label}: curve j", u64=False)
    require(row["selected_side"] in ("curve", "twist"),
            f"{label}: selected side")
    modulus = decimal(row["trace_prior_modulus"], f"{label}: prior modulus")
    residue = decimal(row["trace_prior_residue"], f"{label}: prior residue")
    require(modulus > 0 and residue < modulus, f"{label}: trace prior bounds")
    ell_value = decimal(row["ell"], f"{label}: ell")
    require(type(row["exact"]) is bool, f"{label}: exact boolean")
    require(type(row["schoof_control_applicable"]) is bool,
            f"{label}: Schoof applicability boolean")
    atkin_count = decimal(row["atkin_residue_count"], f"{label}: Atkin count")
    information = decimal(row["information_microbits"],
                          f"{label}: information")
    decimal(row["evaluation_us"], f"{label}: evaluation")
    materializations = decimal(row["materialization_count"],
                               f"{label}: materialization count")
    materialization_us = decimal(row["materialization_us"],
                                 f"{label}: materialization time")
    require(materializations in (0, 1), f"{label}: materialization bound")
    require((materializations == 0) == (materialization_us == 0),
            f"{label}: materialization count/time mismatch")
    schoof_us = decimal(row["schoof_us"], f"{label}: Schoof time")
    require(decimal(row["process_peak_rss_bytes"], f"{label}: peak RSS") > 0,
            f"{label}: zero peak RSS")

    if row["exact"]:
        trace = decimal(row["trace_residue"], f"{label}: trace residue")
        require(trace < ell_value, f"{label}: trace residue bound")
        require(row["atkin_projective_order"] is None and atkin_count == 0,
                f"{label}: exact/Atkin structure")
    elif row["atkin_projective_order"] is not None:
        order = decimal(row["atkin_projective_order"],
                        f"{label}: projective order")
        require(row["trace_residue"] is None, f"{label}: Atkin trace residue")
        require(0 < order <= ell_value + 1 and 0 < atkin_count <= ell_value,
                f"{label}: Atkin bounds")
    else:
        require(row["trace_residue"] is None and atkin_count == 0 and
                information == 0, f"{label}: unconstrained structure")

    if ell_value <= 13:
        schoof = decimal(row["schoof_residue"], f"{label}: Schoof residue")
        require(schoof < ell_value and row["schoof_control_applicable"] and
                schoof_us > 0, f"{label}: required Schoof control")
    else:
        require(row["schoof_residue"] is None and
                not row["schoof_control_applicable"] and schoof_us == 0,
                f"{label}: unexpected Schoof control")


def validate_summary(
    summary: dict[str, Any], levels: list[dict[str, Any]],
    curves: list[dict[str, Any]], all_rss: list[int], name: str,
) -> None:
    exact_keys(summary, SUMMARY_KEYS, f"{name}: summary")
    require(summary["schema"] == SUMMARY_SCHEMA, f"{name}: summary schema")
    require({key: summary[key] for key in CONFIG_KEYS} == EXPECTED_CONFIG,
            f"{name}: target/configuration mismatch")
    for key in (
        "seed", "range_start", "count", "threads", "maximum_prime_candidates",
        "maximum_x_candidates_per_surface", "schoof_through", "cache_index_us",
        "cache_residency_budget_bytes", "cached_level_load_count",
        "cached_level_load_us", "cached_context_evictions",
        "final_cached_retained_contexts", "final_cached_retained_payload_bytes",
        "process_peak_rss_bytes", "generation_us", "elapsed_us",
    ):
        decimal(summary[key], f"{name}: summary {key}")
    decimal(summary["prime"], f"{name}: prime", u64=False)
    require(type(summary["require_point_four"]) is bool,
            f"{name}: require-point-four boolean")
    require(re.fullmatch(r"[0-9a-f]{64}", summary["cache_sha256"]) is not None,
            f"{name}: cache digest syntax")

    require(len(summary["levels"]) == len(LEVELS),
            f"{name}: summary level count")
    for level_index, ell in enumerate(LEVELS):
        aggregate = summary["levels"][level_index]
        exact_keys(aggregate, SUMMARY_LEVEL_KEYS,
                   f"{name}: summary ell {ell}")
        require(aggregate["ell"] == ell, f"{name}: summary level order")
        raw = [row for row in levels if row["ell"] == ell]
        derived = raw_aggregate(raw)
        retained = {key: decimal(aggregate[key], f"{name}: {ell} {key}")
                    for key in derived}
        require(retained == derived,
                f"{name}: raw-to-summary aggregate mismatch at ell {ell}: "
                f"retained={retained}, derived={derived}")

    total_materializations = sum(decimal(
        row["materialization_count"], f"{name}: materialization count")
        for row in levels)
    total_materialization_us = sum(decimal(
        row["materialization_us"], f"{name}: materialization time")
        for row in levels)
    require(decimal(summary["cached_level_load_count"],
                    f"{name}: cached loads") == total_materializations,
            f"{name}: cached load total")
    require(decimal(summary["cached_level_load_us"],
                    f"{name}: cached load time") == total_materialization_us,
            f"{name}: cached load time total")
    loads = decimal(summary["cached_level_load_count"], f"{name}: loads")
    evictions = decimal(summary["cached_context_evictions"],
                        f"{name}: evictions")
    retained = decimal(summary["final_cached_retained_contexts"],
                       f"{name}: retained contexts")
    require(loads >= evictions and retained == loads - evictions,
            f"{name}: cache retention accounting")
    payload = decimal(summary["final_cached_retained_payload_bytes"],
                      f"{name}: retained payload")
    budget = decimal(summary["cache_residency_budget_bytes"],
                     f"{name}: cache budget")
    require(0 < payload <= budget, f"{name}: retained payload bound")

    generation = sum(decimal(curve["generation_us"],
                             f"{name}: curve generation") for curve in curves)
    profile = sum(decimal(curve["profile_us"],
                          f"{name}: curve profile") for curve in curves)
    require(decimal(summary["generation_us"], f"{name}: generation total") ==
            generation, f"{name}: generation total")
    require(decimal(summary["elapsed_us"], f"{name}: elapsed") >=
            generation + profile, f"{name}: elapsed lower bound")
    require(decimal(summary["process_peak_rss_bytes"], f"{name}: peak RSS") ==
            max(all_rss), f"{name}: peak RSS total")
    for order_key in (
            "warm_information_per_cost_order",
            "observed_information_per_cost_order"):
        order = summary[order_key]
        require(len(order) == len(LEVELS) and set(order) == set(LEVELS),
                f"{name}: {order_key} is not a level permutation")
    require(isinstance(summary["claim_scope"], str) and summary["claim_scope"],
            f"{name}: empty claim scope")


def load_run(path: Path, name: str, retained: bool) -> dict[str, Any]:
    raw = path.read_bytes()
    if retained:
        expected = EXPECTED_RAW[name]
        require(len(raw) == expected["bytes"], f"{name}: byte length")
        require(sha256(path) == expected["sha256"], f"{name}: raw digest")
    rows: list[dict[str, Any]] = []
    for line_number, line in enumerate(raw.splitlines(), 1):
        try:
            row = json.loads(line)
        except json.JSONDecodeError as error:
            raise AuditError(f"{name}:{line_number}: invalid JSON: {error}") from error
        require(isinstance(row, dict), f"{name}:{line_number}: non-object row")
        rows.append(row)
    require(len(rows) == len(INDICES) * (len(LEVELS) + 1) + 1,
            f"{name}: row count")

    level_rows: list[dict[str, Any]] = []
    curve_rows: list[dict[str, Any]] = []
    all_rss: list[int] = []
    cursor = 0
    seen_pairs: set[tuple[str, str]] = set()
    for index in INDICES:
        curve_levels: list[dict[str, Any]] = []
        for ell in LEVELS:
            row = rows[cursor]
            cursor += 1
            validate_level(row, name, index, ell)
            pair = (row["global_index"], row["ell"])
            require(pair not in seen_pairs, f"{name}: duplicate grid cell {pair}")
            seen_pairs.add(pair)
            curve_levels.append(row)
            level_rows.append(row)
            all_rss.append(decimal(row["process_peak_rss_bytes"],
                                   f"{name}: level RSS"))
        require(len({row["curve_j"] for row in curve_levels}) == 1,
                f"{name}: curve j changed within index {index}")
        require(len({row["selected_side"] for row in curve_levels}) == 1,
                f"{name}: selected side changed within index {index}")
        require(len({(row["trace_prior_modulus"], row["trace_prior_residue"])
                     for row in curve_levels}) == 1,
                f"{name}: trace prior changed within index {index}")

        curve = rows[cursor]
        cursor += 1
        exact_keys(curve, CURVE_KEYS, f"{name}: curve {index}")
        require(curve["schema"] == CURVE_SCHEMA, f"{name}: curve schema")
        require(curve["global_index"] == index, f"{name}: curve index")
        require(decimal(curve["levels"], f"{name}: curve level count") ==
                len(LEVELS), f"{name}: curve level count")
        sums = {
            "evaluation_us": sum(decimal(row["evaluation_us"],
                                         f"{name}: level evaluation")
                                 for row in curve_levels),
            "materialization_us": sum(decimal(row["materialization_us"],
                                              f"{name}: level materialization")
                                      for row in curve_levels),
            "schoof_us": sum(decimal(row["schoof_us"],
                                     f"{name}: level Schoof")
                             for row in curve_levels),
        }
        for field, expected in sums.items():
            require(decimal(curve[field], f"{name}: curve {field}") == expected,
                    f"{name}: curve {index} {field} total")
        generation = decimal(curve["generation_us"],
                             f"{name}: curve generation")
        profile = decimal(curve["profile_us"], f"{name}: curve profile")
        require(generation > 0 and profile >= sum(sums.values()),
                f"{name}: curve timing bounds")
        curve_rss = decimal(curve["process_peak_rss_bytes"],
                            f"{name}: curve RSS")
        require(curve_rss >= all_rss[-1], f"{name}: curve RSS moved backwards")
        all_rss.append(curve_rss)
        curve_rows.append(curve)

    require(len(seen_pairs) == len(INDICES) * len(LEVELS),
            f"{name}: incomplete unique grid")
    summary = rows[cursor]
    cursor += 1
    require(cursor == len(rows), f"{name}: trailing rows")
    all_rss.append(decimal(summary["process_peak_rss_bytes"],
                           f"{name}: summary RSS"))
    require(all(left <= right for left, right in zip(all_rss, all_rss[1:])),
            f"{name}: peak RSS telemetry moved backwards")
    validate_summary(summary, level_rows, curve_rows, all_rss, name)
    return {
        "levels": level_rows,
        "curves": curve_rows,
        "summary": summary,
        "raw_bytes": len(raw),
        "raw_sha256": sha256(path),
    }


def semantic_projection(levels: list[dict[str, Any]]) -> list[tuple[Any, ...]]:
    return [tuple(row[key] for key in SEMANTIC_LEVEL_KEYS) for row in levels]


def summary_total(summary: dict[str, Any], field: str) -> int:
    return sum(decimal(level[field], f"summary total {field}")
               for level in summary["levels"])


def compare_runs(baseline: dict[str, Any], candidate: dict[str, Any]) -> None:
    require({key: baseline["summary"][key] for key in CONFIG_KEYS} ==
            {key: candidate["summary"][key] for key in CONFIG_KEYS},
            "baseline/candidate configuration differs")
    require(semantic_projection(baseline["levels"]) ==
            semantic_projection(candidate["levels"]),
            "candidate changed a mathematical/control level result")


def derived_result(
    baseline: dict[str, Any], candidate: dict[str, Any],
) -> dict[str, Any]:
    baseline_summary = baseline["summary"]
    candidate_summary = candidate["summary"]
    baseline_evaluation = summary_total(baseline_summary, "evaluation_us")
    candidate_evaluation = summary_total(candidate_summary, "evaluation_us")
    baseline_rss = decimal(baseline_summary["process_peak_rss_bytes"],
                           "baseline result RSS")
    candidate_rss = decimal(candidate_summary["process_peak_rss_bytes"],
                            "candidate result RSS")
    baseline_elapsed = decimal(baseline_summary["elapsed_us"],
                               "baseline result elapsed")
    candidate_elapsed = decimal(candidate_summary["elapsed_us"],
                                "candidate result elapsed")
    schoof_attempts = summary_total(candidate_summary, "schoof_attempts")
    schoof_validations = summary_total(candidate_summary, "schoof_validations")
    unconstrained = summary_total(candidate_summary, "unconstrained")
    target = {
        "prime": candidate_summary["prime"],
        "seed": candidate_summary["seed"],
        "range_start": candidate_summary["range_start"],
        "count": candidate_summary["count"],
        "threads": candidate_summary["threads"],
        "require_point_four": candidate_summary["require_point_four"],
        "levels": [level["ell"] for level in candidate_summary["levels"]],
        "schoof_through": candidate_summary["schoof_through"],
        "maximum_prime_candidates":
            candidate_summary["maximum_prime_candidates"],
        "maximum_x_candidates_per_surface":
            candidate_summary["maximum_x_candidates_per_surface"],
        "cache_sha256": candidate_summary["cache_sha256"],
        "cache_residency_budget_bytes":
            candidate_summary["cache_residency_budget_bytes"],
    }
    grid = {(row["global_index"], row["ell"])
            for row in candidate["levels"]}
    return {
        "schema": RESULT_SCHEMA,
        "target": target,
        "validation": {
            "level_records": len(candidate["levels"]),
            "curve_records": len(candidate["curves"]),
            "unique_ordered_grid": len(grid) == len(candidate["levels"]),
            "semantic_level_records_equal": True,
            "raw_summary_aggregates_equal": True,
            "independent_schoof_attempts": schoof_attempts,
            "independent_schoof_validations": schoof_validations,
            "unconstrained_levels": unconstrained,
        },
        "baseline": {
            "commit": BASELINE_COMMIT,
            "tree": BASELINE_TREE,
            "raw": "baseline.ndjson",
            "raw_sha256": baseline["raw_sha256"],
            "raw_bytes": baseline["raw_bytes"],
            "evaluation_us": baseline_evaluation,
            "process_peak_rss_bytes": baseline_rss,
            "elapsed_us": baseline_elapsed,
        },
        "combined_candidate": {
            "commit": CANDIDATE_COMMIT,
            "tree": CANDIDATE_TREE,
            "raw": "combined-candidate.ndjson",
            "raw_sha256": candidate["raw_sha256"],
            "raw_bytes": candidate["raw_bytes"],
            "components": [
                "factored CRT meet-in-the-middle",
                "uniform factor-degree certificate",
                "baby-step/giant-step modular composition",
            ],
            "evaluation_us": candidate_evaluation,
            "process_peak_rss_bytes": candidate_rss,
            "elapsed_us": candidate_elapsed,
        },
        "comparison": {
            "evaluation_speedup": baseline_evaluation / candidate_evaluation,
            "peak_rss_reduction_ratio": baseline_rss / candidate_rss,
            "whole_profile_speedup": baseline_elapsed / candidate_elapsed,
        },
        "claim_scope": (
            "Fixed local p125 cohort evidence for the complete candidate commit "
            "relative to its parent. The result is not a per-component, "
            "certificate-yield, or unbounded asymptotic claim."
        ),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path,
                        help="reproduction baseline NDJSON")
    parser.add_argument("--candidate", type=Path,
                        help="reproduction candidate NDJSON")
    args = parser.parse_args()
    require((args.baseline is None) == (args.candidate is None),
            "--baseline and --candidate must be supplied together")
    return args


def main() -> None:
    args = parse_args()
    retained = args.baseline is None
    if retained:
        audit_manifest()
        audit_provenance()
        baseline_path = BUNDLE / "baseline.ndjson"
        candidate_path = BUNDLE / "combined-candidate.ndjson"
        baseline_name = baseline_path.name
        candidate_name = candidate_path.name
    else:
        baseline_path = args.baseline.resolve()
        candidate_path = args.candidate.resolve()
        baseline_name = str(baseline_path)
        candidate_name = str(candidate_path)

    baseline = load_run(baseline_path, baseline_name, retained)
    candidate = load_run(candidate_path, candidate_name, retained)
    compare_runs(baseline, candidate)
    result = derived_result(baseline, candidate)
    require(result["validation"]["independent_schoof_attempts"] == 64,
            "Schoof attempt count")
    require(result["validation"]["independent_schoof_validations"] == 64,
            "Schoof validation count")
    require(result["validation"]["unconstrained_levels"] == 0,
            "unconstrained direct levels")
    if retained:
        retained_result = json.loads((BUNDLE / "result.json").read_text("utf-8"))
        require(retained_result == result,
                f"result.json is not raw-derived: retained={retained_result}, "
                f"derived={result}")
    comparison = result["comparison"]
    print(
        "p125 direct combined-patch audit: ok "
        f"(evaluation {comparison['evaluation_speedup']:.2f}x, "
        f"peak RSS {comparison['peak_rss_reduction_ratio']:.2f}x, "
        f"whole profile {comparison['whole_profile_speedup']:.2f}x)"
    )


if __name__ == "__main__":
    try:
        main()
    except AuditError as error:
        raise SystemExit(f"p125 direct combined-patch audit failed: {error}") from error
