#!/usr/bin/env python3
"""Validate and launch one immutable OneShotSEA worker on an EC2 host."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
from datetime import datetime, timezone

MAX_U64 = (1 << 64) - 1
SAFE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
HEX_40 = re.compile(r"^[0-9a-f]{40}$")
HEX_64 = re.compile(r"^[0-9a-f]{64}$")


def fail(message: str) -> "NoReturn":
    raise SystemExit(f"error: {message}")


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def within(path: Path, root: Path, label: str) -> Path:
    resolved = path.resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError:
        fail(f"{label} escapes its required root")
    return resolved


def copy_authenticated(source: Path, destination: Path, label: str) -> None:
    if not source.is_file() or source.is_symlink():
        fail(f"{label} is missing or is not a regular file")
    source_digest = digest(source)
    if destination.exists():
        if destination.is_symlink() or not destination.is_file():
            fail(f"retained {label} is not a regular file")
        if digest(destination) != source_digest:
            fail(f"retained {label} changed")
        return
    temporary = destination.with_name(f"{destination.name}.tmp.{os.getpid()}")
    with source.open("rb") as reader, temporary.open("xb") as writer:
        shutil.copyfileobj(reader, writer, 1024 * 1024)
    temporary.replace(destination)


def partition(start: int, end: int, worker: int, workers: int) -> tuple[int, int]:
    width, remainder = divmod(end - start, workers)
    count = width + int(worker < remainder)
    assigned_start = start + worker * width + min(worker, remainder)
    return assigned_start, assigned_start + count


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser()
    result.add_argument("--root", type=Path, required=True)
    result.add_argument("--instance-id", required=True)
    result.add_argument("--run-id", required=True)
    result.add_argument("--run-kind", choices=("benchmark", "production"), required=True)
    result.add_argument("--prime", type=int, required=True)
    result.add_argument("--worker-id", type=int, required=True)
    result.add_argument("--worker-count", type=int, required=True)
    result.add_argument("--range-start", type=int, required=True)
    result.add_argument("--range-end", type=int, required=True)
    result.add_argument("--seed", type=int, required=True)
    result.add_argument("--max-level", type=int, required=True)
    result.add_argument("--sea-threads", type=int, required=True)
    result.add_argument("--table-dir", required=True)
    result.add_argument("--smooth-cache", type=Path, required=True)
    result.add_argument("--smooth-cache-sha256", required=True)
    result.add_argument("--curve-family", choices=("weber-f", "x1-11", "x1-27"))
    result.add_argument("--x1-require-point4", type=int)
    result.add_argument(
        "--sea-strategy", choices=("weber-first", "direct-first"),
        default="weber-first",
    )
    result.add_argument("--classical-direct-levels")
    result.add_argument("--classical-direct-max-prime-candidates", type=int)
    result.add_argument("--classical-direct-max-x-candidates", type=int)
    result.add_argument("--classical-direct-context-cache", type=Path)
    result.add_argument("--classical-direct-context-sha256")
    result.add_argument("--classical-direct-context-max-file-bytes", type=int)
    result.add_argument("--classical-direct-cache-resident-bytes", type=int)
    result.add_argument("--curve-threads", type=int)
    result.add_argument("--sea-level-telemetry", type=int)
    result.add_argument("--schoof-fallback", type=int)
    result.add_argument("--skip-incomplete-curves", type=int)
    result.add_argument("--smooth-coordinators", type=int)
    result.add_argument("--max-curves", type=int)
    result.add_argument("--checkpoint-every", type=int)
    result.add_argument("--trace-cap", type=int)
    result.add_argument("--smooth-threads", type=int)
    result.add_argument("--smooth-max-batch", type=int)
    result.add_argument("--smooth-root-auxiliary-bytes", type=int)
    result.add_argument("--smooth-build-segment-span", type=int)
    result.add_argument("--assembly-attempts", type=int)
    result.add_argument("--max-certificate-candidates", type=int)
    result.add_argument("--max-candidate-search-nodes", type=int)
    result.add_argument("--wall-time-limit-seconds", type=int, default=0)
    result.add_argument("--resume", action="store_true")
    return result


def main() -> int:
    args = parser().parse_args()
    if not re.fullmatch(r"i-[0-9a-f]{8,17}", args.instance_id):
        fail("invalid instance id")
    if not SAFE_ID.fullmatch(args.run_id):
        fail("invalid run id")
    integer_values = (
        args.prime,
        args.worker_id,
        args.worker_count,
        args.range_start,
        args.range_end,
        args.seed,
        args.max_level,
        args.sea_threads,
        args.wall_time_limit_seconds,
    )
    if any(value < 0 or value > MAX_U64 for value in integer_values):
        fail("search integer exceeds the unsigned 64-bit CLI limit")
    if args.prime <= 0 or args.worker_count <= 0 or not 0 <= args.worker_id < args.worker_count:
        fail("invalid prime or worker identity")
    if not 0 <= args.range_start < args.range_end <= MAX_U64:
        fail("invalid global range")
    if args.max_level < 5 or args.sea_threads <= 0:
        fail("max-level must be at least 5 and sea-threads must be positive")
    curve_threads = args.curve_threads if args.curve_threads is not None else 1
    smooth_coordinators = (
        args.smooth_coordinators if args.smooth_coordinators is not None else 0
    )
    if curve_threads <= 0 or curve_threads > MAX_U64:
        fail("invalid --curve-threads")
    if not 0 <= smooth_coordinators <= min(curve_threads, MAX_U64):
        fail("invalid --smooth-coordinators")
    boolean_options = (
        ("x1-require-point4", args.x1_require_point4),
        ("sea-level-telemetry", args.sea_level_telemetry),
        ("schoof-fallback", args.schoof_fallback),
        ("skip-incomplete-curves", args.skip_incomplete_curves),
    )
    for name, value in boolean_options:
        if value is not None and value not in (0, 1):
            fail(f"invalid --{name}")
    if (
        args.x1_require_point4 == 1
        and (args.curve_family is None or args.curve_family == "weber-f")
    ):
        fail("--x1-require-point4 requires an X1 curve family")
    if not HEX_64.fullmatch(args.smooth_cache_sha256):
        fail("invalid trusted smooth-cache digest")
    if args.max_curves is None or not 0 < args.max_curves <= MAX_U64:
        fail("--max-curves must be positive")
    if args.run_kind == "production" and not args.wall_time_limit_seconds:
        fail("production runs require wall time for artifact-fetch margin")

    direct_values = (
        args.classical_direct_levels,
        args.classical_direct_max_prime_candidates,
        args.classical_direct_max_x_candidates,
        args.classical_direct_context_cache,
        args.classical_direct_context_sha256,
        args.classical_direct_context_max_file_bytes,
        args.classical_direct_cache_resident_bytes,
    )
    direct_enabled = args.sea_strategy == "direct-first"
    if not direct_enabled and any(value is not None for value in direct_values):
        fail("classical direct options require --sea-strategy direct-first")
    direct_levels: list[int] = []
    if direct_enabled:
        if any(value is None for value in direct_values):
            fail("--sea-strategy direct-first requires every direct-cache option")
        assert args.classical_direct_levels is not None
        if not re.fullmatch(
            r"[1-9][0-9]*(?:,[1-9][0-9]*)*", args.classical_direct_levels
        ):
            fail("invalid --classical-direct-levels")
        direct_levels = [int(value) for value in args.classical_direct_levels.split(",")]
        if len(direct_levels) != len(set(direct_levels)) or any(
            value > (1 << 32) - 1 for value in direct_levels
        ):
            fail("invalid --classical-direct-levels")
        for value in direct_levels:
            if value <= 3 or any(
                value % divisor == 0
                for divisor in range(2, math.isqrt(value) + 1)
            ):
                fail("direct levels must be distinct primes greater than three")
        direct_positive = (
            ("classical-direct-max-prime-candidates",
             args.classical_direct_max_prime_candidates),
            ("classical-direct-max-x-candidates",
             args.classical_direct_max_x_candidates),
            ("classical-direct-context-max-file-bytes",
             args.classical_direct_context_max_file_bytes),
        )
        for name, value in direct_positive:
            assert value is not None
            if not 0 < value <= MAX_U64:
                fail(f"invalid --{name}")
        assert args.classical_direct_context_max_file_bytes is not None
        if args.classical_direct_context_max_file_bytes < 96:
            fail("classical direct cache admission limit is below its header")
        assert args.classical_direct_cache_resident_bytes is not None
        if not 0 <= args.classical_direct_cache_resident_bytes <= MAX_U64:
            fail("invalid --classical-direct-cache-resident-bytes")
        if (
            args.classical_direct_context_sha256 is None
            or not HEX_64.fullmatch(args.classical_direct_context_sha256)
        ):
            fail("invalid trusted direct-cache digest")

    search_options = (
        ("curve-family", args.curve_family),
        ("x1-require-point4", args.x1_require_point4),
        ("curve-threads", args.curve_threads),
        ("sea-level-telemetry", args.sea_level_telemetry),
        ("schoof-fallback", args.schoof_fallback),
        ("skip-incomplete-curves", args.skip_incomplete_curves),
        ("smooth-coordinators", args.smooth_coordinators),
    )
    search_argv: list[str] = []
    for name, value in search_options:
        if value is not None:
            search_argv.extend((f"--{name}", str(value)))

    resource_options = (
        ("max-curves", args.max_curves, True),
        ("checkpoint-every", args.checkpoint_every, True),
        ("trace-cap", args.trace_cap, True),
        ("smooth-threads", args.smooth_threads, False),
        ("smooth-max-batch", args.smooth_max_batch, True),
        ("smooth-root-auxiliary-bytes", args.smooth_root_auxiliary_bytes, True),
        ("smooth-build-segment-span", args.smooth_build_segment_span, True),
        ("assembly-attempts", args.assembly_attempts, True),
        ("max-certificate-candidates", args.max_certificate_candidates, True),
        ("max-candidate-search-nodes", args.max_candidate_search_nodes, True),
    )
    resource_argv: list[str] = []
    for name, value, positive in resource_options:
        if value is None:
            continue
        if value < 0 or value > MAX_U64 or (positive and value == 0):
            fail(f"invalid --{name}")
        resource_argv.extend((f"--{name}", str(value)))

    root = args.root.resolve()
    current = within(root / "current", root, "current deployment")
    build_manifest_path = current / "build-manifest.json"
    if not build_manifest_path.is_file():
        fail("deployment has no build manifest")
    with build_manifest_path.open(encoding="utf-8") as stream:
        build = json.load(stream)
    if build.get("schema") != "oneshotsea.aws-build.v1":
        fail("unknown build manifest schema")
    if build.get("instance_id") != args.instance_id:
        fail("build manifest belongs to another instance")
    commit = build.get("deployment_commit", "")
    binary_sha = build.get("binary_sha256", "")
    if not HEX_40.fullmatch(commit) or not HEX_64.fullmatch(binary_sha):
        fail("invalid build identity")
    if build.get("binary_relative_path") != "build/oneshotsea":
        fail("unexpected binary path in build manifest")
    executable = within(current / "build/oneshotsea", current, "binary")
    if not os.access(executable, os.X_OK) or digest(executable) != binary_sha:
        fail("deployed binary is missing or its digest changed")
    environment = within(current / "environment.txt", current, "build environment")
    environment_sha = build.get("environment_sha256", "")
    if (
        not environment.is_file()
        or not HEX_64.fullmatch(environment_sha)
        or digest(environment) != environment_sha
    ):
        fail("deployed environment record is missing or its digest changed")
    tables = within(current / args.table_dir, current, "table directory")
    if not tables.is_dir():
        fail("table directory does not exist")
    smooth_cache = within(args.smooth_cache, root, "smooth cache")
    if not smooth_cache.is_file() or digest(smooth_cache) != args.smooth_cache_sha256:
        fail("smooth cache is missing or its digest changed")
    cache_manifest_path = within(
        smooth_cache.parent / "manifest.json", root, "smooth cache manifest"
    )
    if not cache_manifest_path.is_file():
        fail("smooth cache has no provenance manifest")
    with cache_manifest_path.open(encoding="utf-8") as stream:
        cache_manifest = json.load(stream)
    if (
        cache_manifest.get("schema") != "oneshotsea.aws-cache.v1"
        or cache_manifest.get("prime") != str(args.prime)
        or cache_manifest.get("max_level") != args.max_level
        or cache_manifest.get("table_dir") != args.table_dir
        or cache_manifest.get("deployment_commit") != commit
        or cache_manifest.get("binary_sha256") != binary_sha
        or cache_manifest.get("smooth_cache_sha256") != args.smooth_cache_sha256
        or cache_manifest.get("expected_smooth_cache_sha256")
        != args.smooth_cache_sha256
    ):
        fail("smooth cache provenance does not match the trusted worker inputs")

    direct_cache = None
    direct_manifest_path = None
    direct_argv: list[str] = []
    if direct_enabled:
        assert args.classical_direct_context_cache is not None
        assert args.classical_direct_context_sha256 is not None
        assert args.classical_direct_max_prime_candidates is not None
        assert args.classical_direct_max_x_candidates is not None
        assert args.classical_direct_context_max_file_bytes is not None
        assert args.classical_direct_cache_resident_bytes is not None
        direct_cache = within(
            args.classical_direct_context_cache, root, "direct cache"
        )
        if (
            not direct_cache.is_file()
            or direct_cache.is_symlink()
            or digest(direct_cache) != args.classical_direct_context_sha256
        ):
            fail("direct cache is missing or its digest changed")
        direct_manifest_path = within(
            direct_cache.parent / "manifest.json", root,
            "direct cache manifest",
        )
        if not direct_manifest_path.is_file():
            fail("direct cache has no provenance manifest")
        with direct_manifest_path.open(encoding="utf-8") as stream:
            direct_manifest = json.load(stream)
        if not isinstance(direct_manifest, dict):
            fail("direct cache provenance manifest is not an object")
        expected_direct_command = [
            str(executable), "prepare-classical-direct-context",
            "--p", str(args.prime),
            "--classical-direct-levels", args.classical_direct_levels,
            "--classical-direct-max-prime-candidates",
            str(args.classical_direct_max_prime_candidates),
            "--classical-direct-max-x-candidates",
            str(args.classical_direct_max_x_candidates),
            "--classical-direct-context-max-file-bytes",
            str(args.classical_direct_context_max_file_bytes),
            "--sea-threads", str(args.sea_threads),
            "--output", str(direct_cache),
        ]
        direct_checks = (
            ("schema", "oneshotsea.aws-direct-cache.v1"),
            ("cache_id", direct_cache.parent.name),
            ("prime", str(args.prime)),
            ("ordered_levels", [str(value) for value in direct_levels]),
            ("maximum_prime_candidates",
             args.classical_direct_max_prime_candidates),
            ("maximum_x_candidates_per_surface",
             args.classical_direct_max_x_candidates),
            ("sea_threads", args.sea_threads),
            ("max_file_bytes", args.classical_direct_context_max_file_bytes),
            ("file_bytes", direct_cache.stat().st_size),
            ("deployment_commit", commit),
            ("binary_sha256", binary_sha),
            ("direct_cache_sha256", args.classical_direct_context_sha256),
            ("command_argv", expected_direct_command),
        )
        for name, expected in direct_checks:
            if direct_manifest.get(name) != expected:
                fail(f"direct cache provenance mismatch: {name}")
        if direct_manifest.get("expected_direct_cache_sha256") not in (
            None, args.classical_direct_context_sha256
        ):
            fail("direct cache provenance mismatch: expected digest")
        direct_argv = [
            "--sea-strategy", "direct-first",
            "--classical-direct-levels", args.classical_direct_levels,
            "--classical-direct-max-prime-candidates",
            str(args.classical_direct_max_prime_candidates),
            "--classical-direct-max-x-candidates",
            str(args.classical_direct_max_x_candidates),
            "--classical-direct-context-cache", str(direct_cache),
            "--classical-direct-context-sha256",
            args.classical_direct_context_sha256,
            "--classical-direct-context-max-file-bytes",
            str(args.classical_direct_context_max_file_bytes),
            "--classical-direct-cache-resident-bytes",
            str(args.classical_direct_cache_resident_bytes),
        ]

    assigned_start, assigned_end = partition(
        args.range_start, args.range_end, args.worker_id, args.worker_count
    )
    build_id = f"git:{commit}-sha256:{binary_sha}"
    run_dir = root / "runs" / args.run_id / f"worker-{args.worker_id}"
    run_dir.mkdir(parents=True, exist_ok=True)
    checkpoint = run_dir / "checkpoint.json"
    progress = run_dir / "progress.jsonl"
    result = run_dir / "certificate.txt"
    log = run_dir / "worker.log"
    resource_usage = run_dir / "resource-usage.txt"
    attempts = run_dir / "attempts.jsonl"
    manifest_path = run_dir / "manifest.json"
    command_path = run_dir / "command.sh"
    provenance_dir = run_dir / "provenance"
    provenance_dir.mkdir(exist_ok=True)
    if provenance_dir.is_symlink() or not provenance_dir.is_dir():
        fail("worker provenance path is not a regular directory")
    within(provenance_dir, run_dir, "worker provenance directory")
    provenance_sources = {
        "build-manifest.json": build_manifest_path,
        "build-environment.txt": environment,
        "build.log": within(current / "build.log", current, "build log"),
        "cache-manifest.json": cache_manifest_path,
        "cache-command.sh": within(
            smooth_cache.parent / "command.sh", root, "smooth cache command"
        ),
        "cache-build.log": within(
            smooth_cache.parent / "build.log", root, "smooth cache build log"
        ),
        "remote_worker.py": Path(__file__).resolve(),
    }
    if direct_enabled:
        assert direct_cache is not None and direct_manifest_path is not None
        provenance_sources.update({
            "direct-cache-manifest.json": direct_manifest_path,
            "direct-cache-command.sh": within(
                direct_cache.parent / "command.sh", root,
                "direct cache command",
            ),
            "direct-cache-build.log": within(
                direct_cache.parent / "build.log", root,
                "direct cache build log",
            ),
        })
    for name, source in provenance_sources.items():
        copy_authenticated(source, provenance_dir / name, name)
    provenance_manifest_path = provenance_dir / "manifest.json"
    provenance_manifest = {
        "schema": "oneshotsea.aws-worker-provenance.v1",
        "deployment_commit": commit,
        "binary_sha256": binary_sha,
        "smooth_cache_sha256": args.smooth_cache_sha256,
        "files": {
            name: digest(provenance_dir / name) for name in sorted(provenance_sources)
        },
    }
    if direct_enabled:
        provenance_manifest["direct_cache_sha256"] = (
            args.classical_direct_context_sha256
        )
    encoded_provenance = (
        json.dumps(provenance_manifest, sort_keys=True, separators=(",", ":")) + "\n"
    )
    if provenance_manifest_path.exists():
        if provenance_manifest_path.is_symlink() or provenance_manifest_path.read_text(
            encoding="utf-8"
        ) != encoded_provenance:
            fail("retained worker provenance manifest changed")
    else:
        temporary_provenance = provenance_manifest_path.with_name(
            f"{provenance_manifest_path.name}.tmp.{os.getpid()}"
        )
        with temporary_provenance.open("x", encoding="utf-8") as stream:
            stream.write(encoded_provenance)
        temporary_provenance.replace(provenance_manifest_path)
    session = f"sea-{args.run_id.replace('.', '_')}-{args.worker_id}"

    completed = subprocess.run(
        ["tmux", "has-session", "-t", session],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if completed.returncode == 0:
        fail(f"tmux session already exists: {session}")

    command = [
        str(executable), "search", "--p", str(args.prime), "--seed", str(args.seed),
        "--range-start", str(args.range_start), "--range-end", str(args.range_end),
        "--worker-id", str(args.worker_id), "--worker-count", str(args.worker_count),
        "--max-level", str(args.max_level), "--sea-threads", str(args.sea_threads),
        "--table-dir", str(tables), "--smooth-cache", str(smooth_cache),
        "--smooth-cache-sha256", args.smooth_cache_sha256,
        "--checkpoint", str(checkpoint), "--progress", str(progress),
        "--certificate-out", str(result), "--build-id", build_id,
        *search_argv,
        *direct_argv,
        *resource_argv,
    ]
    run_command = command
    if args.wall_time_limit_seconds:
        run_command = [
            "timeout", "--signal=TERM", "--kill-after=60",
            str(args.wall_time_limit_seconds), *command,
        ]
    command_text = f"""#!/usr/bin/env bash
