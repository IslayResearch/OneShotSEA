#!/usr/bin/env python3
import hashlib
import json
import math
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent
REPOSITORY = ROOT.parents[2]

EXPECTED_COMMIT = "8a9a08317eefb5e27246c5decea27adc8e1c2962"
EXPECTED_TREE = "3fa9ca8dee861c561f629dda48f4aafaf750e3c8"
EXPECTED_SEARCH_BINARY_SHA256 = (
    "aba34a1186b5b09a7649da2ab2f4b5af0e3439f6f19730b059d38efa76d14279"
)
EXPECTED_PARI_SHA256 = (
    "cb8372f5159eb8c42f823479bcb0d4e096e149751a6cf59421d1dbf03f9d9dee"
)
EXPECTED_PRIME = int(
    "100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237"
)
EXPECTED_SEED = 202607300000
EXPECTED_INDEX = 2000000
EXPECTED_A = int(
    "71767066679186603923921770935567842539817966722958413189905128110444348984023454005578926582733725886104879149565268081093352"
)
EXPECTED_B = int(
    "14511377786124402615947847290378561693211977815305608793270085406962899322682302670385951055155817257403252766376845387395489"
)
EXPECTED_TRACE_PRIOR = (432, 14)
EXPECTED_SMOOTH_SHA256 = (
    "afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551"
)
EXPECTED_WEBER_MANIFEST_SHA256 = (
    "ac1fb3eafd991bccae2fcc05572108f318522b15fd6a3a164b8665c16f2d6bd5"
)
EXPECTED_DIRECT_SHA256 = (
    "b31c858c5398d7b284ad7b003ce4647211de8dec0412421f90ed226f2926ecfd"
)
EXPECTED_DIRECT_LEVELS = [5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43,
                          47, 53, 59]
EXPECTED_BASELINE_SCHEDULE = (
    "b3fe5d37b2537aa93d68c1367327bae5cb694cfdca2c8b4a5713d5ef2244d198"
)
EXPECTED_HYBRID_SCHEDULE = (
    "3eaa5e982c848955287b96f1e76a8c855515a196fb6d0207ddc2e2d1cd60c832"
)
EXPECTED_BUILD_LABEL = "local:4b646cd-hybrid-ab"

MANIFEST_FILES = {
    "README.md",
    "result.json",
    "commands.sh",
    "audit.py",
    "point_count.gp",
    "verifier.cpp",
    "raw/weber.progress.ndjson",
    "raw/hybrid.progress.ndjson",
    "raw/weber.checkpoint.json",
    "raw/hybrid.checkpoint.json",
    "raw/native-table-backed.txt",
    "raw/pari-point-count.txt",
}


class AuditError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise AuditError(message)


def equal(actual, expected, label):
    require(actual == expected,
            f"{label}: expected {expected!r}, observed {actual!r}")


def integer(value, label):
    require(not isinstance(value, bool), f"{label}: boolean is not an integer")
    try:
        return int(value)
    except (TypeError, ValueError) as error:
        raise AuditError(f"{label}: invalid integer {value!r}") from error


def load_json(path):
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def load_single_json_record(path):
    lines = [line for line in path.read_text(encoding="utf-8").splitlines()
             if line.strip()]
    equal(len(lines), 1, f"{path.name} record count")
    return json.loads(lines[0])


def load_key_values(path, expected_keys):
    values = {}
    for number, line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), 1):
        require("=" in line, f"{path.name}:{number}: missing equals sign")
        key, value = line.split("=", 1)
        require(key and key not in values,
                f"{path.name}:{number}: empty or duplicate key {key!r}")
        values[key] = value
    equal(set(values), set(expected_keys), f"{path.name} keys")
    return values


