#!/usr/bin/env python3
"""Validate and summarize a retained p125 Poly B/A/A/B benchmark bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re


PHASES = ("b1", "a1", "a2", "b2")
MODES = ("frobenius-194", "frobenius-281", "frobenius-401", "sea")
SHA256 = re.compile(r"[0-9a-f]{64}")
GIT_COMMIT = re.compile(r"[0-9a-f]{40}")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _key_values(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key in values:
            raise ValueError(f"duplicate key {key!r} in {path}:{number}")
        values[key] = value
    return values


def _verify_checksums(directory: Path) -> int:
    checksum_path = directory / "SHA256SUMS"
    records = 0
    recorded_paths: set[Path] = set()
    for number, line in enumerate(checksum_path.read_text().splitlines(), 1):
        fields = line.split(maxsplit=1)
        if len(fields) != 2 or SHA256.fullmatch(fields[0]) is None:
            raise ValueError(f"malformed SHA256SUMS line {number}")
        relative = fields[1][1:] if fields[1].startswith("*") else fields[1]
        path = Path(relative)
        if path.is_absolute() or ".." in path.parts:
            raise ValueError(f"unsafe SHA256SUMS path on line {number}")
        target = directory / path
        normalized = Path(*[part for part in path.parts if part != "."])
        if normalized in recorded_paths:
            raise ValueError(f"duplicate SHA256SUMS path on line {number}")
        recorded_paths.add(normalized)
        if not target.is_file() or target.is_symlink():
            raise ValueError(f"missing or non-regular checksummed file: {path}")
        if _sha256(target) != fields[0]:
            raise ValueError(f"checksum mismatch: {path}")
        records += 1
    if records == 0:
        raise ValueError("empty SHA256SUMS")
    actual_paths = {
        path.relative_to(directory)
        for path in directory.iterdir()
        if path.name != "SHA256SUMS"
    }
    if recorded_paths != actual_paths:
        raise ValueError("SHA256SUMS does not cover the exact bundle file set")
    return records


def _timing_us(path: Path, mode: str) -> tuple[int, dict[str, int]]:
    values = _key_values(path)
    if values.get("timing.schema") != "oneshotsea.p125-poly-trusted-timing.v1":
        raise ValueError(f"wrong timing schema: {path}")
    expected_mode = "sea" if mode == "sea" else "frobenius"
    if values.get("timing.mode") != expected_mode:
        raise ValueError(f"wrong timing mode: {path}")
    numeric = {
        key[len("timing."):]: int(value)
        for key, value in values.items()
        if key.startswith("timing.") and key not in {"timing.schema", "timing.mode"}
    }
    primary = numeric["sea_us"] if mode == "sea" else numeric["elapsed_us"]
    if primary <= 0:
        raise ValueError(f"nonpositive primary timing: {path}")
    return primary, numeric


def _maximum_rss_kib(path: Path) -> int:
    prefix = "Maximum resident set size (kbytes):"
    matches = [
        line.lstrip()[len(prefix):].strip()
        for line in path.read_text().splitlines()
        if line.lstrip().startswith(prefix)
    ]
    if len(matches) != 1 or int(matches[0]) <= 0:
        raise ValueError(f"missing or invalid maximum RSS: {path}")
    return int(matches[0])


def summarize(directory: Path) -> dict[str, object]:
    checksum_records = _verify_checksums(directory)
    environment = _key_values(directory / "ENVIRONMENT.txt")
    if environment.get("schema") != "oneshotsea.p125-poly-isolated-ab.v1":
        raise ValueError("wrong environment schema")
    for filename, key in (
        ("baseline.bin", "retained_baseline_sha256"),
        ("candidate.bin", "retained_candidate_sha256"),
        ("BUILD_COMMANDS.sh", "build_commands_sha256"),
        ("BUILD.log", "build_log_sha256"),
    ):
        if environment.get(key) != _sha256(directory / filename):
            raise ValueError(f"retained identity mismatch: {filename}")
    if GIT_COMMIT.fullmatch(environment.get("source_commit", "")) is None:
        raise ValueError("invalid source commit identity")
    if environment.get("source_tracked_diff_clean") != "true":
        raise ValueError("benchmark source was not recorded clean")

    projection_file = {}
    for number, line in enumerate(
        (directory / "PROJECTION_SHA256.txt").read_text().splitlines(), 1
    ):
        fields = line.split()
        if (len(fields) != 2 or fields[0] in projection_file or
                SHA256.fullmatch(fields[1]) is None):
            raise ValueError(f"malformed projection digest line {number}")
        projection_file[fields[0]] = fields[1]
    if set(projection_file) != set(MODES):
        raise ValueError("projection digest mode set is incomplete")

    modes: dict[str, object] = {}
    candidate_rss_peak = 0
    baseline_rss_peak = 0
    pooled_baseline_frobenius = 0
    pooled_candidate_frobenius = 0
    for mode in MODES:
        primary: dict[str, int] = {}
        details: dict[str, dict[str, int]] = {}
        rss: dict[str, int] = {}
        projections: dict[str, str] = {}
        for phase in PHASES:
            stem = f"{phase}-{mode}"
            primary[phase], details[phase] = _timing_us(
                directory / f"{stem}.timing.stderr", mode
            )
            rss[phase] = _maximum_rss_kib(directory / f"{stem}.resource.txt")
            projections[phase] = _sha256(directory / f"{stem}.stdout")
        if len(set(projections.values())) != 1:
            raise ValueError(f"semantic projection mismatch: {mode}")
        projection = next(iter(projections.values()))
        if projection_file[mode] != projection:
            raise ValueError(f"recorded projection digest mismatch: {mode}")

        baseline = [primary["b1"], primary["b2"]]
        candidate = [primary["a1"], primary["a2"]]
        baseline_mean = sum(baseline) / 2
        candidate_mean = sum(candidate) / 2
        paired_speedups = [baseline[0] / candidate[0], baseline[1] / candidate[1]]
        speedup = baseline_mean / candidate_mean
        candidate_rss_peak = max(candidate_rss_peak, rss["a1"], rss["a2"])
        baseline_rss_peak = max(baseline_rss_peak, rss["b1"], rss["b2"])
        if mode.startswith("frobenius-"):
            pooled_baseline_frobenius += sum(baseline)
            pooled_candidate_frobenius += sum(candidate)
        modes[mode] = {
            "primary_timing_us": primary,
            "timing_details_us": details,
            "maximum_rss_kib": rss,
            "baseline_mean_us": baseline_mean,
            "candidate_mean_us": candidate_mean,
            "speedup": speedup,
            "paired_speedups": paired_speedups,
            "projection_sha256": projection,
        }

    pooled_speedup = pooled_baseline_frobenius / pooled_candidate_frobenius
    sea_speedup = modes["sea"]["speedup"]
    rss_ratio = candidate_rss_peak / baseline_rss_peak
    label = environment.get("label")
    gating_modes = ("sea",) if label == "quotient-context" else MODES
    every_gating_pair_improves = all(
        all(value > 1.0 for value in modes[mode]["paired_speedups"])
        for mode in gating_modes
    )
    every_gating_mean_improves = all(
        modes[mode]["speedup"] > 1.0 for mode in gating_modes
    )
    pooled_frobenius_gate_applicable = gating_modes != ("sea",)
    gates = {
        "semantic_projections_identical": True,
        "every_paired_ratio_above_one": every_gating_pair_improves,
        "every_mode_mean_improves": every_gating_mean_improves,
        "pooled_frobenius_speedup_above_1_05": (
            pooled_speedup > 1.05
            if pooled_frobenius_gate_applicable else True
        ),
        "sea_speedup_above_1_05": sea_speedup > 1.05,
        "candidate_rss_increase_at_most_5_percent": rss_ratio <= 1.05,
    }
    gates["accepted"] = all(gates.values())
    return {
        "schema": "oneshotsea.p125-poly-isolated-ab-summary.v1",
        "label": label,
        "gating_modes": list(gating_modes),
        "pooled_frobenius_gate_applicable": pooled_frobenius_gate_applicable,
        "checksum_records_verified": checksum_records,
        "environment": environment,
        "modes": modes,
        "pooled_frobenius_speedup": pooled_speedup,
        "baseline_peak_rss_kib": baseline_rss_peak,
        "candidate_peak_rss_kib": candidate_rss_peak,
        "candidate_to_baseline_peak_rss_ratio": rss_ratio,
        "gates": gates,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    result = summarize(arguments.bundle)
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if arguments.output is None:
        print(encoded, end="")
    else:
        arguments.output.write_text(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