set -uo pipefail
cd {shlex.quote(str(current))}
started_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
started_epoch=$(date +%s)
printf '{{"event":"start","utc":"%s","epoch":%s}}\\n' "$started_utc" "$started_epoch" >>{shlex.quote(str(attempts))}
set +e
/usr/bin/time -v -o {shlex.quote(str(resource_usage))} -- {shlex.join(run_command)} >>{shlex.quote(str(log))} 2>&1
status=$?
set -e
ended_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
ended_epoch=$(date +%s)
printf '{{"event":"end","utc":"%s","epoch":%s,"status":%s}}\\n' "$ended_utc" "$ended_epoch" "$status" >>{shlex.quote(str(attempts))}
exit "$status"
"""
    command_sha256 = hashlib.sha256(command_text.encode()).hexdigest()
    manifest = {
        "schema": "oneshotsea.aws-worker.v1",
        "instance_id": args.instance_id,
        "run_id": args.run_id,
        "run_kind": args.run_kind,
        "worker_id": args.worker_id,
        "worker_count": args.worker_count,
        "global_range": {
            "start": str(args.range_start), "end": str(args.range_end),
            "count": str(args.range_end - args.range_start),
        },
        "assigned_range": {
            "start": str(assigned_start), "end": str(assigned_end),
            "count": str(assigned_end - assigned_start),
        },
        "seed": str(args.seed),
        "prime": str(args.prime),
        "deployment_commit": commit,
        "binary_sha256": binary_sha,
        "build_id": build_id,
        "launcher_sha256": digest(Path(__file__).resolve()),
        "provenance_manifest_sha256": digest(provenance_manifest_path),
        "wall_time_limit_seconds": args.wall_time_limit_seconds,
        "command_argv": command,
        "command_sha256": command_sha256,
    }
    if direct_enabled:
        manifest["direct_cache_sha256"] = args.classical_direct_context_sha256

    if manifest_path.exists():
        if not args.resume:
            fail("worker manifest exists; use --resume after review")
        if not checkpoint.is_file() or checkpoint.stat().st_size == 0:
            fail("cannot resume without a nonempty checkpoint")
        if result.exists():
            fail("certificate already exists; fetch and verify it")
        with manifest_path.open(encoding="utf-8") as stream:
            previous = json.load(stream)
        for key, value in manifest.items():
            if previous.get(key) != value:
                fail(f"resume manifest mismatch for {key}")
        if (
            command_path.is_symlink()
            or not command_path.is_file()
            or digest(command_path) != command_sha256
            or command_path.read_text(encoding="utf-8") != command_text
        ):
            fail("resume command file is missing or changed")
    else:
        if args.resume:
            fail("--resume requested without a manifest")
        if any(path.exists() for path in (checkpoint, progress, result)):
            fail("worker artifacts exist without a manifest")
        manifest["created_utc"] = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        temporary = manifest_path.with_name(f"{manifest_path.name}.tmp.{os.getpid()}")
        with temporary.open("x", encoding="utf-8") as stream:
            json.dump(manifest, stream, sort_keys=True, separators=(",", ":"))
            stream.write("\n")
        temporary.replace(manifest_path)

        with command_path.open("x", encoding="utf-8") as stream:
            stream.write(command_text)
        command_path.chmod(0o700)

    subprocess.run(["tmux", "new-session", "-d", "-s", session, str(command_path)], check=True)
    print(f"session={session}")
    print(f"run_dir={run_dir}")
    print(f"assigned_range=[{assigned_start},{assigned_end})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