def authenticate():
    entries = {}
    manifest = ROOT / "SHA256SUMS"
    for number, line in enumerate(
            manifest.read_text(encoding="utf-8").splitlines(), 1):
        fields = line.split("  ", 1)
        equal(len(fields), 2, f"SHA256SUMS:{number} format")
        expected, relative = fields
        require(re.fullmatch(r"[0-9a-f]{64}", expected) is not None,
                f"SHA256SUMS:{number}: invalid digest")
        path = PurePosixPath(relative)
        require(not path.is_absolute() and ".." not in path.parts and
                "." not in path.parts,
                f"SHA256SUMS:{number}: unsafe path {relative!r}")
        require(relative not in entries,
                f"SHA256SUMS:{number}: duplicate path {relative!r}")
        entries[relative] = expected

    equal(set(entries), MANIFEST_FILES, "SHA256SUMS file set")
    actual_files = {
        path.relative_to(ROOT).as_posix()
        for path in ROOT.rglob("*")
        if path.is_file() and path.name != "SHA256SUMS" and
        "__pycache__" not in path.parts
    }
    equal(actual_files, MANIFEST_FILES, "artifact file set")
    for relative, expected in entries.items():
        actual = hashlib.sha256((ROOT / relative).read_bytes()).hexdigest()
        equal(actual, expected, f"SHA-256 {relative}")


def validate_git_source(source):
    equal(source["commit"], EXPECTED_COMMIT, "source commit")
    equal(source["tree"], EXPECTED_TREE, "source tree")
    equal(source["search_binary_sha256"], EXPECTED_SEARCH_BINARY_SHA256,
          "search binary digest")
    equal(source["recorded_build_label"], EXPECTED_BUILD_LABEL,
          "recorded build label")
    equal(source["build_label_is_authoritative"], False,
          "build-label authority flag")
    completed = subprocess.run(
        ["git", "rev-parse", f"{EXPECTED_COMMIT}^{{tree}}"],
        cwd=REPOSITORY, check=False, text=True, capture_output=True)
    require(completed.returncode == 0,
            f"cannot resolve retained source commit: {completed.stderr.strip()}")
    equal(completed.stdout.strip(), EXPECTED_TREE,
          "Git commit-to-tree binding")


def validate_target(target):
    equal(integer(target["prime"], "target prime"), EXPECTED_PRIME,
          "target prime")
    equal(target["bits"], EXPECTED_PRIME.bit_length(), "target bit length")
    equal(integer(target["seed"], "target seed"), EXPECTED_SEED,
          "target seed")
    equal(integer(target["index"], "target index"), EXPECTED_INDEX,
          "target index")
    equal(target["curve_family"], "x1-27", "target curve family")
    equal(target["require_point4"], True, "target point-four policy")
    equal(integer(target["curve_a"], "target curve a"), EXPECTED_A,
          "target curve a")
    equal(integer(target["curve_b"], "target curve b"), EXPECTED_B,
          "target curve b")
    equal(integer(target["trace_prior_modulus"], "trace-prior modulus"),
          EXPECTED_TRACE_PRIOR[0], "trace-prior modulus")
    equal(integer(target["trace_prior_residue"], "trace-prior residue"),
          EXPECTED_TRACE_PRIOR[1], "trace-prior residue")
    require(0 <= EXPECTED_A < EXPECTED_PRIME and
            0 <= EXPECTED_B < EXPECTED_PRIME,
            "target coefficients are not canonical")
    discriminant_factor = (
        4 * pow(EXPECTED_A, 3, EXPECTED_PRIME) +
        27 * pow(EXPECTED_B, 2, EXPECTED_PRIME)
    ) % EXPECTED_PRIME
    require(discriminant_factor != 0, "target curve is singular")


def expected_nested_counters(full_point_counts):
    return {
        "curves_attempted": "1",
        "rejections": {
            "invalid_curve": "0",
            "sea": "0",
            "sound_early_abort": "1",
            "heuristic": "0",
            "certificate_assembly": "0",
        },
        "completed_without_certificate": "0",
        "full_point_counts_completed": str(full_point_counts),
        "candidates_reaching_smoothness": "1",
        "certificates_found": "0",
    }


def expected_flat_counters(full_point_counts):
    return {
        "curves_attempted": "1",
        "rejected_invalid_curve": "0",
        "rejected_sea": "0",
        "rejected_sound_early_abort": "1",
        "rejected_heuristic": "0",
        "rejected_certificate_assembly": "0",
        "completed_without_certificate": "0",
        "full_point_counts_completed": str(full_point_counts),
        "candidates_reaching_smoothness": "1",
        "certificates_found": "0",
    }


