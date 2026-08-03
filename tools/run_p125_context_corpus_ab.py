#!/usr/bin/env python3
"""Run an exact p125 SEA corpus A/B for prepared quotient-context reuse.

The script builds two binaries from one source tree, changing only
ONESHOTSEA_QUOTIENT_CONTEXT_REUSE.  It runs every deterministic X1(27) curve in
B/A/A/B order, compares complete mathematical projections byte-for-byte, and
writes compact JSON plus retained raw outputs.
"""

from __future__ import annotations

import argparse
import filecmp
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from statistics import mean
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INDICES = (1000030, 1000031, 1000032)
TIMING_FIELDS = (
    "generation_us",
    "sea_us",
    "total_us",
    "source_lifts_us",
    "modular_roots_us",
    "normalized_codomain_us",
    "bmss_us",
    "eigenvalue_us",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run_checked(command: list[str], *, cwd: Path = ROOT,
                stdout: Any = None, stderr: Any = None) -> None:
    subprocess.run(command, cwd=cwd, check=True, stdout=stdout, stderr=stderr)


def parse_indices(encoded: str) -> list[int]:
    values: list[int] = []
    for item in encoded.split(","):
        item = item.strip()
        if not item:
            continue
        value = int(item, 10)
        if value < 0 or value > (1 << 64) - 1:
            raise argparse.ArgumentTypeError("indices must be unsigned 64-bit integers")
        values.append(value)
    if not values:
        raise argparse.ArgumentTypeError("at least one index is required")
    if len(set(values)) != len(values):
        raise argparse.ArgumentTypeError("indices must be distinct")
    return values


def default_toolchain() -> tuple[str, str, str, str]:
    system = platform.system()
    cc = os.environ.get("CC", "clang" if system == "Darwin" else "gcc")
    cxx = os.environ.get("CXX", "clang++" if system == "Darwin" else "g++")
    if "CPPFLAGS" in os.environ:
        cppflags = os.environ["CPPFLAGS"]
    elif system == "Darwin":
        gmp_prefix = os.environ.get("GMP_PREFIX", "/opt/homebrew/opt/gmp")
        cppflags = f"-Iinclude -isystem {gmp_prefix}/include"
    else:
        cppflags = "-Iinclude"
    if "LDFLAGS" in os.environ:
        ldflags = os.environ["LDFLAGS"]
    elif system == "Darwin":
        gmp_prefix = os.environ.get("GMP_PREFIX", "/opt/homebrew/opt/gmp")
        ldflags = f"-L{gmp_prefix}/lib"
    else:
        ldflags = ""
    return cc, cxx, cppflags, ldflags


def build_variant(*, label: str, macro_value: int, build_dir: Path,
                  jobs: int, cc: str, cxx: str, cppflags: str,
                  ldflags: str, output_dir: Path) -> tuple[Path, list[str]]:
    if build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.parent.mkdir(parents=True, exist_ok=True)
    binary = build_dir / "benchmark_p125_poly_trusted"
    command = [
        "make",
        f"-j{jobs}",
        f"BUILD_DIR={build_dir}",
        f"CC={cc}",
        f"CXX={cxx}",
        f"CPPFLAGS={cppflags} -DONESHOTSEA_QUOTIENT_CONTEXT_REUSE={macro_value}",
        f"LDFLAGS={ldflags}",
        str(binary),
    ]
    build_log = output_dir / f"build-{label}.log"
    with build_log.open("wb") as stream:
        run_checked(command, stdout=stream, stderr=subprocess.STDOUT)
    if not binary.is_file():
        raise RuntimeError(f"build did not create {binary}")
    return binary, command


def parse_key_values(path: Path, prefix: str) -> dict[str, Any]:
    values: dict[str, Any] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        if not raw_line.startswith(prefix):
            continue
        key, encoded = raw_line.split("=", 1)
        key = key.removeprefix(prefix)
        if re.fullmatch(r"-?[0-9]+", encoded):
            values[key] = int(encoded)
        elif re.fullmatch(r"-?[0-9]+(?:\.[0-9]+)?", encoded):
            values[key] = float(encoded)
        else:
            values[key] = encoded
    return values


def timed_command(command: list[str], *, stdout_path: Path,
                  stderr_path: Path, resource_path: Path) -> None:
    system = platform.system()
    time_binary = Path("/usr/bin/time")
    wrapped = command
    if time_binary.is_file() and system == "Linux":
        wrapped = [
            str(time_binary), "-f",
            "resource.wall_s=%e\\nresource.user_s=%U\\nresource.sys_s=%S\\nresource.maxrss_kb=%M",
            "-o", str(resource_path), *command,
        ]
    elif time_binary.is_file() and system == "Darwin":
        wrapped = [str(time_binary), "-p", "-l", "-o", str(resource_path), *command]

    started = time.perf_counter()
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        run_checked(wrapped, stdout=stdout, stderr=stderr)
    elapsed = time.perf_counter() - started
    if not resource_path.exists():
        resource_path.write_text(
            f"resource.wall_s={elapsed:.9f}\n", encoding="utf-8"
        )


def parse_resource(path: Path) -> dict[str, Any]:
    text = path.read_text(encoding="utf-8")
    direct = parse_key_values(path, "resource.")
    if direct:
        return direct
    result: dict[str, Any] = {}
    for key, value in re.findall(r"^(real|user|sys)\s+([0-9.]+)$", text,
                                 re.MULTILINE):
        result[f"{key}_s"] = float(value)
    maximum = re.search(r"^\s*([0-9]+)\s+maximum resident set size$", text,
                        re.MULTILINE)
    if maximum:
        result["maxrss_bytes"] = int(maximum.group(1))
    return result


def run_case(*, label: str, sequence: int, binary: Path, table_dir: Path,
             max_level: int, index: int, cpu: int | None,
             raw_dir: Path) -> dict[str, Any]:
    stem = f"index-{index}-sequence-{sequence}-{label}"
    stdout_path = raw_dir / f"{stem}.projection"
    stderr_path = raw_dir / f"{stem}.timing"
    resource_path = raw_dir / f"{stem}.resource"
    command: list[str] = [
        str(binary), "sea", str(table_dir), str(max_level), str(index)
    ]
    if cpu is not None:
        taskset = shutil.which("taskset")
        if taskset is None:
            raise RuntimeError("--cpu requires taskset on this host")
        command = [taskset, "-c", str(cpu), *command]
    timed_command(command, stdout_path=stdout_path, stderr_path=stderr_path,
                  resource_path=resource_path)
    timing = parse_key_values(stderr_path, "timing.")
    missing = [field for field in TIMING_FIELDS if field not in timing]
    if missing:
        raise RuntimeError(f"missing timing fields in {stderr_path}: {missing}")
    return {
        "label": label,
        "sequence": sequence,
        "timing": timing,
        "resource": parse_resource(resource_path),
        "projection": {
            "path": stdout_path.relative_to(raw_dir.parent).as_posix(),
            "sha256": sha256(stdout_path),
            "bytes": stdout_path.stat().st_size,
        },
        "timing_file": {
            "path": stderr_path.relative_to(raw_dir.parent).as_posix(),
            "sha256": sha256(stderr_path),
            "bytes": stderr_path.stat().st_size,
        },
        "resource_file": {
            "path": resource_path.relative_to(raw_dir.parent).as_posix(),
            "sha256": sha256(resource_path),
            "bytes": resource_path.stat().st_size,
        },
    }


def metric_summary(off: list[dict[str, Any]], on: list[dict[str, Any]]) -> dict[str, Any]:
    summary: dict[str, Any] = {}
    for field in TIMING_FIELDS:
        off_mean = mean(float(run["timing"][field]) for run in off)
        on_mean = mean(float(run["timing"][field]) for run in on)
        summary[field] = {
            "off_mean": off_mean,
            "on_mean": on_mean,
            "speedup": None if on_mean == 0 else off_mean / on_mean,
        }
    return summary


def command_output(command: list[str]) -> str:
    completed = subprocess.run(command, check=True, text=True,
                               stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    return completed.stdout.strip()


def main() -> int:
    cc_default, cxx_default, cppflags_default, ldflags_default = default_toolchain()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--table-dir", type=Path,
                        default=ROOT / "data/modpoly/weber_f")
    parser.add_argument("--indices", type=parse_indices,
                        default=list(DEFAULT_INDICES),
                        help="comma-separated deterministic global indices")
    parser.add_argument("--max-level", type=int, default=277)
    parser.add_argument("--output-dir", type=Path,
                        default=ROOT / "work/p125-context-corpus-ab")
    parser.add_argument("--build-root", type=Path,
                        default=ROOT / "build/context-corpus-ab")
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1))
    parser.add_argument("--cpu", type=int,
                        help="Linux taskset CPU used for every timed run")
    parser.add_argument("--cc", default=cc_default)
    parser.add_argument("--cxx", default=cxx_default)
    parser.add_argument("--cppflags", default=cppflags_default)
    parser.add_argument("--ldflags", default=ldflags_default)
    parser.add_argument("--source-commit",
                        help="explicit commit identity when git metadata is unavailable")
    parser.add_argument("--off-binary", type=Path,
                        help="reuse a prebuilt context-off benchmark binary")
    parser.add_argument("--on-binary", type=Path,
                        help="reuse a prebuilt context-on benchmark binary")
    args = parser.parse_args()

    if args.max_level < 5:
        parser.error("--max-level must be at least 5")
    if args.jobs <= 0:
        parser.error("--jobs must be positive")
    table_dir = args.table_dir.resolve()
    if not table_dir.is_dir():
        parser.error(f"table directory does not exist: {table_dir}")

    output_dir = args.output_dir.resolve()
    raw_dir = output_dir / "raw"
    if output_dir.exists():
        shutil.rmtree(output_dir)
    raw_dir.mkdir(parents=True)

    if (args.off_binary is None) != (args.on_binary is None):
        parser.error("--off-binary and --on-binary must be supplied together")
    if args.off_binary is not None:
        off_binary = args.off_binary.resolve()
        on_binary = args.on_binary.resolve()
        if not off_binary.is_file() or not on_binary.is_file():
            parser.error("prebuilt A/B binaries must both be regular files")
        off_build = ["prebuilt", str(off_binary)]
        on_build = ["prebuilt", str(on_binary)]
        (output_dir / "build-context-off.log").write_text(
            f"prebuilt binary: {off_binary}\n", encoding="utf-8")
        (output_dir / "build-context-on.log").write_text(
            f"prebuilt binary: {on_binary}\n", encoding="utf-8")
    else:
        off_binary, off_build = build_variant(
            label="context-off", macro_value=0,
            build_dir=(args.build_root / "context-off").resolve(), jobs=args.jobs,
            cc=args.cc, cxx=args.cxx, cppflags=args.cppflags,
            ldflags=args.ldflags, output_dir=output_dir,
        )
        on_binary, on_build = build_variant(
            label="context-on", macro_value=1,
            build_dir=(args.build_root / "context-on").resolve(), jobs=args.jobs,
            cc=args.cc, cxx=args.cxx, cppflags=args.cppflags,
            ldflags=args.ldflags, output_dir=output_dir,
        )

    source_commit = args.source_commit
    if source_commit is None and (ROOT / ".git").exists():
        source_commit = command_output(["git", "rev-parse", "HEAD"])

    result: dict[str, Any] = {
        "schema": "oneshotsea.p125-context-corpus-ab.v1",
        "created_at": datetime.now(timezone.utc).isoformat(),
        "source_commit": source_commit,
        "host": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "processor": platform.processor(),
            "python": sys.version.split()[0],
            "cc": command_output([args.cc, "--version"]).splitlines()[0],
            "cxx": command_output([args.cxx, "--version"]).splitlines()[0],
            "cpu_affinity": args.cpu,
        },
        "configuration": {
            "prime": "nextprime(10^125)",
            "indices": args.indices,
            "max_level": args.max_level,
            "trace_cap": 16,
            "sea_threads": 1,
            "table_directory": str(table_dir),
            "run_order_per_index": ["off", "on", "on", "off"],
        },
        "source_sha256": {
            path: sha256(ROOT / path)
            for path in (
                "src/poly.cpp", "src/schoof.cpp",
                "include/oneshotsea/poly.hpp",
                "tools/benchmark_p125_poly_trusted.cpp", "Makefile",
            )
        },
        "builds": {
            "off": {
                "macro": "ONESHOTSEA_QUOTIENT_CONTEXT_REUSE=0",
                "command": off_build,
                "binary": str(off_binary),
                "sha256": sha256(off_binary),
            },
            "on": {
                "macro": "ONESHOTSEA_QUOTIENT_CONTEXT_REUSE=1",
                "command": on_build,
                "binary": str(on_binary),
                "sha256": sha256(on_binary),
            },
        },
        "indices": {},
        "all_projections_identical": True,
    }

    aggregate_off: list[dict[str, Any]] = []
    aggregate_on: list[dict[str, Any]] = []
    for index in args.indices:
        ordered = (
            ("off", 1, off_binary),
            ("on", 1, on_binary),
            ("on", 2, on_binary),
            ("off", 2, off_binary),
        )
        runs: list[dict[str, Any]] = []
        for label, sequence, binary in ordered:
            print(f"running index={index} max_level={args.max_level} "
                  f"variant={label} sequence={sequence}", flush=True)
            runs.append(run_case(
                label=label, sequence=sequence, binary=binary,
                table_dir=table_dir, max_level=args.max_level, index=index,
                cpu=args.cpu, raw_dir=raw_dir,
            ))
        projection_paths = [
            raw_dir.parent / run["projection"]["path"] for run in runs
        ]
        reference_projection = projection_paths[0]
        identical = all(
            run["projection"]["sha256"] == runs[0]["projection"]["sha256"]
            and filecmp.cmp(reference_projection, path, shallow=False)
            for run, path in zip(runs[1:], projection_paths[1:])
        )
        if not identical:
            result["all_projections_identical"] = False
            raise RuntimeError(f"projection mismatch for global index {index}")
        off_runs = [run for run in runs if run["label"] == "off"]
        on_runs = [run for run in runs if run["label"] == "on"]
        aggregate_off.extend(off_runs)
        aggregate_on.extend(on_runs)
        result["indices"][str(index)] = {
            "projection_sha256": runs[0]["projection"]["sha256"],
            "projection_bytes": runs[0]["projection"]["bytes"],
            "runs": runs,
            "summary": metric_summary(off_runs, on_runs),
        }

    result["aggregate_summary"] = metric_summary(aggregate_off, aggregate_on)
    result["limitations"] = [
        "This measures SEA arithmetic through the selected level, not the 5.4 GB exact-smooth scan or certificate-yield probability.",
        "CPU frequency, host contention, and thermal state remain timing noise; reverse-order paired runs reduce but do not eliminate it.",
        "The compile-time off path is retained only for exact reproducible ablation.",
    ]
    result_path = output_dir / "result.json"
    result_path.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    checksum_lines = []
    for path in sorted(output_dir.rglob("*")):
        if path.is_file() and path.name != "SHA256SUMS":
            checksum_lines.append(
                f"{sha256(path)}  ./{path.relative_to(output_dir).as_posix()}"
            )
    (output_dir / "SHA256SUMS").write_text(
        "\n".join(checksum_lines) + "\n", encoding="utf-8"
    )
    print(json.dumps(result["aggregate_summary"], indent=2, sort_keys=True))
    print(f"result: {result_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
