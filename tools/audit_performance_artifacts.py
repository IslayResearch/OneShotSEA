#!/usr/bin/env python3
"""Audit the retained raw files behind the headline p125 performance claims."""

from __future__ import annotations

import hashlib
import json
import math
import re
import statistics
import subprocess
from functools import cmp_to_key
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "artifacts" / "local"
BUNDLES = (
    "p125-x1-11-trace-cap-20260801",
    "p125-x1-27-family-ab-20260801",
    "p125-curve-parallel-20260801",
    "p125-prime-schedule-20260801",
    "p125-weber-root-orbits-20260801",
    "p125-polynomial-reducer-ab-20260731",
    "p125-curve-twist-workcount-20260801",
    "p125-classical-direct-compact-20260803",
)


def fail(message: str) -> None:
    raise AssertionError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_json(path: Path) -> dict:
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def load_jsonl(path: Path) -> list[dict]:
    with path.open(encoding="utf-8") as stream:
        return [json.loads(line) for line in stream if line.strip()]


def load_json_log(path: Path) -> list[dict]:
    rows: list[dict] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("{"):
            rows.append(json.loads(line))
    return rows


def canonical_line(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def close(actual: float, expected: float, tolerance: float = 1e-6) -> None:
    if not math.isclose(actual, expected, rel_tol=tolerance, abs_tol=tolerance):
        fail(f"{actual!r} != {expected!r}")


def audit_checksums(bundle_name: str) -> None:
    bundle = ARTIFACTS / bundle_name
    checksum_path = bundle / "SHA256SUMS"
    declared: dict[str, str] = {}
    for line in checksum_path.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        if not match:
            fail(f"malformed checksum line in {checksum_path}: {line!r}")
        name = match.group(2).removeprefix("./")
        if name in declared:
            fail(f"duplicate checksum entry: {bundle_name}/{name}")
        declared[name] = match.group(1)
    paths = list(bundle.rglob("*"))
    symlinks = [path for path in paths if path.is_symlink()]
    if symlinks:
        fail(f"symlinks are forbidden in {bundle_name}: {symlinks}")
    present = {path.relative_to(bundle).as_posix() for path in paths
               if path.is_file() and path.name != "SHA256SUMS"}
    if set(declared) != present:
        fail(
            f"checksum file set mismatch for {bundle_name}: "
            f"missing={sorted(present - set(declared))}, "
            f"extra={sorted(set(declared) - present)}"
        )
    for name, expected in declared.items():
        actual = sha256(bundle / name)
        if actual != expected:
            fail(f"checksum mismatch for {bundle_name}/{name}: {actual} != {expected}")


def audit_tracked_files() -> None:
    required = {"tools/audit_performance_artifacts.py"}
    for bundle_name in BUNDLES:
        bundle = ARTIFACTS / bundle_name
        required.update(path.relative_to(ROOT).as_posix()
                        for path in bundle.rglob("*") if path.is_file())
    completed = subprocess.run(
        ["git", "ls-files", "-z", "--", "tools/audit_performance_artifacts.py",
         *(f"artifacts/local/{bundle}" for bundle in BUNDLES)],
        cwd=ROOT, check=True, stdout=subprocess.PIPE,
    )
    tracked = {entry.decode() for entry in completed.stdout.split(b"\0") if entry}
    missing = required - tracked
    if missing:
        fail(f"performance evidence is not staged/tracked: {sorted(missing)}")


def verify_file(path: Path, expected: str, *, lines: int | None = None,
                size: int | None = None) -> None:
    if sha256(path) != expected:
        fail(f"embedded checksum mismatch: {path}")
    raw = path.read_bytes()
    if lines is not None and raw.count(b"\n") != lines:
        fail(f"line-count mismatch: {path}")
    if size is not None and len(raw) != size:
        fail(f"byte-count mismatch: {path}")


def normalized_curve(row: dict) -> dict:
    value = json.loads(json.dumps(row))
    for key in ("peak_rss_bytes", "timings_us", "state"):
        value.pop(key, None)
    for level in value.get("sea_level_timings", []):
        for key in tuple(level):
            if key.endswith("_us") or key == "modular_root_workers":
                level.pop(key)
    return value


def time_p(path: Path) -> tuple[float, float, float]:
    text = path.read_text(encoding="utf-8")
    portable = {
        key: float(value)
        for key, value in re.findall(r"^(real|user|sys)\s+([0-9.]+)$", text, re.MULTILINE)
    }
    if len(portable) == 3:
        return portable["real"], portable["user"], portable["sys"]
    extended = re.search(
        r"([0-9.]+) real\s+([0-9.]+) user\s+([0-9.]+) sys", text
    )
    if not extended:
        fail(f"unrecognized time output: {path}")
    return tuple(float(value) for value in extended.groups())  # type: ignore[return-value]


def vm_stat(path: Path) -> dict[str, int]:
    values: dict[str, int] = {}
    for label, value in re.findall(r"^(Pageouts|Swapins|Swapouts):\s+([0-9]+)\.$",
                                   path.read_text(encoding="utf-8"), re.MULTILINE):
        values[label.lower()] = int(value)
    if set(values) != {"pageouts", "swapins", "swapouts"}:
        fail(f"incomplete vm_stat capture: {path}")
    return values


def sum_level_field(row: dict, field: str) -> int:
    return sum(int(level["timings_us"].get(field, 0)) if "timings_us" in level
               else int(level.get(field, 0))
               for level in row.get("sea_level_timings", []))


def audit_trace_caps() -> None:
    bundle = ARTIFACTS / "p125-x1-11-trace-cap-20260801"
    result = load_json(bundle / "result.json")
    verify_file(bundle / result["implementation_identity"]["retained_binary_path"],
                result["implementation_identity"]["binary_sha256"])
    indices = [str(value) for value in result["target"]["index_order_for_all_per_index_arrays"]]
    for label, reported in result["runs"].items():
        raw_meta = result["raw_outputs"][label]
        for kind, filename in (("checkpoint", "checkpoint.json"),
                               ("progress", "progress.ndjson"),
                               ("run_log", "run.log")):
            meta = raw_meta[kind]
            verify_file(bundle / meta["retained_path"], meta["sha256"],
                        lines=meta["lines"], size=meta["bytes"])
        rows = load_jsonl(bundle / raw_meta["progress"]["retained_path"])
        if [row["index"] for row in rows] != indices:
            fail(f"trace-cap index sequence mismatch for {label}")
        timing_keys = ("generation", "sea", "smoothness", "candidate",
                       "assembly", "verifier", "total")
        for key in timing_keys:
            expected = sum(int(row["timings_us"][key]) for row in rows)
            if expected != reported["aggregate_timings_us"][key]:
                fail(f"trace-cap timing mismatch for {label}/{key}")
        counts = {
            "sea_levels": sum(int(row["sea_levels"]) for row in rows),
            "exact_sea_levels": sum(int(row["exact_sea_levels"]) for row in rows),
            "atkin_sea_levels": sum(int(row.get("atkin_sea_levels", 0)) for row in rows),
            "initial_trace_candidates": sum(int(row["initial_trace_count"]) for row in rows),
            "full_point_counts": sum(bool(row["full_point_count"]) for row in rows),
            "sound_smoothness_rejections": sum(row["status"] == "sound_smoothness_reject" for row in rows),
            "certificates_found": sum(row["status"] == "verified_certificate" for row in rows),
        }
        if counts != reported["aggregate_counts"]:
            fail(f"trace-cap aggregate mismatch for {label}")
        if max(int(row["peak_rss_bytes"]) for row in rows) != reported["peak_rss_bytes"]:
            fail(f"trace-cap peak mismatch for {label}")
        captured_time = time_p(bundle / raw_meta["run_log"]["retained_path"])
        for actual, field in zip(captured_time, ("real", "user", "sys")):
            close(actual, reported["invocation_seconds"][field])
    comparison = result["comparison"]["wall"]
    for label, reported in result["runs"].items():
        close(reported["invocation_seconds"]["real"] / 10,
              comparison["seconds_per_curve"][label])
        close(10 / reported["invocation_seconds"]["real"],
              comparison["curves_per_second"][label])
    cap64_real = result["runs"]["cap64"]["invocation_seconds"]["real"]
    cap16_real = result["runs"]["cap16"]["invocation_seconds"]["real"]
    cap1_real = result["runs"]["cap1"]["invocation_seconds"]["real"]
    close(cap64_real / cap16_real,
          comparison["cap16_over_cap64_throughput_ratio"])
    close(cap64_real - cap16_real, comparison["cap16_vs_cap64_real_reduction_seconds"])
    close((cap64_real - cap16_real) / cap64_real,
          comparison["cap16_vs_cap64_real_reduction_fraction"])
    close(cap16_real - cap1_real, comparison["cap1_vs_cap16_real_reduction_seconds"])
    close((cap16_real - cap1_real) / cap16_real,
          comparison["cap1_vs_cap16_real_reduction_fraction"])
    work = result["comparison"]["work"]
    pairs = (
        ("cap16_vs_cap64_initial_trace_candidates", "aggregate_counts", "initial_trace_candidates"),
        ("cap16_vs_cap64_aggregate_total_us", "aggregate_timings_us", "total"),
        ("cap16_vs_cap64_aggregate_smoothness_us", "aggregate_timings_us", "smoothness"),
    )
    for key, group, field in pairs:
        item = work[key]
        baseline = result["runs"]["cap64"][group][field]
        optimized = result["runs"]["cap16"][group][field]
        if item["cap64"] != baseline or item["cap16"] != optimized or item["reduction"] != baseline - optimized:
            fail(f"trace-cap comparison mismatch: {key}")
        close((baseline - optimized) / baseline, item["reduction_fraction"])
    close(work["cap16_vs_cap64_aggregate_total_us"]["cap64"] /
          work["cap16_vs_cap64_aggregate_total_us"]["cap16"],
          work["cap16_vs_cap64_aggregate_total_us"]["baseline_over_cap16_ratio"])
    sea_levels = work["cap16_vs_cap64_sea_levels"]
    if sea_levels["cap64"] != result["runs"]["cap64"]["aggregate_counts"]["sea_levels"] or \
            sea_levels["cap16"] != result["runs"]["cap16"]["aggregate_counts"]["sea_levels"] or \
            sea_levels["increase"] != sea_levels["cap16"] - sea_levels["cap64"]:
        fail("trace-cap SEA-level comparison mismatch")
    cap1_total = work["cap1_vs_cap16_aggregate_total_us"]
    if cap1_total["cap16"] != result["runs"]["cap16"]["aggregate_timings_us"]["total"] or \
            cap1_total["cap1"] != result["runs"]["cap1"]["aggregate_timings_us"]["total"] or \
            cap1_total["increase"] != cap1_total["cap1"] - cap1_total["cap16"]:
        fail("cap1/cap16 total comparison mismatch")
    close(cap1_total["increase"] / cap1_total["cap16"], cap1_total["increase_fraction"])
    for key, field, direction in (("cap1_vs_cap16_sea_us", "sea", "increase"),
                                  ("cap1_vs_cap16_smoothness_us", "smoothness", "reduction")):
        item = work[key]
        if item["cap16"] != result["runs"]["cap16"]["aggregate_timings_us"][field] or \
                item["cap1"] != result["runs"]["cap1"]["aggregate_timings_us"][field]:
            fail(f"trace-cap comparison inputs mismatch: {key}")
        delta = item["cap1"] - item["cap16"] if direction == "increase" else item["cap16"] - item["cap1"]
        if item[direction] != delta:
            fail(f"trace-cap comparison delta mismatch: {key}")


def audit_family() -> None:
    bundle = ARTIFACTS / "p125-x1-27-family-ab-20260801"
    result = load_json(bundle / "result.json")
    verify_file(bundle / result["implementation_identity"]["retained_binary_path"],
                result["implementation_identity"]["binary_sha256"])
    verify_file(bundle / result["implementation_identity"]["retained_runner_path"],
                result["implementation_identity"]["runner_sha256"])
    mapping = {"x1_11_b1": "b1", "x1_27_x1": "x1",
               "x1_27_x2": "x2", "x1_11_b2": "b2"}
    rows_by_run: dict[str, list[dict]] = {}
    outcome = result["outcomes_per_run"]
    for label, directory in mapping.items():
        meta = result["raw_outputs"][label]
        for kind, filename in (("search_log", "search.log"),
                               ("progress", "progress.ndjson"),
                               ("checkpoint", "checkpoint.json")):
            verify_file(bundle / "raw" / directory / filename, meta[kind]["sha256"],
                        lines=meta[kind]["lines"])
        rows = load_jsonl(bundle / "raw" / directory / "progress.ndjson")
        rows_by_run[label] = rows
        if [row["index"] for row in rows] != [str(index) for index in range(10)]:
            fail(f"family index sequence mismatch for {label}")
        reported = result["runs"][label]
        captured_time = time_p(bundle / "raw" / directory / "search.log")
        for actual, field in zip(captured_time, ("real", "user", "sys")):
            close(actual, reported["invocation_time_seconds"][field])
        for key in ("generation", "sea", "smoothness", "total"):
            if sum(int(row["timings_us"][key]) for row in rows) != reported["timings_us_sum"][key]:
                fail(f"family timing mismatch for {label}/{key}")
        scalar_fields = {
            "sea_levels_sum": "sea_levels",
            "exact_sea_levels_sum": "exact_sea_levels",
            "atkin_sea_levels_sum": "atkin_sea_levels",
            "final_exact_trace_candidates_sum": "final_exact_trace_candidates",
            "final_trace_candidates_sum": "final_trace_candidates",
        }
        for output, source in scalar_fields.items():
            if sum(int(row.get(source, 0)) for row in rows) != reported[output]:
                fail(f"family count mismatch for {label}/{source}")
        if max(int(row["peak_rss_bytes"]) for row in rows) != reported["reported_peak_rss_bytes_max"]:
            fail(f"family peak mismatch for {label}")
        observed = {
            "curves_attempted": len(rows),
            "sound_smoothness_rejections": sum(row["status"] == "sound_smoothness_reject" for row in rows),
            "heuristic_rejections": sum(bool(row["heuristic"]) for row in rows),
            "full_point_counts": sum(bool(row["full_point_count"]) for row in rows),
            "candidates_reaching_smoothness": sum(bool(row["reached_smoothness"]) for row in rows),
            "certificates_found": sum(row["status"] == "verified_certificate" for row in rows),
        }
        for key, value in observed.items():
            if value != outcome[key]:
                fail(f"family outcome mismatch for {label}/{key}")
        checkpoint = load_json(bundle / "raw" / directory / "checkpoint.json")
        if int(checkpoint["next_index"]) != 10 or int(checkpoint["counters"]["curves_attempted"]) != 10:
            fail(f"family checkpoint frontier mismatch for {label}")
        for field, expected in (("rejected_sound_early_abort", 10),
                                ("rejected_heuristic", 0),
                                ("full_point_counts_completed", outcome["full_point_counts"]),
                                ("certificates_found", 0)):
            if int(checkpoint["counters"][field]) != expected:
                fail(f"family checkpoint counter mismatch for {label}/{field}")
        summaries = [row for row in load_json_log(bundle / "raw" / directory / "search.log")
                     if row.get("schema") == "oneshotsea.search-summary.v1"]
        if len(summaries) != 1 or bool(summaries[0]["range_exhausted"]) != outcome["range_exhausted"] or \
                bool(summaries[0]["verified"]) != outcome["verified"]:
            fail(f"family summary mismatch for {label}")
    if (bundle / "raw/b1/checkpoint.json").read_bytes() != (bundle / "raw/b2/checkpoint.json").read_bytes():
        fail("X1(11) repeated checkpoints differ")
    if (bundle / "raw/x1/checkpoint.json").read_bytes() != (bundle / "raw/x2/checkpoint.json").read_bytes():
        fail("X1(27) repeated checkpoints differ")
    for left, right in (("x1_11_b1", "x1_11_b2"), ("x1_27_x1", "x1_27_x2")):
        if [normalized_curve(row) for row in rows_by_run[left]] != [normalized_curve(row) for row in rows_by_run[right]]:
            fail(f"family semantic repeat mismatch: {left}/{right}")
    measured = result["measured_comparison"]
    x11 = statistics.mean(result["runs"][key]["invocation_time_seconds"]["real"]
                          for key in ("x1_11_b1", "x1_11_b2"))
    x27 = statistics.mean(result["runs"][key]["invocation_time_seconds"]["real"]
                          for key in ("x1_27_x1", "x1_27_x2"))
    close(x11, measured["x1_11_two_run_mean"]["wall_seconds"])
    close(x27, measured["x1_27_two_run_mean"]["wall_seconds"])
    close(x11 / x27, measured["ratios_x1_27_over_x1_11"]["wall_curve_throughput"])
    for name, keys in (("x1_11_two_run_mean", ("x1_11_b1", "x1_11_b2")),
                       ("x1_27_two_run_mean", ("x1_27_x1", "x1_27_x2"))):
        item = measured[name]
        close(item["wall_seconds"] / 10, item["wall_seconds_per_curve"])
        close(10 / item["wall_seconds"], item["wall_curves_per_second"])
        for source, output in (("generation", "generation_us_sum"),
                               ("sea", "sea_us_sum"),
                               ("smoothness", "smoothness_us_sum"),
                               ("total", "total_us_sum")):
            close(statistics.mean(result["runs"][key]["timings_us_sum"][source]
                                  for key in keys), item[output])
    x11_mean = measured["x1_11_two_run_mean"]
    x27_mean = measured["x1_27_two_run_mean"]
    ratios = measured["ratios_x1_27_over_x1_11"]
    close(x27_mean["wall_seconds"] / x11_mean["wall_seconds"], ratios["wall_time"])
    close(x27_mean["generation_us_sum"] / x11_mean["generation_us_sum"],
          ratios["generation_task_time"])
    for field, output in (("sea_us_sum", "sea_task_throughput"),
                          ("smoothness_us_sum", "smoothness_task_throughput"),
                          ("total_us_sum", "total_concurrent_task_work_throughput")):
        close(x11_mean[field] / x27_mean[field], ratios[output])


def audit_curve_parallel() -> None:
    bundle = ARTIFACTS / "p125-curve-parallel-20260801"
    result = load_json(bundle / "result.json")
    verify_file(bundle / result["evaluated_source"]["retained_frozen_binary_path"],
                result["evaluated_source"]["frozen_binary_sha256"])
    raw = result["raw_sha256"]
    paths = {
        "serial_stdout_jsonl": "raw/k1-k2/serial.jsonl",
        "parallel_stdout_jsonl": "raw/k1-k2/parallel.jsonl",
        "serial_progress_jsonl": "raw/k1-k2/serial/progress.jsonl",
        "parallel_progress_jsonl": "raw/k1-k2/parallel/progress.jsonl",
        "serial_time": "raw/k1-k2/serial.time",
        "parallel_time": "raw/k1-k2/parallel.time",
        "vm_before_parallel": "raw/k1-k2/vm-before-parallel.txt",
        "vm_after_parallel": "raw/k1-k2/vm-after-parallel.txt",
    }
    for key, path in paths.items():
        verify_file(bundle / path, raw[key])
    mode_rows: dict[str, list[dict]] = {}
    for label, prefix in (("serial", "serial"), ("parallel", "parallel")):
        wall, user, system = time_p(bundle / f"raw/k1-k2/{prefix}.time")
        mode = result["modes"][label]
        close(wall, mode["invocation_wall_seconds"])
        close(user, mode["user_seconds"])
        close(system, mode["system_seconds"])
        rows = load_jsonl(bundle / f"raw/k1-k2/{prefix}/progress.jsonl")
        mode_rows[label] = rows
        if max(int(row["peak_rss_bytes"]) for row in rows) != mode["reported_peak_rss_bytes"]:
            fail(f"curve-window peak mismatch for {label}")
        for row in rows:
            index = row["index"]
            close(int(row["timings_us"]["total"]) / 1_000_000,
                  mode["curve_total_seconds"][index])
            close(int(row["timings_us"]["sea"]) / 1_000_000,
                  mode["curve_sea_seconds"][index])
            close(int(row["timings_us"]["smoothness"]) / 1_000_000,
                  mode["curve_smooth_seconds"][index])
    followups = {3: "k3", 5: "k5", 10: "k10"}
    progress_paths = [bundle / "raw/k1-k2/serial/progress.jsonl",
                      bundle / "raw/k1-k2/parallel/progress.jsonl"]
    for slots, directory in followups.items():
        item = result["scaling_followups"][f"curve_threads_{slots}"]
        field_paths = {
            "stdout_jsonl": f"raw/{directory}/{directory}.jsonl",
            "progress_jsonl": f"raw/{directory}/{directory}/progress.jsonl",
            "checkpoint": f"raw/{directory}/{directory}/checkpoint.json",
            "time": f"raw/{directory}/{directory}.time",
            "vm_before": f"raw/{directory}/vm-before-{directory}.txt",
            "vm_after": f"raw/{directory}/vm-after-{directory}.txt",
        }
        for key, path in field_paths.items():
            verify_file(bundle / path, item["raw_sha256"][key])
        wall, user, _ = time_p(bundle / field_paths["time"])
        close(wall, item["invocation_wall_seconds"])
        close(user, item["user_seconds"])
        close(wall / slots, item["invocation_seconds_per_curve"])
        rows = load_jsonl(bundle / field_paths["progress_jsonl"])
        warm = max(int(row["timings_us"]["total"]) for row in rows) / 1_000_000
        close(warm, item["warm_window_seconds"])
        close(warm / slots, item["warm_seconds_per_curve"])
        if max(int(row["peak_rss_bytes"]) for row in rows) != item["reported_peak_rss_bytes"]:
            fail(f"curve-window follow-up peak mismatch for K={slots}")
        before = vm_stat(bundle / field_paths["vm_before"])
        after = vm_stat(bundle / field_paths["vm_after"])
        observed_vm = {key: after[key] - before[key] for key in before}
        if observed_vm != item["system_vm_delta_pages"]:
            fail(f"curve-window vm_stat mismatch for K={slots}")
        if "warm_speedup_over_serial_sum" in item:
            serial_rows = load_jsonl(bundle / "raw/k3/serial10/progress.jsonl")
            serial_sum = (sum(int(row["timings_us"]["total"])
                              for row in mode_rows["serial"]) +
                          sum(int(row["timings_us"]["total"])
                              for row in serial_rows)) / 1_000_000
            close(serial_sum / warm, item["warm_speedup_over_serial_sum"])
        progress_paths.append(bundle / field_paths["progress_jsonl"])
    projections: dict[str, dict] = {}
    for path in progress_paths:
        for row in load_jsonl(path):
            projection = normalized_curve(row)
            old = projections.setdefault(row["index"], projection)
            if old != projection:
                fail(f"curve-window non-timing mismatch at index {row['index']}")
    comparison = result["comparison"]
    close(result["modes"]["serial"]["invocation_wall_seconds"] /
          result["modes"]["parallel"]["invocation_wall_seconds"],
          comparison["full_invocation_speedup"])
    serial_warm = sum(int(row["timings_us"]["total"])
                      for row in mode_rows["serial"]) / 1_000_000
    parallel_warm = max(int(row["timings_us"]["total"])
                        for row in mode_rows["parallel"]) / 1_000_000
    close(serial_warm, comparison["serial_warm_curve_seconds_sum"])
    close(parallel_warm, comparison["parallel_warm_curve_window_seconds"])
    close(serial_warm / parallel_warm,
          comparison["warm_curve_throughput_speedup"])
    close(result["modes"]["parallel"]["reported_peak_rss_bytes"] /
          result["modes"]["serial"]["reported_peak_rss_bytes"],
          comparison["peak_rss_ratio_parallel_over_serial"])
    if comparison["peak_rss_increase_bytes"] != \
            result["modes"]["parallel"]["reported_peak_rss_bytes"] - \
            result["modes"]["serial"]["reported_peak_rss_bytes"]:
        fail("curve-window peak-RSS delta mismatch")
    before = vm_stat(bundle / paths["vm_before_parallel"])
    after = vm_stat(bundle / paths["vm_after_parallel"])
    observed_vm = {key: after[key] - before[key] for key in before}
    vm_report = result["system_vm_stat_parallel_window"]
    if observed_vm != {"pageouts": vm_report["pageouts_delta_pages"],
                       "swapins": vm_report["swapins_delta_pages"],
                       "swapouts": vm_report["swapouts_delta_pages"]}:
        fail("curve-window main vm_stat mismatch")
    for key in ("pageouts", "swapins", "swapouts"):
        if vm_report[f"{key}_delta_bytes"] != observed_vm[key] * vm_report["page_size_bytes"]:
            fail(f"curve-window vm_stat byte mismatch: {key}")


def audit_prime_schedule() -> None:
    bundle = ARTIFACTS / "p125-prime-schedule-20260801"
    result = load_json(bundle / "result.json")
    profile = bundle / result["profile"]["path"]
    verify_file(profile, result["profile"]["sha256"])
    profile_rows = [line.split() for line in profile.read_text(encoding="utf-8").splitlines()
                    if line and not line.startswith("#")]
    if len(profile_rows) != result["profile"]["levels"] or \
            max(int(row[0]) for row in profile_rows) != result["profile"]["max_level"]:
        fail("prime-schedule profile shape mismatch")
    profile_values = [(int(ell), int(information), int(cost))
                      for ell, information, cost in profile_rows]
    if len({row[0] for row in profile_values}) != len(profile_values) or \
            any(information < 0 or cost <= 0 for _, information, cost in profile_values):
        fail("prime-schedule profile contains invalid rows")

    def compare_profile(left: tuple[int, int, int], right: tuple[int, int, int]) -> int:
        lhs = left[1] * right[2]
        rhs = right[1] * left[2]
        if lhs != rhs:
            return -1 if lhs > rhs else 1
        return (left[0] > right[0]) - (left[0] < right[0])

    derived_expected_order = [row[0] for row in sorted(profile_values,
                                                        key=cmp_to_key(compare_profile))]
    paths = {
        "increasing_jsonl": "p125-prime-schedule-increasing.jsonl",
        "increasing_time": "p125-prime-schedule-increasing.time",
        "expected_jsonl": "p125-prime-schedule-expected.jsonl",
        "expected_time": "p125-prime-schedule-expected.time",
        "expected_2_jsonl": "p125-prime-schedule-expected-2.jsonl",
        "expected_2_time": "p125-prime-schedule-expected-2.time",
        "increasing_2_jsonl": "p125-prime-schedule-increasing-2.jsonl",
        "increasing_2_time": "p125-prime-schedule-increasing-2.time",
    }
    for key, path in paths.items():
        verify_file(bundle / "raw" / path, result["raw_sha256"][key])
    increasing_times = [time_p(bundle / "raw" / paths[key])
                        for key in ("increasing_time", "increasing_2_time")]
    expected_times = [time_p(bundle / "raw" / paths[key])
                      for key in ("expected_time", "expected_2_time")]
    increasing = [item[0] for item in increasing_times]
    expected = [item[0] for item in expected_times]
    if increasing != result["increasing"]["wall_seconds"]:
        fail("increasing schedule wall samples mismatch")
    if expected != result["expected_information_per_cost"]["wall_seconds"]:
        fail("expected-cost schedule wall samples mismatch")
    if [item[1] for item in increasing_times] != result["increasing"]["user_seconds"] or \
            [item[1] for item in expected_times] != result["expected_information_per_cost"]["user_seconds"]:
        fail("prime-schedule user samples mismatch")
    close(statistics.median(increasing), result["increasing"]["median_wall_seconds"])
    close(statistics.median(expected), result["expected_information_per_cost"]["median_wall_seconds"])
    close(statistics.median(item[1] for item in increasing_times),
          result["increasing"]["median_user_seconds"])
    close(statistics.median(item[1] for item in expected_times),
          result["expected_information_per_cost"]["median_user_seconds"])
    close(statistics.median(expected) / statistics.median(increasing),
          result["expected_over_increasing_wall_ratio"])
    close(100 * (statistics.median(expected) / statistics.median(increasing) - 1),
          result["expected_wall_delta_percent"], tolerance=1e-3)
    run_paths = [bundle / "raw" / paths[key] for key in
                 ("increasing_jsonl", "expected_jsonl", "expected_2_jsonl", "increasing_2_jsonl")]
    run_rows = [load_jsonl(path) for path in run_paths]
    increasing_order = [int(row["ell"]) for row in run_rows[0] if row["type"] == "level"]
    if increasing_order != sorted(increasing_order) or \
            [int(row["ell"]) for row in run_rows[3] if row["type"] == "level"] != increasing_order:
        fail("increasing raw schedule order mismatch")
    expected_order = result["expected_information_per_cost"]["order"]
    if expected_order != derived_expected_order:
        fail("prime-schedule result order does not follow the retained profile")
    for rows in run_rows[1:3]:
        if [int(row["ell"]) for row in rows if row["type"] == "level"] != expected_order:
            fail("expected-cost raw schedule order mismatch")
    summaries: list[dict] = []
    semantic_runs: list[list[dict]] = []
    time_fields = {"source_lifts", "modular_roots", "normalized_codomain", "bmss", "eigenvalue"}
    for rows in run_rows:
        summary = next(row for row in rows if row["type"] == "summary").copy()
        summary.pop("prime_schedule")
        summaries.append(summary)
        semantic: list[dict] = []
        for row in rows:
            if row["type"] != "level":
                continue
            projected = {key: row.get(key) for key in
                         ("ell", "exact", "trace_residue", "atkin_projective_order",
                          "atkin_residue_count", "compatible_source_lifts",
                          "modular_root_workers", "modular_root_orbits",
                          "modular_root_reused_lifts", "modular_root_orbit_reuse")}
            projected["operation_counts"] = {
                key: value for key, value in row["timings_us"].items() if key not in time_fields
            }
            semantic.append(projected)
        semantic_runs.append(sorted(semantic, key=lambda row: int(row["ell"])))
    if any(summary != summaries[0] for summary in summaries[1:]):
        fail("prime-schedule final summaries differ")
    summary_digest = hashlib.sha256(canonical_line(summaries[0])).hexdigest()
    if summary_digest != result["equality"]["final_summary_projection_sha256"]:
        fail("prime-schedule final-summary digest mismatch")
    if any(semantic != semantic_runs[0] for semantic in semantic_runs[1:]):
        fail("prime-schedule intrinsic/operation projections differ")
    semantic_digest = hashlib.sha256(canonical_line(semantic_runs[0])).hexdigest()
    if semantic_digest != result["equality"]["sorted_intrinsic_and_operation_count_sha256"]:
        fail("prime-schedule audited semantic digest mismatch")
    intrinsic = [{key: row[key] for key in
                  ("ell", "exact", "trace_residue", "atkin_projective_order", "atkin_residue_count")}
                 for row in semantic_runs[0]]
    retained_math = sorted(load_jsonl(bundle / "raw/p125-prime-schedule-increasing-math.jsonl"),
                           key=lambda row: int(row["ell"]))
    if intrinsic != retained_math or (bundle / "raw/p125-prime-schedule-increasing-math.jsonl").read_bytes() != \
            (bundle / "raw/p125-prime-schedule-expected-math.jsonl").read_bytes():
        fail("prime-schedule retained mathematical projections differ")


def sum_flat_level_field(path: Path, field: str) -> int:
    return sum(int(row.get("timings_us", {}).get(field, 0)) for row in load_jsonl(path))


def audit_root_orbits() -> None:
    bundle = ARTIFACTS / "p125-weber-root-orbits-20260801"
    result = load_json(bundle / "result.json")
    verify_file(bundle / result["retained_binary_path"], result["binary_sha256"])
    raw = result["observed_raw_sha256"]
    paths = {
        "baseline_run_1_jsonl": "baseline.jsonl",
        "optimized_run_1_jsonl": "orbit.jsonl",
        "baseline_run_2_jsonl": "baseline2.jsonl",
        "optimized_run_2_jsonl": "orbit2.jsonl",
        "index_4_baseline_progress": "index4-baseline-progress.jsonl",
        "index_4_optimized_jsonl": "index4-orbit.jsonl",
        "index_4_time": "index4-orbit.time",
    }
    for key, path in paths.items():
        verify_file(bundle / "raw" / path, raw[key])
    if (bundle / "raw/baseline.math.jsonl").read_bytes() != (bundle / "raw/orbit.math.jsonl").read_bytes():
        fail("orbit A/B mathematical projections differ")
    if (bundle / "raw/index4-old.math.jsonl").read_bytes() != (bundle / "raw/index4-new.math.jsonl").read_bytes():
        fail("index-4 orbit mathematical projections differ")
    ab = result["matched_level_193_ablation"]
    if sha256(bundle / "raw/baseline.math.jsonl") != ab["canonical_math_sha256"]:
        fail("orbit A/B canonical-math digest mismatch")
    baseline_wall = [time_p(bundle / f"raw/{name}.time")[0] for name in ("baseline", "baseline2")]
    orbit_wall = [time_p(bundle / f"raw/{name}.time")[0] for name in ("orbit", "orbit2")]
    if baseline_wall != ab["baseline"]["wall_seconds"] or orbit_wall != ab["optimized"]["wall_seconds"]:
        fail("orbit wall samples mismatch")
    close(statistics.median(baseline_wall) / statistics.median(orbit_wall),
          ab["median_wall_speedup"], tolerance=5e-4)
    baseline_roots = [sum_flat_level_field(bundle / f"raw/{name}.jsonl", "modular_roots")
                      / 1_000_000 for name in ("baseline", "baseline2")]
    orbit_roots = [sum_flat_level_field(bundle / f"raw/{name}.jsonl", "modular_roots")
                   / 1_000_000 for name in ("orbit", "orbit2")]
    for name in ("baseline", "baseline2"):
        rows = load_jsonl(bundle / f"raw/{name}.jsonl")
        if sum(int(row.get("modular_root_orbits", 0)) for row in rows) != ab["baseline"]["root_evaluations"] or \
                sum(int(row.get("modular_root_reused_lifts", 0)) for row in rows) != 0:
            fail(f"baseline root-evaluation count mismatch: {name}")
    for name in ("orbit", "orbit2"):
        rows = load_jsonl(bundle / f"raw/{name}.jsonl")
        if sum(int(row.get("modular_root_orbits", 0)) for row in rows) != ab["optimized"]["root_evaluations"] or \
                sum(int(row.get("modular_root_reused_lifts", 0)) for row in rows) != ab["optimized"]["reused_source_lifts"]:
            fail(f"optimized root-evaluation count mismatch: {name}")
    for actual, expected in zip(baseline_roots, ab["baseline"]["modular_root_seconds"]):
        close(actual, expected)
    for actual, expected in zip(orbit_roots, ab["optimized"]["modular_root_seconds"]):
        close(actual, expected)
    close(statistics.mean(baseline_roots) / statistics.mean(orbit_roots),
          ab["mean_modular_root_speedup"], tolerance=5e-4)
    index4 = result["production_index_4_replay"]
    if sha256(bundle / "raw/index4-old.math.jsonl") != index4["canonical_level_projection_sha256"]:
        fail("index-4 canonical-level digest mismatch")
    baseline_row = next(row for row in load_jsonl(bundle / "raw/index4-baseline-progress.jsonl")
                        if row["index"] == "4")
    baseline_roots = sum_level_field(baseline_row, "modular_roots_us") / 1_000_000
    close(baseline_roots, index4["retained_baseline"]["modular_root_seconds"])
    close(int(baseline_row["timings_us"]["sea"]) / 1_000_000,
          index4["retained_baseline"]["sea_seconds"])
    index4_roots = sum_flat_level_field(bundle / "raw/index4-orbit.jsonl", "modular_roots") / 1_000_000
    index4_rows = load_jsonl(bundle / "raw/index4-orbit.jsonl")
    if sum(int(row.get("modular_root_orbits", 0)) for row in index4_rows) != index4["optimized"]["root_evaluations"] or \
            sum(int(row.get("modular_root_reused_lifts", 0)) for row in index4_rows) != index4["optimized"]["reused_source_lifts"]:
        fail("index-4 root-evaluation count mismatch")
    close(index4_roots, index4["optimized"]["modular_root_seconds"])
    close(baseline_roots / index4_roots,
          index4["modular_root_speedup"], tolerance=5e-4)
    index4_wall = time_p(bundle / "raw/index4-orbit.time")[0]
    close(index4_wall, index4["optimized"]["invocation_wall_seconds"])
    close((int(baseline_row["timings_us"]["sea"]) / 1_000_000) / index4_wall,
          index4["baseline_sea_to_optimized_invocation_speedup"], tolerance=5e-4)


def audit_reducer() -> None:
    bundle = ARTIFACTS / "p125-polynomial-reducer-ab-20260731"
    result = load_json(bundle / "result.json")
    paths = {
        "baseline_progress": "raw/baseline/progress.jsonl",
        "optimized_checkpoint": "raw/optimized/checkpoint.json",
        "optimized_progress": "raw/optimized/progress.jsonl",
        "optimized_run_log": "raw/optimized/run.log",
    }
    for key, path in paths.items():
        verify_file(bundle / path, result["raw_sha256"][key])
    baseline = next(row for row in load_jsonl(bundle / paths["baseline_progress"])
                    if row["index"] == "1")
    optimized = load_jsonl(bundle / paths["optimized_progress"])[0]
    if normalized_curve(baseline) != normalized_curve(optimized):
        fail("reducer A/B non-timing projection mismatch")
    report = result["p125_index_1"]
    for row, label in ((baseline, "baseline"), (optimized, "optimized")):
        close(int(row["timings_us"]["sea"]) / 1_000_000, report[label]["sea_seconds"])
        close(int(row["timings_us"]["total"]) / 1_000_000,
              report[label]["complete_curve_seconds"])
        for raw_field, output in (("modular_roots_us", "modular_roots_seconds"),
                                  ("eigenvalue_us", "eigenvalue_seconds"),
                                  ("bmss_us", "bmss_seconds"),
                                  ("normalized_codomain_us", "normalized_codomain_seconds")):
            close(sum_level_field(row, raw_field) / 1_000_000, report[label][output],
                  tolerance=1e-3)
    log_text = (bundle / paths["optimized_run_log"]).read_text(encoding="utf-8")
    wall_match = re.search(r"([0-9.]+) real", log_text)
    if not wall_match:
        fail("optimized reducer wall time is absent")
    close(float(wall_match.group(1)), report["optimized"]["invocation_wall_seconds"])
    close(report["baseline"]["complete_curve_seconds"] /
          report["optimized"]["complete_curve_seconds"],
          report["speedup"]["complete_curve"])
    for field, output in (("modular_roots_seconds", "modular_roots"),
                          ("sea_seconds", "sea")):
        close(report["baseline"][field] / report["optimized"][field],
              report["speedup"][output])
    close(report["baseline"]["reported_invocation_wall_seconds"] /
          report["optimized"]["invocation_wall_seconds"],
          report["speedup"]["reported_invocation_wall"])
    synthetic = result["synthetic_degree_301_reported"]
    close(statistics.median(synthetic["baseline_seconds"]) /
          statistics.median(synthetic["optimized_seconds"]), synthetic["speedup"])


def audit_curve_twist() -> None:
    bundle = ARTIFACTS / "p125-curve-twist-workcount-20260801"
    result = load_json(bundle / "result.json")
    rows: list[dict] = []
    for name, expected in result["raw_sha256"].items():
        path = bundle / "raw" / name
        verify_file(path, expected)
        rows.extend(load_jsonl(path))
    rows.sort(key=lambda row: int(row["index"]))
    if [int(row["index"]) for row in rows] != list(range(12)):
        fail("curve/twist retained indices are not exactly [0,12)")
    observed = result["observed"]
    if len(result["raw_sha256"]) != result["source"]["progress_streams"]:
        fail("curve/twist progress-stream count mismatch")
    if len(rows) != observed["curves"]:
        fail("curve/twist curve total mismatch")
    if sum(int(row["sea_passes"]) for row in rows) != observed["sea_executions"]:
        fail("curve/twist SEA-execution total mismatch")
    for row in rows:
        if row["state"]["prime"] != result["source"]["prime"] or \
                row["state"]["seed"] != result["source"]["seed"]:
            fail(f"curve/twist source identity mismatch at index {row['index']}")
    if sum(int(row["initial_trace_count"]) for row in rows) != observed["initial_trace_candidates"]:
        fail("curve/twist trace-candidate total mismatch")
    if 2 * observed["initial_trace_candidates"] != observed["exact_order_screens"]:
        fail("curve/twist order-screen total mismatch")
    if 2 * len(rows) != observed["actual_curve_twist_order_opportunities"]:
        fail("curve/twist actual-opportunity total mismatch")
    if sum(row["status"] == "sound_smoothness_reject" for row in rows) != observed["sound_smoothness_rejections"]:
        fail("curve/twist rejection total mismatch")
    if sum(bool(row["full_point_count"]) for row in rows) != observed["full_point_counts"]:
        fail("curve/twist full-count total mismatch")
    if sum(row["status"] == "verified_certificate" for row in rows) != observed["certificates"]:
        fail("curve/twist certificate total mismatch")


def audit_direct_context_compaction() -> None:
    bundle = ARTIFACTS / "p125-classical-direct-compact-20260803"
    result = load_json(bundle / "result.json")
    if result["schema"] != "oneshotsea.p125-classical-direct-compact-evidence.v1":
        fail("direct-context result schema mismatch")

    environment: dict[str, str] = {}
    for line in (bundle / "environment.txt").read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        key, separator, value = line.partition("=")
        if not separator or key in environment:
            fail(f"malformed direct-context environment line: {line!r}")
        environment[key] = value

    identity = result["implementation_identity"]
    expected_identity = {
        "candidate": {
            "commit": "7c6a997ddd4482e311b27d6bd3f7e1aa93b909e9",
            "tree": "39c73c22ac0ba351e3319b3fb54c58db81bac458",
            "parent": "4f917fcfc2a2dc5d06980e08bef77d9c95c83dbd",
            "benchmark_sha256": "69ea7463af869a1644ef107d166f4d8b3b30a0f2cc125cd3ef699a6986626b56",
            "library_sha256": "aafb4f92decf11da07a341dd4569cf9a336a2f862500962a5f2ffdc06c299ae5",
        },
        "baseline": {
            "commit": "4f917fcfc2a2dc5d06980e08bef77d9c95c83dbd",
            "tree": "6db235641ae0e9d6822afd4af57fc9d3dc2f4689",
            "parent": "0d2136bc4fd6cc12fc4c2795356be23f60af9f41",
            "benchmark_sha256": "e537d9120f414a45104bf3842be70c08a9cf61178aade97d0b690b8bfa8a117d",
            "library_sha256": "9a8ef586e0b8c38facb5cba02e4af9c0421a49c0edb18d9fca3736fe146fb806",
            "compile_define": "ONESHOTSEA_BENCHMARK_LEGACY_CONTEXT=1",
        },
    }
    for label, expected in expected_identity.items():
        if identity[label] != expected:
            fail(f"direct-context {label} implementation identity mismatch")
        for key, value in expected.items():
            environment_key = f"{label}_{key}"
            if key == "compile_define":
                environment_key = "baseline_compile_define"
            if environment.get(environment_key) != value:
                fail(f"direct-context environment identity mismatch: {environment_key}")
        git_meta = subprocess.run(
            ["git", "show", "-s", "--format=%H%n%T%n%P", expected["commit"]],
            cwd=ROOT, check=True, stdout=subprocess.PIPE, text=True,
        ).stdout.splitlines()
        if git_meta != [expected["commit"], expected["tree"], expected["parent"]]:
            fail(f"direct-context Git identity mismatch: {label}")
    source = subprocess.run(
        ["git", "show", f"{expected_identity['candidate']['commit']}:tools/benchmark_p125_classical_direct.cpp"],
        cwd=ROOT, check=True, stdout=subprocess.PIPE,
    ).stdout
    source_hash = hashlib.sha256(source).hexdigest()
    if source_hash != identity["benchmark_source_sha256"] or \
            environment.get("benchmark_source_sha256") != source_hash:
        fail("direct-context benchmark source identity mismatch")

    numeric_fields = (
        "ell", "thread_limit", "preparation_us", "cold_us",
        "warm_distinct_j_us", "validation_us", "auxiliary_prime_count",
        "class_number", "cold_schoof_residue", "warm_schoof_residue",
        "process_peak_rss_bytes",
    )
    matrix_fields = ("matrix_coefficients", "matrix_payload_bytes")

    def validate_row(row: dict, *, compact: bool) -> None:
        if row.get("schema") != result["benchmark_schema"]:
            fail("direct-context benchmark schema mismatch")
        for field in numeric_fields:
            if not isinstance(row.get(field), str) or not re.fullmatch(r"[0-9]+", row[field]):
                fail(f"direct-context numeric-string mismatch: {field}")
        for field in matrix_fields:
            value = row.get(field)
            if compact:
                if not isinstance(value, str) or not re.fullmatch(r"[0-9]+", value):
                    fail(f"direct-context compact matrix field mismatch: {field}")
            elif value is not None:
                fail(f"direct-context baseline matrix field must be null: {field}")
        if not isinstance(row.get("cold_exact"), bool) or \
                not isinstance(row.get("warm_exact"), bool):
            fail("direct-context exactness flag mismatch")

    parallel = result["same_binary_parallel"]
    rows = load_jsonl(bundle / parallel["raw"])
    if len(rows) != parallel["record_count"]:
        fail("direct-context same-binary record-count mismatch")
    expected_order = [pair for _ in range(parallel["bracket_count"])
                      for pair in parallel["order_per_bracket"]]
    observed_order = [[int(row["ell"]), int(row["thread_limit"])] for row in rows]
    if observed_order != expected_order:
        fail("direct-context same-binary bracket order mismatch")
    static = {
        7: {"auxiliary_prime_count": 37, "class_number": 12,
            "matrix_coefficients": 5994, "matrix_payload_bytes": 47952,
            "cold_exact": True, "warm_exact": False,
            "cold_schoof_residue": 5, "warm_schoof_residue": 6},
        11: {"auxiliary_prime_count": 43, "class_number": 36,
             "matrix_coefficients": 14534, "matrix_payload_bytes": 116272,
             "cold_exact": True, "warm_exact": True,
             "cold_schoof_residue": 10, "warm_schoof_residue": 6},
    }
    median_fields = ("preparation_us", "cold_us", "warm_distinct_j_us",
                     "process_peak_rss_bytes")
    for row in rows:
        validate_row(row, compact=True)
        ell = int(row["ell"])
        for field, expected in static[ell].items():
            actual = row[field] if isinstance(expected, bool) else int(row[field])
            if actual != expected:
                fail(f"direct-context same-binary static mismatch: ell={ell}/{field}")
        expected_coefficients = 2 * int(row["auxiliary_prime_count"]) * (ell + 2) ** 2
        if int(row["matrix_coefficients"]) != expected_coefficients or \
                int(row["matrix_payload_bytes"]) != 8 * expected_coefficients:
            fail(f"direct-context matrix-size formula mismatch: ell={ell}")
    for ell in (7, 11):
        report = parallel["levels"][str(ell)]
        for field, expected in static[ell].items():
            if report[field] != expected:
                fail(f"direct-context reported static mismatch: ell={ell}/{field}")
        groups: dict[int, list[dict]] = {
            threads: [row for row in rows
                      if int(row["ell"]) == ell and int(row["thread_limit"]) == threads]
            for threads in (1, 4)
        }
        if any(len(group) != 6 for group in groups.values()):
            fail(f"direct-context sample multiplicity mismatch: ell={ell}")
        medians: dict[int, dict[str, float]] = {}
        for threads, group in groups.items():
            medians[threads] = {
                field: statistics.median(int(row[field]) for row in group)
                for field in median_fields
            }
            reported = report["serial_medians" if threads == 1 else "four_worker_medians"]
            for field in median_fields:
                close(medians[threads][field], reported[field])
        close(medians[1]["preparation_us"] / medians[4]["preparation_us"],
              report["preparation_speedup"])
        close(medians[1]["cold_us"] / medians[4]["cold_us"], report["cold_speedup"])
        close(1 - medians[4]["warm_distinct_j_us"] / medians[4]["cold_us"],
              report["four_worker_warm_vs_cold_reduction_fraction"])
        close(medians[4]["process_peak_rss_bytes"] / medians[1]["process_peak_rss_bytes"],
              report["four_worker_over_serial_peak_rss_ratio"])

    bracket = result["compaction_bracket"]
    rows = load_jsonl(bundle / bracket["raw"])
    if len(rows) != bracket["record_count"]:
        fail("direct-context compaction-bracket record-count mismatch")
    observed_order = [["candidate" if row["matrix_coefficients"] is not None else "baseline",
                       int(row["ell"])] for row in rows]
    if observed_order != bracket["order"]:
        fail("direct-context compaction-bracket order mismatch")
    bracket_static = {
        13: {"auxiliary_prime_count": 45, "class_number": 36,
             "matrix_coefficients": 20250, "matrix_payload_bytes": 162000,
             "cold_exact": False, "warm_exact": True,
             "cold_schoof_residue": 0, "warm_schoof_residue": 5},
        29: {"auxiliary_prime_count": 76, "class_number": 36,
             "matrix_coefficients": 146072, "matrix_payload_bytes": 1168576,
             "cold_exact": True, "warm_exact": True,
             "cold_schoof_residue": 23, "warm_schoof_residue": 12},
    }
    for row in rows:
        candidate = row["matrix_coefficients"] is not None
        validate_row(row, compact=candidate)
        if int(row["thread_limit"]) != 4:
            fail("direct-context compaction bracket did not use four workers")
        ell = int(row["ell"])
        expected = bracket_static[ell]
        for field in ("auxiliary_prime_count", "class_number", "cold_exact",
                      "warm_exact", "cold_schoof_residue", "warm_schoof_residue"):
            actual = row[field] if isinstance(expected[field], bool) else int(row[field])
            if actual != expected[field]:
                fail(f"direct-context compaction static mismatch: ell={ell}/{field}")
        if candidate:
            if int(row["matrix_coefficients"]) != expected["matrix_coefficients"] or \
                    int(row["matrix_payload_bytes"]) != expected["matrix_payload_bytes"]:
                fail(f"direct-context compaction matrix mismatch: ell={ell}")
            if expected["matrix_payload_bytes"] != 8 * 2 * expected["auxiliary_prime_count"] * (ell + 2) ** 2:
                fail(f"direct-context compaction formula mismatch: ell={ell}")
    for ell in (13, 29):
        report = bracket["levels"][str(ell)]
        candidate_rows = [row for row in rows
                          if int(row["ell"]) == ell and row["matrix_coefficients"] is not None]
        baseline_rows = [row for row in rows
                         if int(row["ell"]) == ell and row["matrix_coefficients"] is None]
        if len(candidate_rows) != report["candidate_samples"] or \
                len(baseline_rows) != report["baseline_samples"]:
            fail(f"direct-context compaction sample multiplicity mismatch: ell={ell}")
        candidate_rss = statistics.median(int(row["process_peak_rss_bytes"])
                                          for row in candidate_rows)
        baseline_rss = statistics.median(int(row["process_peak_rss_bytes"])
                                         for row in baseline_rows)
        candidate_warm = statistics.median(int(row["warm_distinct_j_us"])
                                           for row in candidate_rows)
        baseline_warm = statistics.median(int(row["warm_distinct_j_us"])
                                          for row in baseline_rows)
        close(candidate_rss, report["candidate_median_peak_rss_bytes"])
        close(baseline_rss, report["baseline_median_peak_rss_bytes"])
        close(1 - candidate_rss / baseline_rss, report["peak_rss_reduction_fraction"])
        close(candidate_warm, report["candidate_median_warm_distinct_j_us"])
        close(baseline_warm, report["baseline_median_warm_distinct_j_us"])
        close(baseline_warm / candidate_warm, report["warm_speedup"])
        if report["matrix_coefficients"] != bracket_static[ell]["matrix_coefficients"] or \
                report["matrix_payload_bytes"] != bracket_static[ell]["matrix_payload_bytes"]:
            fail(f"direct-context compaction reported matrix mismatch: ell={ell}")
    if result["claim_boundary"]["cold_cross_commit_speedup_claimed"] is not False:
        fail("direct-context artifact must not claim a cross-commit cold speedup")


def main() -> None:
    for bundle in BUNDLES:
        audit_checksums(bundle)
    audit_tracked_files()
    audit_trace_caps()
    audit_family()
    audit_curve_parallel()
    audit_prime_schedule()
    audit_root_orbits()
    audit_reducer()
    audit_curve_twist()
    audit_direct_context_compaction()
    print(f"performance artifact audit ok: {len(BUNDLES)} bundles")


if __name__ == "__main__":
    main()