def validate_progress_and_checkpoint(record, checkpoint, schedule,
                                     full_point_counts, label):
    equal(record["schema"], "oneshotsea.search-curve.v1",
          f"{label} progress schema")
    equal(integer(record["index"], f"{label} index"), EXPECTED_INDEX,
          f"{label} index")
    state = record["state"]
    equal(state["schema"], "oneshotsea.search-progress.v1",
          f"{label} state schema")
    equal(checkpoint["schema_version"], 1,
          f"{label} checkpoint schema")
    identity = {
        "prime": str(EXPECTED_PRIME),
        "seed": str(EXPECTED_SEED),
        "worker_id": "0",
        "worker_count": "1",
        "range_start": str(EXPECTED_INDEX),
        "range_end": str(EXPECTED_INDEX + 1),
        "schedule_sha256": schedule,
        "table_manifest_sha256": EXPECTED_WEBER_MANIFEST_SHA256,
        "build_id": EXPECTED_BUILD_LABEL,
        "next_index": str(EXPECTED_INDEX + 1),
    }
    for key, expected in identity.items():
        equal(state[key], expected, f"{label} state {key}")
        equal(checkpoint[key], expected, f"{label} checkpoint {key}")
    equal(state["complete"], True, f"{label} completion flag")
    equal(state["counters"], expected_nested_counters(full_point_counts),
          f"{label} progress counters")
    equal(checkpoint["counters"], expected_flat_counters(full_point_counts),
          f"{label} checkpoint counters")
    require(re.fullmatch(r"[0-9a-f]{16}", checkpoint["crc64_ecma"])
            is not None, f"{label} checkpoint CRC format")


def validate_common_curve_record(record, label):
    equal(record["status"], "sound_smoothness_reject", f"{label} status")
    equal(record["outcome_class"], "sound_rejection",
          f"{label} outcome class")
    equal(record["sound_early_abort"], True, f"{label} sound rejection")
    equal(record["heuristic"], False, f"{label} heuristic flag")
    equal(record["reached_smoothness"], True,
          f"{label} smoothness stage")
    equal(integer(record["generator_rejections"],
                  f"{label} generator rejections"), 96,
          f"{label} generator rejections")
    equal(record["trace_prior"], {"modulus": "432", "residue": "14"},
          f"{label} trace prior")
    for key in ("candidate_attempts", "candidate_search_nodes",
                "assembly_calls", "canonical_rejections",
                "schoof_fallback_level_count"):
        equal(integer(record[key], f"{label} {key}"), 0,
              f"{label} {key}")
    equal(record["schoof_fallback_levels"], [],
          f"{label} Schoof level records")
    equal(record["sea_level_timings"], [],
          f"{label} disabled SEA telemetry")
    equal(integer(record["sea_passes"], f"{label} SEA passes"), 1,
          f"{label} SEA passes")
    timings = record["timings_us"]
    for key in ("candidate", "assembly", "verifier"):
        equal(integer(timings[key], f"{label} timing {key}"), 0,
              f"{label} timing {key}")


