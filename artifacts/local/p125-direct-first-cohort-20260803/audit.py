#!/usr/bin/env python3
"""Authenticate and rederive the retained p125 direct-first cohort claims."""

import hashlib
import json
import math
import re
import subprocess
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent
REPOSITORY = ROOT.parents[2]
PRIME = int(
    "10000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000237"
)
INDICES = [2000001, 2000002, 2000003, 2000004]
PARI_TRACES = {
    2000001: -498621923547174620050105080065695461058932825132695425058035790,
    2000002: 312744557074493258005540218670034986285435355679693042023392238,
    2000003: -252845884365417830567895303231394093497235790636298489485509474,
    2000004: 432966650303160993124127306120296021107647349914430129038843294,
}
EXPECTED_FILES = {
    "README.md", "audit.py", "cache-builds.json", "commands.sh",
    "point_count.gp", "print_curves.cpp", "result.json",
    "verify_candidates.cpp",
    "raw/generated-curves.txt", "raw/hybrid-cap16.checkpoint.json",
    "raw/hybrid-cap16.progress.ndjson", "raw/hybrid.checkpoint.json",
    "raw/hybrid.progress.ndjson", "raw/low15-profile-summary.json",
    "raw/mid8-profile.ndjson", "raw/paired-a-low.checkpoint.json",
    "raw/paired-a-low.progress.ndjson",
    "raw/paired-b-selected.checkpoint.json",
    "raw/paired-b-selected.progress.ndjson", "raw/pari-point-counts.txt",
    "raw/selected20-candidates.txt",
    "raw/selected20-cap16.checkpoint.json",
    "raw/selected20-cap16.progress.ndjson",
    "raw/weber-cap16.checkpoint.json",
    "raw/weber-cap16.progress.ndjson", "raw/weber.checkpoint.json",
    "raw/weber.progress.ndjson",
}


class AuditError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise AuditError(message)


def equal(actual, expected, label):
    require(actual == expected,
            f"{label}: expected {expected!r}, observed {actual!r}")


def close(actual, expected, label, rel_tol=1e-12):
    require(math.isclose(float(actual), float(expected), rel_tol=rel_tol),
            f"{label}: expected {expected!r}, observed {actual!r}")


def load_json(relative):
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


def load_rows(relative):
    return [json.loads(line) for line in
            (ROOT / relative).read_text(encoding="utf-8").splitlines()
            if line.strip()]


def key_values(line, label):
    values = {}
    for field in line.split():
        require("=" in field, f"{label}: malformed field {field!r}")
        key, value = field.split("=", 1)
        require(key and key not in values,
                f"{label}: empty or duplicate key {key!r}")
        values[key] = value
    return values


def authenticate():
    entries = {}
    for number, line in enumerate(
            (ROOT / "SHA256SUMS").read_text(encoding="utf-8").splitlines(), 1):
        fields = line.split("  ", 1)
        equal(len(fields), 2, f"SHA256SUMS line {number} format")
        digest, relative = fields
        require(re.fullmatch(r"[0-9a-f]{64}", digest) is not None,
                f"SHA256SUMS line {number}: invalid digest")
        path = PurePosixPath(relative)
        require(not path.is_absolute() and ".." not in path.parts,
                f"SHA256SUMS line {number}: unsafe path")
        require(relative not in entries,
                f"SHA256SUMS line {number}: duplicate path")
        entries[relative] = digest
    equal(set(entries), EXPECTED_FILES, "manifest file set")
    actual = {
        path.relative_to(ROOT).as_posix()
        for path in ROOT.rglob("*")
        if path.is_file() and path.name != "SHA256SUMS" and
        "__pycache__" not in path.parts
    }
    equal(actual, EXPECTED_FILES, "artifact file set")
    for relative, digest in entries.items():
        observed = hashlib.sha256((ROOT / relative).read_bytes()).hexdigest()
        equal(observed, digest, f"SHA-256 {relative}")