def validate_result_projection(result, baseline, hybrid):
    baseline_result = result["baseline"]
    hybrid_result = result["hybrid"]
    validate_common_curve_record(baseline, "baseline")
    validate_common_curve_record(hybrid, "hybrid")

    equal(baseline_result["strategy"], "weber-first", "baseline strategy")
    require("sea_strategy" not in baseline and "direct_first" not in baseline,
            "baseline unexpectedly records a direct strategy")
    baseline_projection = {
        "status": baseline["status"],
        "outcome_class": baseline["outcome_class"],
        "sound_early_abort": baseline["sound_early_abort"],
        "full_point_count": baseline["full_point_count"],
        "generator_rejections": integer(baseline["generator_rejections"],
                                        "baseline generator rejections"),
        "weber_levels": integer(baseline["sea_levels"], "baseline levels"),
        "exact_weber_levels": integer(baseline["exact_sea_levels"],
                                      "baseline exact levels"),
        "atkin_weber_levels": integer(baseline["atkin_sea_levels"],
                                      "baseline Atkin levels"),
        "initial_trace_candidates": integer(baseline["initial_trace_count"],
                                            "baseline initial traces"),
        "final_exact_trace_candidates": integer(
            baseline["final_exact_trace_candidates"],
            "baseline final exact traces"),
        "final_effective_trace_candidates": integer(
            baseline["final_trace_candidates"],
            "baseline final effective traces"),
        "generation_us": integer(baseline["timings_us"]["generation"],
                                 "baseline generation time"),
        "sea_us": integer(baseline["timings_us"]["sea"],
                          "baseline SEA time"),
        "smoothness_us": integer(baseline["timings_us"]["smoothness"],
                                 "baseline smoothness time"),
        "total_us": integer(baseline["timings_us"]["total"],
                            "baseline total time"),
        "peak_rss_bytes": integer(baseline["peak_rss_bytes"],
                                  "baseline peak RSS"),
    }
    for key, expected in baseline_projection.items():
        equal(baseline_result[key], expected, f"baseline result {key}")

    equal(hybrid["sea_strategy"], "direct-first", "hybrid strategy record")
    equal(hybrid["direct_first"],
          {"attempts": "1", "completions": "0", "fallbacks": "1"},
          "hybrid direct-first counters")
    equal(integer(hybrid["classical_direct_passes"], "direct passes"), 1,
          "hybrid direct passes")
    equal(hybrid["classical_direct_levels"], [],
          "hybrid disabled direct telemetry")
    hybrid_projection = {
        "status": hybrid["status"],
        "outcome_class": hybrid["outcome_class"],
        "sound_early_abort": hybrid["sound_early_abort"],
        "full_point_count": hybrid["full_point_count"],
        "generator_rejections": integer(hybrid["generator_rejections"],
                                        "hybrid generator rejections"),
        "direct_attempts": integer(hybrid["direct_first"]["attempts"],
                                   "hybrid direct attempts"),
        "direct_completions": integer(hybrid["direct_first"]["completions"],
                                      "hybrid direct completions"),
        "weber_continuations": integer(hybrid["direct_first"]["fallbacks"],
                                       "hybrid Weber continuations"),
        "direct_levels": integer(hybrid["classical_direct_level_count"],
                                 "hybrid direct levels"),
        "exact_direct_levels": integer(hybrid["exact_classical_direct_levels"],
                                       "hybrid exact direct levels"),
        "atkin_direct_levels": integer(hybrid["atkin_classical_direct_levels"],
                                       "hybrid Atkin direct levels"),
        "weber_levels": integer(hybrid["sea_levels"], "hybrid Weber levels"),
        "exact_weber_levels": integer(hybrid["exact_sea_levels"],
                                      "hybrid exact Weber levels"),
        "atkin_weber_levels": integer(hybrid["atkin_sea_levels"],
                                      "hybrid Atkin Weber levels"),
        "initial_trace_candidates": integer(hybrid["initial_trace_count"],
                                            "hybrid initial traces"),
        "final_exact_trace_candidates": integer(
            hybrid["final_exact_trace_candidates"],
            "hybrid final exact traces"),
        "final_effective_trace_candidates": integer(
            hybrid["final_trace_candidates"],
            "hybrid final effective traces"),
        "trace": hybrid["trace"],
        "generation_us": integer(hybrid["timings_us"]["generation"],
                                 "hybrid generation time"),
        "sea_us": integer(hybrid["timings_us"]["sea"],
                          "hybrid SEA time"),
        "direct_phase_us": integer(hybrid["timings_us"]["direct_first"],
                                   "hybrid direct time"),
        "weber_continuation_us": integer(
            hybrid["timings_us"]["direct_first_fallback"],
            "hybrid continuation time"),
        "smoothness_us": integer(hybrid["timings_us"]["smoothness"],
                                 "hybrid smoothness time"),
        "total_us": integer(hybrid["timings_us"]["total"],
                            "hybrid total time"),
        "peak_rss_bytes": integer(hybrid["peak_rss_bytes"],
                                  "hybrid peak RSS"),
    }
    for key, expected in hybrid_projection.items():
        equal(hybrid_result[key], expected, f"hybrid result {key}")
    equal(hybrid_result["strategy"],
          "direct-first-retained-weber-continuation",
          "hybrid result strategy")
    equal(hybrid_result["sea_us"],
          hybrid_result["direct_phase_us"] +
          hybrid_result["weber_continuation_us"],
          "hybrid SEA phase sum")