def validate_source(result):
    commit = result["implementation_commit"]
    equal(commit, "8a9a08317eefb5e27246c5decea27adc8e1c2962",
          "implementation commit")
    completed = subprocess.run(
        ["git", "rev-parse", f"{commit}^{{tree}}"], cwd=REPOSITORY,
        check=False, capture_output=True, text=True)
    require(completed.returncode == 0,
            f"cannot resolve implementation commit: {completed.stderr.strip()}")
    equal(completed.stdout.strip(),
          "3fa9ca8dee861c561f629dda48f4aafaf750e3c8",
          "implementation source tree")


def validate_checkpoint(progress_name, checkpoint_name):
    rows = load_rows(f"raw/{progress_name}")
    checkpoint = load_json(f"raw/{checkpoint_name}")
    state = rows[-1]["state"]
    for key in ("prime", "seed", "worker_id", "worker_count", "range_start",
                "range_end", "schedule_sha256", "table_manifest_sha256",
                "build_id", "next_index"):
        equal(checkpoint[key], state[key], f"{progress_name} checkpoint {key}")
    equal(checkpoint["schema_version"], 1,
          f"{progress_name} checkpoint schema")
    require(re.fullmatch(r"[0-9a-f]{16}", checkpoint["crc64_ecma"])
            is not None, f"{progress_name} checkpoint CRC")
    return rows


def validate_rows(rows, statuses, levels, candidates, direct_levels=None,
                  complete=True, next_index="2000005"):
    equal([int(row["index"]) for row in rows], INDICES[:len(rows)],
          "cohort indices")
    equal([row["status"] for row in rows], statuses, "cohort statuses")
    equal([int(row["sea_levels"]) for row in rows], levels,
          "cohort Weber levels")
    equal([int(row["final_trace_candidates"]) for row in rows], candidates,
          "cohort effective candidates")
    for row in rows:
        equal(row["heuristic"], False, "cohort heuristic flag")
        if row["status"] == "sound_smoothness_reject":
            equal(row["outcome_class"], "sound_rejection", "sound outcome")
            equal(row["sound_early_abort"], True, "sound-rejection flag")
            equal(row["reached_smoothness"], True, "smoothness stage")
        else:
            equal(row["status"], "sea_level_limit", "level-limit status")
            equal(row["outcome_class"], "implementation_level_limit",
                  "level-limit outcome")
            equal(row["sound_early_abort"], False, "level-limit sound flag")
            equal(row["reached_smoothness"], False,
                  "level-limit smoothness stage")
    if direct_levels is not None:
        equal([int(row["classical_direct_level_count"]) for row in rows],
              [direct_levels] * len(rows), "cohort direct levels")
    equal(rows[-1]["state"]["complete"], complete, "cohort completion")
    equal(rows[-1]["state"]["next_index"], next_index, "cohort cursor")


def validate_search_runs(result):
    sound = ["sound_smoothness_reject"] * 4
    weber = validate_checkpoint("weber.progress.ndjson",
                                "weber.checkpoint.json")
    hybrid = validate_checkpoint("hybrid.progress.ndjson",
                                 "hybrid.checkpoint.json")
    weber16 = validate_checkpoint("weber-cap16.progress.ndjson",
                                  "weber-cap16.checkpoint.json")
    hybrid16 = validate_checkpoint("hybrid-cap16.progress.ndjson",
                                   "hybrid-cap16.checkpoint.json")
    selected16 = validate_checkpoint("selected20-cap16.progress.ndjson",
                                     "selected20-cap16.checkpoint.json")
    low_pair = validate_checkpoint("paired-a-low.progress.ndjson",
                                   "paired-a-low.checkpoint.json")
    selected_pair = validate_checkpoint(
        "paired-b-selected.progress.ndjson",
        "paired-b-selected.checkpoint.json")

    validate_rows(weber, sound, [55, 56, 58, 76], [1, 41, 49, 64])
    validate_rows(hybrid, sound, [38, 38, 42, 58], [6, 23, 1, 2], 15)
    validate_rows(weber16, sound[:3] + ["sea_level_limit"],
                  [55, 57, 59, 77], [1, 1, 1, 64],
                  complete=False, next_index="2000004")
    validate_rows(hybrid16, sound, [38, 39, 42, 58], [6, 1, 1, 2], 15)
    validate_rows(selected16, sound, [33, 33, 35, 52], [1, 13, 4, 5], 20)

    cap64 = result["cap64_matched_cohort"]
    weber_sea = sum(int(row["timings_us"]["sea"]) for row in weber)
    hybrid_sea = sum(int(row["timings_us"]["sea"]) for row in hybrid)
    equal(weber_sea, cap64["weber_first"]["sea_us"], "cap64 Weber SEA sum")
    equal(hybrid_sea, cap64["low15_direct_first"]["sea_us"],
          "cap64 hybrid SEA sum")
    equal(sum(int(row["timings_us"]["total"]) for row in weber),
          cap64["weber_first"]["total_us"], "cap64 Weber total sum")
    equal(sum(int(row["timings_us"]["total"]) for row in hybrid),
          cap64["low15_direct_first"]["total_us"],
          "cap64 hybrid total sum")
    equal(sum(int(row["sea_levels"]) for row in weber), 245,
          "cap64 Weber levels")
    equal(sum(int(row["sea_levels"]) for row in hybrid), 176,
          "cap64 hybrid Weber levels")
    equal(sum(int(row["classical_direct_level_count"]) for row in hybrid),
          60, "cap64 direct levels")
    equal(sum(int(row["timings_us"]["direct_first"]) for row in hybrid),
          4615368, "cap64 direct time")
    close(weber_sea / hybrid_sea, cap64["sea_speedup"],
          "cap64 SEA speedup")
    close(100 * (weber_sea - hybrid_sea) / weber_sea,
          cap64["sea_reduction_percent"], "cap64 SEA reduction")
    equal(cap64["total_time_claimed"], False, "cap64 total-time nonclaim")

    coverage = result["cap16_coverage"]
    equal(sum(coverage["low15_direct_first"]["weber_levels"]) -
          sum(coverage["selected20_direct_first"]["weber_levels"]), 24,
          "selected schedule Weber savings")
    equal(24 - 4 * (20 - 15), 4, "selected schedule net level savings")

    equal(len(low_pair), 1, "paired low record count")
    equal(len(selected_pair), 1, "paired selected record count")
    equal(int(low_pair[0]["index"]), 2000003, "paired low index")
    equal(int(selected_pair[0]["index"]), 2000003, "paired selected index")
    paired = result["paired_thermal_control_index_2000003"]
    selected_a = selected16[2]
    projections = (("selected_a", selected_a), ("low", low_pair[0]),
                   ("selected_b", selected_pair[0]))
    for label, row in projections:
        observed = paired[label]
        equal(int(row["timings_us"]["generation"]),
              observed["generation_us"], f"paired {label} generation")
        equal(int(row["timings_us"]["direct_first"]),
              observed["direct_us"], f"paired {label} direct")
        equal(int(row["timings_us"]["direct_first_fallback"]),
              observed["weber_continuation_us"],
              f"paired {label} Weber continuation")
        equal(int(row["timings_us"]["sea"]), observed["sea_us"],
              f"paired {label} SEA")
    generations = [paired[label]["generation_us"]
                   for label in ("selected_a", "low", "selected_b")]
    spread = 100 * (max(generations) - min(generations)) / min(generations)
    close(spread, paired["maximum_generation_spread_percent"],
          "paired generation spread", rel_tol=1e-7)
    selected_mean = (paired["selected_a"]["sea_us"] +
                     paired["selected_b"]["sea_us"]) / 2
    equal(selected_mean, paired["mean_selected_sea_us"],
          "paired selected mean")
    close(paired["low"]["sea_us"] / selected_mean,
          paired["low_over_mean_selected_sea_speedup"],
          "paired speedup", rel_tol=1e-7)