def validate_comparison(result, baseline, hybrid):
    baseline_sea = integer(baseline["timings_us"]["sea"], "baseline SEA")
    hybrid_sea = integer(hybrid["timings_us"]["sea"], "hybrid SEA")
    baseline_total = integer(baseline["timings_us"]["total"],
                             "baseline total")
    hybrid_total = integer(hybrid["timings_us"]["total"], "hybrid total")
    saved = integer(baseline["sea_levels"], "baseline levels") - integer(
        hybrid["sea_levels"], "hybrid levels")
    comparison = result["comparison"]
    equal(comparison["same_target_identity"], True,
          "comparison target identity")
    equal(comparison["same_generator_rejections"],
          baseline["generator_rejections"] == hybrid["generator_rejections"],
          "comparison generator identity")
    equal(comparison["same_sound_rejection"],
          baseline["status"] == hybrid["status"] ==
          "sound_smoothness_reject", "comparison rejection identity")
    equal(comparison["weber_levels_saved"], saved,
          "comparison levels saved")
    expected = {
        "sea_speedup": baseline_sea / hybrid_sea,
        "sea_time_reduction_percent":
            100.0 * (baseline_sea - hybrid_sea) / baseline_sea,
        "total_speedup": baseline_total / hybrid_total,
        "total_time_reduction_percent":
            100.0 * (baseline_total - hybrid_total) / baseline_total,
    }
    for key, value in expected.items():
        require(math.isclose(comparison[key], value,
                             rel_tol=1e-14, abs_tol=1e-14),
                f"comparison {key}: expected {value}, "
                f"observed {comparison[key]}")


def validate_verifier_source(result, target):
    source_path = ROOT / "verifier.cpp"
    source = source_path.read_text(encoding="utf-8")
    native = result["native_table_backed_cap_one"]
    equal(hashlib.sha256(source_path.read_bytes()).hexdigest(),
          native["verifier_source_sha256"], "native verifier source digest")
    patterns = {
        "prime": r'const mpz_class prime\(\s*"([0-9]+)"\s*\);',
        "seed": r'constexpr std::uint64_t seed = UINT64_C\(([0-9]+)\);',
        "index": r'constexpr std::uint64_t index = UINT64_C\(([0-9]+)\);',
        "a": r'const mpz_class expected_a\(\s*"([0-9]+)"\s*\);',
        "b": r'const mpz_class expected_b\(\s*"([0-9]+)"\s*\);',
    }
    parsed = {}
    for key, pattern in patterns.items():
        match = re.search(pattern, source, re.DOTALL)
        require(match is not None, f"native verifier does not pin {key}")
        parsed[key] = integer(match.group(1), f"native verifier {key}")
    equal(parsed["prime"], EXPECTED_PRIME, "native verifier prime")
    equal(parsed["seed"], EXPECTED_SEED, "native verifier seed")
    equal(parsed["index"], EXPECTED_INDEX, "native verifier index")
    equal(parsed["a"], EXPECTED_A, "native verifier curve a")
    equal(parsed["b"], EXPECTED_B, "native verifier curve b")
    compact = re.sub(r"\s+", "", source)
    for fragment in (
            "deterministic_x1_27_search_curve(prime,seed,index,true)",
            "sample.pair.curve.a()!=expected_a",
            "sample.pair.curve.b()!=expected_b",
            'run_weber_sea_reference(sample.pair.curve,"data/modpoly/weber_f",'
            "401U,1U",
            "result.traces->size()!=1U"):
        require(fragment in compact,
                f"native verifier is missing semantic guard {fragment!r}")
    equal(integer(target["curve_a"], "target a"), parsed["a"],
          "native verifier-to-target a binding")
    equal(integer(target["curve_b"], "target b"), parsed["b"],
          "native verifier-to-target b binding")


def validate_native_result(result, target, hybrid_trace):
    native_result = result["native_table_backed_cap_one"]
    native = load_key_values(
        ROOT / "raw/native-table-backed.txt",
        {"schema", "prime", "seed", "index", "a", "b", "trace",
         "exact_candidates", "effective_candidates", "levels",
         "elapsed_us"})
    equal(native["schema"], "oneshotsea.native-table-backed-cap-one.v1",
          "native raw schema")
    expected_numbers = {
        "prime": EXPECTED_PRIME,
        "seed": EXPECTED_SEED,
        "index": EXPECTED_INDEX,
        "a": EXPECTED_A,
        "b": EXPECTED_B,
        "exact_candidates": 1,
        "effective_candidates": 1,
        "levels": 71,
    }
    for key, expected in expected_numbers.items():
        equal(integer(native[key], f"native raw {key}"), expected,
              f"native raw {key}")
    equal(native["trace"], hybrid_trace, "native-to-hybrid trace agreement")
    projection = {
        "trace": native["trace"],
        "exact_trace_candidates": integer(native["exact_candidates"],
                                            "native exact candidates"),
        "effective_trace_candidates": integer(native["effective_candidates"],
                                                "native effective candidates"),
        "weber_levels": integer(native["levels"], "native levels"),
        "elapsed_us": integer(native["elapsed_us"], "native elapsed time"),
    }
    for key, expected in projection.items():
        equal(native_result[key], expected, f"native result {key}")
    equal(native_result["independence_limit"],
          "separate table-backed producer, shared downstream SEA implementation",
          "native independence scope")
    require(re.fullmatch(r"[0-9a-f]{64}",
                         native_result["verifier_binary_sha256"]) is not None,
            "native verifier binary digest format")
    equal(integer(target["prime"], "target prime"),
          integer(native["prime"], "native prime"),
          "native-to-target prime binding")


def parse_gp_script():
    script = (ROOT / "point_count.gp").read_text(encoding="utf-8")
    parsed = {}
    for name in ("p", "a", "b"):
        matches = re.findall(rf"(?m)^\s*{name}\s*=\s*([0-9]+)\s*;\s*$",
                             script)
        equal(len(matches), 1, f"GP assignment count for {name}")
        parsed[name] = integer(matches[0], f"GP {name}")
    compact = re.sub(r"\s+", "", script)
    required = (
        "prime_check=isprime(p);",
        'if(!prime_check,error("thesuppliedmodulusisnotprime"));',
        "E=ellinit([a,b],p);",
        "order=ellcard(E);",
        "trace_value=p+1-order;",
        'print("schema=oneshotsea.pari-point-count.v1");',
        'print("pari_version=",version());',
        'print("prime_check=",prime_check);',
        'print("prime=",p);',
        'print("a=",a);',
        'print("b=",b);',
        'print("order=",order);',
        'print("trace=",trace_value);',
        'print("elapsed_ms=",elapsed_ms);',
    )
    for fragment in required:
        require(fragment in compact,
                f"GP script is missing semantic guard {fragment!r}")
    equal(compact.count("ellcard("), 1, "GP ellcard call count")
    return parsed


def validate_pari_result(result, target, hybrid_trace):
    pari_result = result["independent_pari_point_count"]
    pari = load_key_values(
        ROOT / "raw/pari-point-count.txt",
        {"schema", "pari_version", "prime_check", "prime", "a", "b",
         "order", "trace", "elapsed_ms"})
    equal(pari["schema"], "oneshotsea.pari-point-count.v1",
          "PARI raw schema")
    equal(pari["pari_version"], "[2, 17, 4]", "PARI raw version")
    equal(pari["prime_check"], "1", "PARI raw primality result")
    gp = parse_gp_script()
    expected_curve = {"p": EXPECTED_PRIME, "a": EXPECTED_A,
                      "b": EXPECTED_B}
    equal(gp, expected_curve, "GP script curve")
    for raw_key, script_key in (("prime", "p"), ("a", "a"), ("b", "b")):
        equal(integer(pari[raw_key], f"PARI raw {raw_key}"), gp[script_key],
              f"PARI raw-to-script {raw_key} binding")
    trace = integer(pari["trace"], "PARI trace")
    order = integer(pari["order"], "PARI order")
    equal(order, EXPECTED_PRIME + 1 - trace, "PARI order identity")
    require(trace * trace <= 4 * EXPECTED_PRIME,
            "PARI trace is outside the Hasse interval")
    equal(trace % EXPECTED_TRACE_PRIOR[0], EXPECTED_TRACE_PRIOR[1],
          "PARI trace-prior congruence")
    equal(pari["trace"], hybrid_trace, "PARI-to-hybrid trace agreement")
    equal(pari_result["pari_version"], "2.17.4", "PARI result version")
    equal(pari_result["executable_sha256"], EXPECTED_PARI_SHA256,
          "PARI executable digest")
    equal(pari_result["prime_check"], True, "PARI result primality flag")
    equal(integer(pari_result["order"], "PARI result order"), order,
          "PARI result order")
    equal(integer(pari_result["trace"], "PARI result trace"), trace,
          "PARI result trace")
    equal(pari_result["elapsed_ms"],
          integer(pari["elapsed_ms"], "PARI raw elapsed time"),
          "PARI result elapsed time")
    equal(pari_result["agrees_with_hybrid_trace"], True,
          "PARI agreement flag")
    equal(pari_result["production_dependency"], False,
          "PARI production-dependency flag")
    equal(integer(target["curve_a"], "target curve a"), gp["a"],
          "PARI-to-target a binding")
    equal(integer(target["curve_b"], "target curve b"), gp["b"],
          "PARI-to-target b binding")