def validate_curves_pari_and_candidates(result):
    generated = {}
    for number, line in enumerate((ROOT / "raw/generated-curves.txt")
                                  .read_text().splitlines(), 1):
        values = key_values(line, f"generated curves line {number}")
        index = int(values["index"])
        generated[index] = values
        a, b, j = int(values["a"]), int(values["b"]), int(values["j"])
        require(0 <= a < PRIME and 0 <= b < PRIME,
                f"curve {index}: noncanonical coefficients")
        denominator = (4 * pow(a, 3, PRIME) + 27 * pow(b, 2, PRIME)) % PRIME
        require(denominator != 0, f"curve {index}: singular model")
        computed_j = (1728 * 4 * pow(a, 3, PRIME) *
                      pow(denominator, -1, PRIME)) % PRIME
        equal(j, computed_j, f"curve {index} j-invariant")
        equal(int(values["prior_modulus"]), 432,
              f"curve {index} prior modulus")
    equal(sorted(generated), INDICES, "generated curve indices")
    priors = {index: int(generated[index]["prior_residue"])
              for index in INDICES}
    equal(priors, {2000001: 418, 2000002: 14, 2000003: 14, 2000004: 14},
          "generated trace priors")

    pari = {}
    for number, line in enumerate((ROOT / "raw/pari-point-counts.txt")
                                  .read_text().splitlines(), 1):
        values = key_values(line, f"PARI line {number}")
        index = int(values["index"])
        order, trace = int(values["order"]), int(values["trace"])
        equal(order, PRIME + 1 - trace, f"PARI order relation {index}")
        equal(trace, PARI_TRACES[index], f"PARI trace {index}")
        equal(trace % 432, priors[index], f"PARI trace prior {index}")
        require(int(values["elapsed_ms"]) > 0,
                f"PARI elapsed time {index}")
        pari[index] = trace
    equal(sorted(pari), INDICES, "PARI indices")

    candidates = {}
    radius = math.isqrt(4 * PRIME)
    for number, line in enumerate((ROOT / "raw/selected20-candidates.txt")
                                  .read_text().splitlines(), 1):
        values = key_values(line, f"candidate line {number}")
        index = int(values["index"])
        traces = [int(value) for value in values["traces"].split(",")]
        equal(len(traces), int(values["count"]),
              f"candidate count {index}")
        equal(traces, sorted(set(traces)), f"candidate ordering {index}")
        require(all(abs(trace) <= radius for trace in traces),
                f"candidate Hasse bound {index}")
        require(all(trace % 432 == priors[index] for trace in traces),
                f"candidate trace prior {index}")
        require(pari[index] in traces, f"PARI trace absent at index {index}")
        candidates[index] = traces
    equal(sorted(candidates), INDICES, "candidate indices")
    equal([len(candidates[index]) for index in INDICES], [1, 13, 4, 5],
          "candidate-set sizes")
    independent = result["independent_pari"]
    equal({int(key): int(value) for key, value in
           independent["signed_traces"].items()}, PARI_TRACES,
          "result PARI traces")
    equal(independent["all_traces_present"], True,
          "result PARI inclusion")
    equal(independent["production_dependency"], False,
          "PARI production dependency")

    source = (ROOT / "point_count.gp").read_text(encoding="utf-8")
    for index in INDICES:
        require(generated[index]["a"] in source and
                generated[index]["b"] in source and
                str(PARI_TRACES[index]) in source,
                f"PARI replay input missing curve {index}")