def validate_inputs(inputs):
    equal(inputs["smooth_cache_sha256"], EXPECTED_SMOOTH_SHA256,
          "smooth-cache digest")
    equal(inputs["weber_manifest_sha256"], EXPECTED_WEBER_MANIFEST_SHA256,
          "Weber manifest digest")
    equal(inputs["direct_cache_sha256"], EXPECTED_DIRECT_SHA256,
          "direct-cache digest")
    equal(inputs["direct_cache_bytes"], 30203068, "direct-cache size")
    equal(inputs["direct_levels"], EXPECTED_DIRECT_LEVELS,
          "direct level schedule")
    equal(inputs["direct_max_prime_candidates"], 10000000,
          "direct prime-candidate cap")
    equal(inputs["direct_max_x_candidates_per_surface"], 1000000,
          "direct x-candidate cap")
    equal(inputs["direct_cache_resident_budget_bytes"], 1000000000,
          "direct cache residency budget")
    equal(inputs["sea_threads"], 1, "SEA thread count")


def validate_commands():
    script = (ROOT / "commands.sh").read_text(encoding="utf-8")
    assignments = {
        "P": EXPECTED_PRIME,
        "SMOOTH_SHA": EXPECTED_SMOOTH_SHA256,
        "DIRECT_SHA": EXPECTED_DIRECT_SHA256,
        "LEVELS": ",".join(str(level) for level in EXPECTED_DIRECT_LEVELS),
    }
    for name, expected in assignments.items():
        match = re.search(rf"(?m)^{name}=([^\n]+)$", script)
        require(match is not None, f"commands.sh does not assign {name}")
        observed = match.group(1)
        if name == "P":
            equal(integer(observed, "commands prime"), expected,
                  "commands prime")
        else:
            equal(observed, expected, f"commands {name}")
    blocks = re.findall(r'"\$BIN" search \\\n(.*?)(?=\n\n)', script,
                        re.DOTALL)
    equal(len(blocks), 2, "commands search invocation count")
    baseline, hybrid = blocks
    common_fragments = (
        '--p "$P" --seed 202607300000',
        "--range-start 2000000 --range-end 2000001",
        "--worker-id 0 --worker-count 1",
        "--curve-family x1-27 --x1-require-point4 1",
        "--max-level 401 --trace-cap 64",
        '--smooth-cache-sha256 "$SMOOTH_SHA"',
        "--curve-threads 1 --smooth-coordinators 1 --sea-threads 1",
        "--sea-level-telemetry 0 --max-curves 1",
    )
    for number, block in enumerate((baseline, hybrid), 1):
        collapsed = " ".join(block.replace("\\", " ").split())
        for fragment in common_fragments:
            require(fragment in collapsed,
                    f"search command {number} misses {fragment!r}")
    baseline_collapsed = " ".join(baseline.replace("\\", " ").split())
    hybrid_collapsed = " ".join(hybrid.replace("\\", " ").split())
    require("--sea-strategy" not in baseline_collapsed and
            "--classical-direct" not in baseline_collapsed,
            "baseline command contains direct-first options")
    for fragment in (
            "--sea-strategy direct-first",
            '--classical-direct-levels "$LEVELS"',
            "--classical-direct-max-prime-candidates 10000000",
            "--classical-direct-max-x-candidates 1000000",
            '--classical-direct-context-sha256 "$DIRECT_SHA"',
            "--classical-direct-cache-resident-bytes 1000000000"):
        require(fragment in hybrid_collapsed,
                f"hybrid command misses {fragment!r}")
    require('"$GP" -q -f -s 2000000000' in script and
            "point_count.gp" in script,
            "commands.sh does not retain the PARI invocation")