def validate_profiles_and_schedule(result):
    mid_rows = load_rows("raw/mid8-profile.ndjson")
    equal(len(mid_rows), 145, "mid8 profile record count")
    mid = mid_rows[-1]
    low = load_json("raw/low15-profile-summary.json")
    equal(mid["schema"], "oneshotsea.classical-direct-cohort-summary.v1",
          "mid8 summary schema")
    equal(low["schema"], "oneshotsea.classical-direct-cohort-summary.v1",
          "low15 summary schema")
    mid_levels = {int(level["ell"]): level for level in mid["levels"]}
    low_levels = {int(level["ell"]): level for level in low["levels"]}
    equal(sorted(mid_levels), [61, 67, 71, 73, 79, 83, 89, 97],
          "mid8 levels")
    equal(int(mid["count"]), 16, "mid8 curve count")
    equal(int(low["count"]), 16, "low15 curve count")
    equal(mid["cache_sha256"],
          result["authenticated_inputs"]["mid8_cache_sha256"],
          "mid8 cache digest")
    reference = low_levels[59]
    selected = [67, 71, 79, 61, 73]
    excluded = [83, 89, 97]
    for ell in selected:
        require(int(mid_levels[ell]["information_microbits"]) *
                int(reference["evaluation_us"]) >
                int(reference["information_microbits"]) *
                int(mid_levels[ell]["evaluation_us"]),
                f"selected level {ell} does not beat level 59")
    for ell in excluded:
        require(int(mid_levels[ell]["information_microbits"]) *
                int(reference["evaluation_us"]) <
                int(reference["information_microbits"]) *
                int(mid_levels[ell]["evaluation_us"]),
                f"excluded level {ell} unexpectedly beats level 59")
    information_bits = sum(
        int(mid_levels[ell]["information_microbits"])
        for ell in selected) / 16 / 1_000_000
    evaluation_us = sum(int(mid_levels[ell]["evaluation_us"])
                        for ell in selected) / 16
    schedule = result["selected_schedule"]
    equal(schedule["added_mid_levels"], selected, "selected mid levels")
    equal(schedule["excluded_mid_levels"], excluded, "excluded mid levels")
    close(information_bits, schedule["added_expected_information_bits_per_curve"],
          "selected expected information")
    equal(evaluation_us, schedule["added_warm_evaluation_us_per_curve"],
          "selected evaluation cost")
    equal(schedule["levels"],
          [7, 5, 11, 13, 19, 17, 23, 29, 31, 37,
           41, 43, 47, 53, 67, 71, 79, 61, 73, 59],
          "selected ordered schedule")

    builds = load_json("cache-builds.json")
    for label, key in (("mid8", "mid8_cache_sha256"),
                       ("selected20", "selected20_cache_sha256")):
        equal(builds[label]["sha256"], result["authenticated_inputs"][key],
              f"{label} build digest")
        require(builds[label]["file_bytes"] <=
                builds[label]["max_file_bytes"],
                f"{label} cache exceeds retained cap")
    equal(builds["selected20"]["file_bytes"], schedule["cache_bytes"],
          "selected cache bytes")
    equal(builds["selected20"]["matrix_payload_bytes"],
          schedule["matrix_payload_bytes"], "selected matrix bytes")


def validate_readme():
    text = (ROOT / "README.md").read_text(encoding="utf-8")
    flattened = " ".join(text.split())
    require("The answer is yes to all three on this bounded cohort." in text,
            "artifact README omits bounded conclusion")
    require("does not measure certificate yield" in flattened,
            "artifact README omits certificate-yield nonclaim")
    root = (REPOSITORY / "README.md").read_text(encoding="utf-8")
    require("p125-direct-first-cohort-20260803/README.md" in root,
            "root README does not link this evidence")
    require("being finalized" not in root,
            "root README retains stale provisional language")


def main():
    authenticate()
    result = load_json("result.json")
    equal(result["schema"], "oneshotsea.p125-direct-first-cohort.v1",
          "result schema")
    equal(int(result["target"]["prime"]), PRIME, "result prime")
    equal(result["target"]["bits"], PRIME.bit_length(), "result bit length")
    validate_source(result)
    validate_search_runs(result)
    validate_curves_pari_and_candidates(result)
    validate_profiles_and_schedule(result)
    validate_readme()
    print("PASS authenticated p125 direct-first cohort")
    print("PASS Weber-only cap-16 level limit fails closed")
    print("PASS selected-20 completes four sound bounded screenings")
    print("PASS exact PARI traces lie in every retained candidate set")
    print(f"PASS cap-64 aggregate SEA speedup {result['cap64_matched_cohort']['sea_speedup']:.6f}x")
    print(f"PASS paired selected-20 SEA speedup {result['paired_thermal_control_index_2000003']['low_over_mean_selected_sea_speedup']:.6f}x")


if __name__ == "__main__":
    try:
        main()
    except (AuditError, KeyError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit(f"FAIL {error}")