def validate_scope(result):
    equal(result["magma_attempt"], {
        "status": "no_result",
        "reason": "local launcher entered uninterruptible filesystem wait before computation",
        "agreement_claimed": False,
    }, "Magma claim scope")
    equal(result["claim_scope"], {
        "fixed_curve_latency_comparison": True,
        "retained_state_composition_exercised": True,
        "independent_exact_point_count": True,
        "multi_curve_throughput": False,
        "certificate_found": False,
        "certificate_yield_measured": False,
        "cm_crossover_measured": False,
        "asymptotic_exponent_proved": False,
    }, "artifact claim scope")


def validate_readme(result):
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    normalized = " ".join(readme.split())
    baseline = result["baseline"]
    hybrid = result["hybrid"]
    required = (
        f"{baseline['sea_us'] / 1_000_000:.6f} s",
        f"{hybrid['sea_us'] / 1_000_000:.6f} s",
        f"{baseline['total_us'] / 1_000_000:.6f} s",
        f"{hybrid['total_us'] / 1_000_000:.6f} s",
        f"{result['comparison']['sea_speedup']:.5f}x faster",
        f"{result['comparison']['total_speedup']:.5f}x faster",
        result["hybrid"]["trace"],
        EXPECTED_COMMIT,
        EXPECTED_TREE,
        EXPECTED_SEARCH_BINARY_SHA256,
        EXPECTED_PARI_SHA256,
        EXPECTED_DIRECT_SHA256,
        EXPECTED_SMOOTH_SHA256,
        EXPECTED_WEBER_MANIFEST_SHA256,
        "not a multi-curve throughput result",
        "not an independent mathematical oracle",
        "No Magma agreement is claimed",
    )
    for fragment in required:
        require(" ".join(fragment.split()) in normalized,
                f"README is missing retained claim {fragment!r}")


def main():
    authenticate()
    result = load_json(ROOT / "result.json")
    baseline = load_single_json_record(ROOT / "raw/weber.progress.ndjson")
    hybrid = load_single_json_record(ROOT / "raw/hybrid.progress.ndjson")
    baseline_checkpoint = load_json(ROOT / "raw/weber.checkpoint.json")
    hybrid_checkpoint = load_json(ROOT / "raw/hybrid.checkpoint.json")

    equal(result["schema"], "oneshotsea.p125-direct-first-hybrid-ab.v1",
          "result schema")
    equal(result["date"], "2026-08-03", "result date")
    validate_git_source(result["source"])
    validate_target(result["target"])
    validate_inputs(result["authenticated_inputs"])
    validate_progress_and_checkpoint(
        baseline, baseline_checkpoint, EXPECTED_BASELINE_SCHEDULE, 0,
        "baseline")
    validate_progress_and_checkpoint(
        hybrid, hybrid_checkpoint, EXPECTED_HYBRID_SCHEDULE, 1, "hybrid")
    validate_result_projection(result, baseline, hybrid)
    validate_comparison(result, baseline, hybrid)
    validate_verifier_source(result, result["target"])
    validate_native_result(result, result["target"], hybrid["trace"])
    validate_pari_result(result, result["target"], hybrid["trace"])
    validate_commands()
    validate_scope(result)
    validate_readme(result)

    print("p125 direct-first hybrid artifact: PASS")
    print(f"SEA speedup: {result['comparison']['sea_speedup']:.6f}x")
    print(f"total speedup: {result['comparison']['total_speedup']:.6f}x")
    print("native and independent PARI trace agreement: PASS")


if __name__ == "__main__":
    try:
        main()
    except (AuditError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(f"p125 direct-first hybrid artifact: FAIL: {error}",
              file=sys.stderr)
        raise SystemExit(1)
