#!/usr/bin/env python3
"""Audit the predeclared p125 RunPod dual/single B/A/A/B topology gate."""

from __future__ import print_function

import argparse
import calendar
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time
from typing import Any, Dict, List, Optional, Sequence, Tuple

import audit_runpod_search as runpod


SCHEMA = "oneshotsea.p125-topology-audit.v1"
BUILD_PROVENANCE_SCHEMA = "oneshotsea.runpod-build-provenance.v1"
AuditError = runpod.AuditError

TARGET_PRIME = (
    "100000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000237"
)
TARGET_SEED = "202607300000"
DEPLOYMENT_COMMIT = "550815e0013361f5eee4cdb6b044f5cec1a9ae2c"
BINARY_SHA256 = "550c38acebb0407de4fc1021d905796798f9f18534c341a8a5b5238d34259737"
CACHE_SHA256 = "afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551"
SCHEDULE_SHA256 = "1721cf2a6cba0287e9247e859cb0fa923166f4b3feb18d37d5428d2dd3e45a1b"
TABLE_MANIFEST_SHA256 = (
    "ac1fb3eafd991bccae2fcc05572108f318522b15fd6a3a164b8665c16f2d6bd5"
)
BUILD_ID = "git:{}+binary-sha256:{}".format(DEPLOYMENT_COMMIT, BINARY_SHA256)

PAIR_RANGES = {
    "x": {"start": 1000827, "end": 1000891, "count": 64},
    "y": {"start": 1000891, "end": 1000955, "count": 64},
}
RUN_SPECS = {
    "bx": ("B", "x"),
    "ax": ("A", "x"),
    "ay": ("A", "y"),
    "by": ("B", "y"),
}

MAX_DUAL_RSS_BYTES = 48 * 1024 ** 3
MAX_DUAL_START_SKEW_SECONDS = 30
MIN_PAIR_SPEEDUP_EXCLUSIVE = 1.0
MIN_GEOMETRIC_MEAN_EXCLUSIVE = 1.05
WALL_TIME_LIMIT_SECONDS = 2400
ARGV0_SUFFIX = "/OneShotSEA-550815e/build/oneshotsea"
REMOTE_DEPLOY = "/workspace/OneShotSEA-550815e"
REMOTE_RUNS = "/workspace/OneShotSEA/runs"

FIXED_OPTIONS = {
    "--p": TARGET_PRIME,
    "--seed": TARGET_SEED,
    "--max-level": "401",
    "--table-dir": REMOTE_DEPLOY + "/data/modpoly/weber_f",
    "--smooth-cache": "/workspace/OneShotSEA/caches/p125.cache",
    "--smooth-cache-sha256": CACHE_SHA256,
    "--build-id": BUILD_ID,
    "--curve-family": "x1-27",
    "--x1-require-point4": "1",
    "--sea-level-telemetry": "0",
    "--schoof-fallback": "1",
    "--skip-incomplete-curves": "0",
    "--smooth-coordinators": "0",
    "--checkpoint-every": "1",
    "--trace-cap": "16",
    "--sea-threads": "1",
    "--smooth-threads": "1",
    "--smooth-max-batch": "128",
    "--smooth-root-auxiliary-bytes": "134217728",
    "--smooth-build-segment-span": "4000000000",
    "--assembly-attempts": "400",
    "--max-certificate-candidates": "100000",
    "--max-candidate-search-nodes": "1000000",
}
VARIABLE_OPTIONS = {
    "--range-start",
    "--range-end",
    "--worker-id",
    "--worker-count",
    "--curve-threads",
    "--max-curves",
    "--checkpoint",
    "--progress",
    "--certificate-out",
}
EXPECTED_OPTION_NAMES = set(FIXED_OPTIONS) | VARIABLE_OPTIONS

START_RESOURCE_VALUES = {
    "smooth_threads": "1",
    "smooth_max_batch": "128",
    "smooth_root_auxiliary_bytes": "134217728",
    "smooth_build_segment_span": "4000000000",
    "smooth_coordinators": "0",
    "x1_require_point_four": True,
    "skip_incomplete_curves": False,
    "schoof_fallback": True,
    "sea_level_telemetry": False,
    "sea_threads": "1",
    "assembly_attempts": "400",
    "trace_cap": "16",
    "max_certificate_candidates": "100000",
    "max_candidate_search_nodes": "1000000",
}

ALLOWED_STATUSES = {
    "sound_smoothness_reject": "sound_rejection",
    "no_certificate_candidate": "terminal",
    "certificate_assembly_failed": "terminal",
    "canonical_verifier_rejected": "terminal",
}
REQUIRED_RECORD_FIELDS = {
    "schema",
    "index",
    "status",
    "peak_rss_bytes",
    "heuristic",
    "outcome_class",
    "sound_early_abort",
    "full_point_count",
    "reached_smoothness",
    "generator_rejections",
    "trace_prior",
    "sea_passes",
    "sea_levels",
    "exact_sea_levels",
    "atkin_sea_levels",
    "schoof_fallback_level_count",
    "initial_trace_count",
    "candidate_attempts",
    "candidate_search_nodes",
    "assembly_calls",
    "canonical_rejections",
    "timings_us",
    "sea_level_timings",
    "schoof_fallback_levels",
    "state",
}
OPTIONAL_RECORD_FIELDS = {
    "final_exact_trace_candidates",
    "final_trace_candidates",
    "trace",
}
INTEGER_RECORD_FIELDS = {
    "index",
    "peak_rss_bytes",
    "generator_rejections",
    "sea_passes",
    "sea_levels",
    "exact_sea_levels",
    "atkin_sea_levels",
    "schoof_fallback_level_count",
    "initial_trace_count",
    "candidate_attempts",
    "candidate_search_nodes",
    "assembly_calls",
    "canonical_rejections",
    "final_exact_trace_candidates",
    "final_trace_candidates",
}
TIMING_FIELDS = {
    "generation",
    "sea",
    "smoothness",
    "candidate",
    "assembly",
    "verifier",
    "total",
}
MANIFEST_FIELDS = {
    "schema",
    "run_id",
    "run_kind",
    "worker_id",
    "worker_count",
    "global_range",
    "assigned_range",
    "seed",
    "prime",
    "deployment_commit",
    "binary_sha256",
    "build_id",
    "wall_time_limit_seconds",
    "command_sha256",
    "command_argv",
    "started_utc",
}
WORKER_FILES = {
    "attempts.jsonl",
    "checkpoint.json",
    "command.sh",
    "manifest.json",
    "progress.jsonl",
    "resource-usage.txt",
    "worker.log",
}


def _canonical_sha256(value: Any) -> str:
    encoded = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def _effective_optimization(text: str, label: str) -> str:
    try:
        tokens = shlex.split(text, comments=True, posix=True)
    except ValueError as error:
        raise AuditError("cannot tokenize {}: {}".format(label, error))
    optimizations = []  # type: List[str]
    for token in tokens:
        optimizations.extend(
            re.findall(
                r"(?:^|[=\s])(-O(?:fast|[0-9]+|g|s|z)?)(?=$|\s)", token
            )
        )
    if not optimizations:
        raise AuditError("{} has no effective optimization option".format(label))
    effective = optimizations[-1]
    if effective != "-O2":
        raise AuditError(
            "{} final effective optimization is {}, expected -O2".format(
                label, effective
            )
        )
    return effective


def _binary_producers(path: Path) -> List[str]:
    if not path.is_file() or path.is_symlink():
        raise AuditError("retained binary is missing or non-regular")
    try:
        encoded = path.read_bytes()
    except OSError as error:
        raise AuditError("cannot read retained binary: {}".format(error))
    producers = set()  # type: set
    for match in re.finditer(rb"[\x20-\x7e]{4,}", encoded):
        value = match.group(0).decode("ascii")
        if value.startswith("GNU C++"):
            producers.add(value)
    if not producers:
        raise AuditError("retained binary has no GNU C++ producer record")
    return sorted(producers)


def _prove_commit(source_repo: Path, commit: str) -> None:
    if not source_repo.is_dir() or source_repo.is_symlink():
        raise AuditError("source repository is missing or not a real directory")
    try:
        completed = subprocess.run(
            ["git", "-C", str(source_repo), "cat-file", "-t", commit],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except OSError as error:
        raise AuditError("cannot inspect source repository: {}".format(error))
    if completed.returncode != 0 or completed.stdout.strip() != "commit":
        raise AuditError("deployment commit is not a commit object in source repository")


def _build_provenance(path: Path, binary_path: Path, source_repo: Path) -> Dict[str, Any]:
    value = runpod._load_json(path, "build provenance")
    required = {
        "schema",
        "deployment_commit",
        "binary_sha256",
        "build_command",
        "cxx_compiler_version",
        "cxx_producer",
    }
    if set(value) != required:
        raise AuditError("build provenance has an unexpected field set")
    if value.get("schema") != BUILD_PROVENANCE_SCHEMA:
        raise AuditError("unexpected build provenance schema")
    if value.get("deployment_commit") != DEPLOYMENT_COMMIT:
        raise AuditError("build provenance is not the predeclared deployment commit")
    if value.get("binary_sha256") != BINARY_SHA256:
        raise AuditError("build provenance is not the predeclared binary")
    for name in ("build_command", "cxx_compiler_version", "cxx_producer"):
        if not isinstance(value.get(name), str) or not value[name].strip():
            raise AuditError("build provenance {} is empty".format(name))
    actual_binary_sha256 = runpod._sha256_file(binary_path)
    if actual_binary_sha256 != BINARY_SHA256:
        raise AuditError("retained binary SHA-256 does not match the predeclared binary")
    producers = _binary_producers(binary_path)
    if value["cxx_producer"] not in producers:
        raise AuditError("build-provenance producer is not embedded in retained binary")
    build_optimization = _effective_optimization(
        value["build_command"], "build_command"
    )
    producer_optimization = _effective_optimization(
        value["cxx_producer"], "cxx_producer"
    )
    _prove_commit(source_repo, DEPLOYMENT_COMMIT)
    return {
        "schema": value["schema"],
        "deployment_commit": DEPLOYMENT_COMMIT,
        "binary_sha256": actual_binary_sha256,
        "build_command_sha256": hashlib.sha256(
            value["build_command"].encode("utf-8")
        ).hexdigest(),
        "cxx_compiler_version": value["cxx_compiler_version"],
        "cxx_producer": value["cxx_producer"],
        "build_command_optimization": build_optimization,
        "cxx_producer_optimization": producer_optimization,
        "embedded_gnu_cxx_producer_count": len(producers),
        "record_sha256": runpod._sha256_file(path),
        "source_commit_object_verified": True,
    }


def _utc_epoch(value: Any, label: str) -> int:
    if not isinstance(value, str) or runpod.UTC.fullmatch(value) is None:
        raise AuditError("{} is not canonical UTC".format(label))
    try:
        parsed = time.strptime(value, "%Y-%m-%dT%H:%M:%SZ")
    except ValueError as error:
        raise AuditError("{} is invalid UTC: {}".format(label, error))
    return calendar.timegm(parsed)


def _semantic_projection(record: Dict[str, Any]) -> Dict[str, Any]:
    value = copy.deepcopy(record)
    for name in ("state", "peak_rss_bytes", "timings_us"):
        value.pop(name, None)
    return value


def _validate_record(record: Dict[str, Any], label: str) -> int:
    fields = set(record)
    if not REQUIRED_RECORD_FIELDS <= fields or not fields <= (
        REQUIRED_RECORD_FIELDS | OPTIONAL_RECORD_FIELDS
    ):
        raise AuditError("{} progress record has an unexpected field set".format(label))
    status = record.get("status")
    if status not in ALLOWED_STATUSES:
        raise AuditError("{} progress has disallowed status {!r}".format(label, status))
    if record.get("heuristic") is not False:
        raise AuditError("{} progress contains a heuristic outcome".format(label))
    if record.get("outcome_class") != ALLOWED_STATUSES[status]:
        raise AuditError("{} progress status/outcome_class disagree".format(label))
    if record.get("sound_early_abort") is not (
        status == "sound_smoothness_reject"
    ):
        raise AuditError("{} progress sound_early_abort disagrees with status".format(label))
    for name in fields & INTEGER_RECORD_FIELDS:
        runpod._integer(record.get(name), label + " " + name)
    timings = record.get("timings_us")
    if not isinstance(timings, dict) or set(timings) != TIMING_FIELDS:
        raise AuditError("{} progress timings_us has an unexpected field set".format(label))
    for name in TIMING_FIELDS:
        runpod._integer(timings.get(name), label + " timing " + name)
    if runpod._integer(timings["total"], label + " total timing", minimum=1) < 1:
        raise AuditError("{} progress total timing is not positive".format(label))
    sea_timings = record.get("sea_level_timings")
    if sea_timings != []:
        raise AuditError("{} has SEA-level telemetry despite the disabled policy".format(label))
    fallback = record.get("schoof_fallback_levels")
    if not isinstance(fallback, list):
        raise AuditError("{} schoof_fallback_levels is not an array".format(label))
    if runpod._integer(
        record.get("schoof_fallback_level_count"), label + " fallback count"
    ) != len(fallback):
        raise AuditError("{} fallback count and level array differ".format(label))
    prior = record.get("trace_prior")
    if prior is not None:
        if not isinstance(prior, dict) or set(prior) != {"modulus", "residue"}:
            raise AuditError("{} trace_prior is malformed".format(label))
        runpod._integer(prior.get("modulus"), label + " trace-prior modulus", minimum=1)
        runpod._integer(prior.get("residue"), label + " trace-prior residue")
    return runpod._integer(record["peak_rss_bytes"], label + " peak RSS", minimum=1)


def _validate_start(
    start: Dict[str, Any], manifest: Dict[str, Any], expected_threads: int, label: str
) -> Dict[str, str]:
    required = {
        "schema",
        "prime",
        "seed",
        "curve_family",
        "worker_id",
        "worker_count",
        "range_start",
        "range_end",
        "next_index",
        "schedule_sha256",
        "table_manifest_sha256",
        "smooth_cache_sha256",
        "verifier_sha256",
        "python_executable",
        "python_sha256",
        "build_id",
        "heuristic_rejection",
        "resources",
    }
    if set(start) != required:
        raise AuditError("{} search-start has an unexpected field set".format(label))
    expected = {
        "schema": "oneshotsea.search-start.v1",
        "prime": TARGET_PRIME,
        "seed": TARGET_SEED,
        "curve_family": "x1-27",
        "schedule_sha256": SCHEDULE_SHA256,
        "table_manifest_sha256": TABLE_MANIFEST_SHA256,
        "smooth_cache_sha256": CACHE_SHA256,
        "build_id": BUILD_ID,
        "heuristic_rejection": False,
    }
    for name, wanted in expected.items():
        if start.get(name) != wanted:
            raise AuditError("{} search-start {} is not pinned".format(label, name))
    resources = start.get("resources")
    expected_resources = dict(START_RESOURCE_VALUES)
    expected_resources["curve_threads"] = str(expected_threads)
    if resources != expected_resources:
        raise AuditError("{} search-start resource policy drift".format(label))
    for name in ("verifier_sha256", "python_sha256"):
        value = start.get(name)
        if not isinstance(value, str) or runpod.SHA256.fullmatch(value) is None:
            raise AuditError("{} search-start has invalid {}".format(label, name))
    if not isinstance(start.get("python_executable"), str) or not start["python_executable"]:
        raise AuditError("{} search-start has no Python executable".format(label))
    return {
        "verifier_sha256": start["verifier_sha256"],
        "python_sha256": start["python_sha256"],
        "python_executable": start["python_executable"],
    }


def _validate_summary(summary: Dict[str, Any], label: str) -> None:
    if set(summary) != {
        "schema",
        "processed",
        "range_exhausted",
        "verified",
        "smooth_batch",
        "state",
    }:
        raise AuditError("{} summary has an unexpected field set".format(label))
    smooth = summary.get("smooth_batch")
    if not isinstance(smooth, dict):
        raise AuditError("{} summary has no smooth-batch telemetry".format(label))
    for name in (
        "submitted_requests",
        "completed_requests",
        "failed_requests",
        "cancelled_requests",
        "coordinator_batches",
        "successful_cache_scan_chunks",
        "submitted_orders",
        "max_queued_requests_in_any_cohort",
        "max_requests_per_batch_in_any_cohort",
        "max_orders_per_successful_scan_chunk_in_any_cohort",
    ):
        if smooth.get(name) != "0":
            raise AuditError("{} summary smooth-batch {} is nonzero".format(label, name))
    if (
        smooth.get("enabled") is not False
        or smooth.get("coordinator_count") != "0"
        or smooth.get("successful_scan_chunk_size_histogram") != []
        or smooth.get("cohorts") != []
    ):
        raise AuditError("{} summary enabled unpinned smooth batching".format(label))


def _validate_utc_binding(directory: Path, label: str) -> None:
    attempts = runpod._load_jsonl(directory / "attempts.jsonl", label + " attempts")
    for record in attempts:
        if _utc_epoch(record.get("utc"), label + " attempt UTC") != runpod._integer(
            record.get("epoch"), label + " attempt epoch"
        ):
            raise AuditError("{} attempt UTC and epoch differ".format(label))
    try:
        resource = (directory / "resource-usage.txt").read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise AuditError("cannot read {} resource record: {}".format(label, error))
    matches = re.findall(
        r"^attempt_start utc=(\S+) epoch=([0-9]+)$", resource, flags=re.MULTILINE
    )
    if len(matches) != 1:
        raise AuditError("{} resource record has no unique attempt start".format(label))
    if matches[0][0] != attempts[0]["utc"]:
        raise AuditError("{} resource and attempt start UTC differ".format(label))


def _fetch_metadata(root: Path, run_id: str, run_end_epoch: int) -> Dict[str, Any]:
    value = runpod._load_json(root / "fetch-metadata.json", "fetch metadata")
    required = {
        "schema",
        "fetched_at",
        "run_id",
        "pod_id",
        "remote_source",
        "pod_state",
        "estimated_current_session_compute_cost_usd",
        "estimate_note",
    }
    if set(value) != required or value.get("schema") != 1:
        raise AuditError("fetch metadata has an unexpected schema/field set")
    if value.get("run_id") != run_id:
        raise AuditError("fetch metadata run_id differs from manifests")
    if value.get("remote_source") != REMOTE_RUNS + "/" + run_id:
        raise AuditError("fetch metadata remote source is not the pinned run")
    if not isinstance(value.get("pod_id"), str) or not value["pod_id"]:
        raise AuditError("fetch metadata has no pod identity")
    fetched_epoch = _utc_epoch(value.get("fetched_at"), "fetch timestamp")
    if fetched_epoch < run_end_epoch:
        raise AuditError("fetch timestamp precedes run completion")
    return {"pod_id": value["pod_id"], "fetched_epoch": fetched_epoch}


def _validate_layout(root: Path, worker_count: int, label: str) -> None:
    wanted_top = {"SHA256SUMS", "fetch-metadata.json"} | {
        "worker-{}".format(worker_id) for worker_id in range(worker_count)
    }
    actual_top = {path.name for path in root.iterdir()}
    if actual_top != wanted_top:
        raise AuditError("{} root has an unexpected v3 file layout".format(label))
    for worker_id in range(worker_count):
        directory = root / "worker-{}".format(worker_id)
        actual = {path.name for path in directory.iterdir()}
        if actual != WORKER_FILES or any(not path.is_file() for path in directory.iterdir()):
            raise AuditError(
                "{} worker-{} has an unexpected v3 file layout".format(
                    label, worker_id
                )
            )
        command = directory / "command.sh"
        if command.stat().st_mode & 0o777 != 0o700:
            raise AuditError("{} worker-{} command.sh is not mode 0700".format(label, worker_id))


def _run(
    root: Path,
    label: str,
    topology: str,
    range_name: str,
    provenance: Dict[str, Any],
) -> Dict[str, Any]:
    del provenance  # Immutable identities are pinned independently below.
    expected_range = PAIR_RANGES[range_name]
    expected_count = 2 if topology == "B" else 1
    expected_threads = 8 if topology == "B" else 16
    _validate_layout(root, expected_count, label)
    base = runpod.audit(
        root,
        expected_commit=DEPLOYMENT_COMMIT,
        expected_binary_sha256=BINARY_SHA256,
        expected_cache_sha256=CACHE_SHA256,
        expected_global_start=expected_range["start"],
        expected_global_end=expected_range["end"],
        expected_worker_count=expected_count,
        allow_timeout_124=False,
    )
    if not base["outcome"]["all_assigned_ranges_exhausted"]:
        raise AuditError("{} did not exhaust every assigned range".format(label))
    if base["outcome"]["certificates"]:
        raise AuditError("{} found a certificate and is not a no-certificate gate leg".format(label))
    if base["outcome"]["counters"]["rejected_heuristic"] != 0:
        raise AuditError("{} records heuristic rejection".format(label))
    if base["outcome"]["counters"]["certificates_found"] != 0:
        raise AuditError("{} records a certificate".format(label))
    if base["resources"]["exit_statuses"] != [0] * expected_count:
        raise AuditError("{} has a nonzero exit".format(label))
    if base["resources"]["swaps_sum"] != 0:
        raise AuditError("{} swapped".format(label))

    run_id = base["identity"]["run_id"]
    expected_prefix = "p125-topology-{}-550815e-".format(label)
    if not run_id.startswith(expected_prefix) or len(run_id) == len(expected_prefix):
        raise AuditError("{} run_id is not label/commit bound".format(label))
    if base["identity"]["run_kind"] != "benchmark":
        raise AuditError("{} is not a benchmark run".format(label))
    if base["identity"]["prime"] != TARGET_PRIME or base["identity"]["seed"] != TARGET_SEED:
        raise AuditError("{} is not the predeclared p125 seed".format(label))

    merged = []  # type: List[Tuple[int, Dict[str, Any]]]
    intervals = []  # type: List[Tuple[float, float]]
    attempt_intervals = []  # type: List[Tuple[int, int]]
    identities = []  # type: List[Dict[str, str]]
    for worker in base["workers"]:
        worker_id = worker["worker_id"]
        worker_label = "{} worker-{}".format(label, worker_id)
        directory = root / "worker-{}".format(worker_id)
        manifest = runpod._load_json(directory / "manifest.json", worker_label + " manifest")
        if set(manifest) != MANIFEST_FIELDS:
            raise AuditError("{} manifest has an unexpected field set".format(worker_label))
        if manifest.get("run_id") != run_id:
            raise AuditError("{} run_id differs within root".format(worker_label))
        if manifest.get("prime") != TARGET_PRIME or manifest.get("seed") != TARGET_SEED:
            raise AuditError("{} manifest prime/seed drift".format(worker_label))
        if manifest.get("wall_time_limit_seconds") != WALL_TIME_LIMIT_SECONDS:
            raise AuditError("{} wall-time policy drift".format(worker_label))
        argv, options = runpod._argv_options(manifest.get("command_argv"), worker_label)
        if not argv[0].endswith(ARGV0_SUFFIX):
            raise AuditError("{} argv[0] is not the pinned deployment binary".format(worker_label))
        if set(options) != EXPECTED_OPTION_NAMES:
            raise AuditError("{} command has an unexpected option set".format(worker_label))
        for name, wanted in FIXED_OPTIONS.items():
            if options.get(name) != wanted:
                raise AuditError("{} fixed command option {} drift".format(worker_label, name))
        assigned = worker["assigned_range"]
        expected_variable = {
            "--range-start": str(expected_range["start"]),
            "--range-end": str(expected_range["end"]),
            "--worker-id": str(worker_id),
            "--worker-count": str(expected_count),
            "--curve-threads": str(expected_threads),
            "--max-curves": str(assigned["count"]),
        }
        remote_worker = REMOTE_RUNS + "/{}/worker-{}".format(run_id, worker_id)
        expected_variable.update(
            {
                "--checkpoint": remote_worker + "/checkpoint.json",
                "--progress": remote_worker + "/progress.jsonl",
                "--certificate-out": remote_worker + "/certificate.txt",
            }
        )
        for name, wanted in expected_variable.items():
            if options.get(name) != wanted:
                raise AuditError("{} variable command option {} drift".format(worker_label, name))

        _validate_utc_binding(directory, worker_label)
        log = runpod._load_jsonl(directory / "worker.log", worker_label + " worker.log")
        starts = [item for item in log if item.get("schema") == "oneshotsea.search-start.v1"]
        summaries = [item for item in log if item.get("schema") == "oneshotsea.search-summary.v1"]
        if len(starts) != 1 or len(summaries) != 1:
            raise AuditError("{} log lacks one start and one summary".format(worker_label))
        identities.append(_validate_start(starts[0], manifest, expected_threads, worker_label))
        _validate_summary(summaries[0], worker_label)
        if worker["worker_log"]["sea_level_record_count"] != 0:
            raise AuditError("{} emitted disabled SEA-level telemetry".format(worker_label))

        progress = runpod._load_jsonl(directory / "progress.jsonl", worker_label + " progress")
        record_rss = []  # type: List[int]
        for record in progress:
            record_rss.append(_validate_record(record, worker_label))
            merged.append((runpod._integer(record["index"], worker_label + " index"), record))
        if not record_rss or max(record_rss) != worker["resource_usage"]["peak_rss_bytes"]:
            raise AuditError("{} progress and GNU-time peak RSS differ".format(worker_label))
        usage = worker["resource_usage"]
        intervals.append(
            (
                float(usage["attempt_start_epoch"]),
                float(usage["attempt_start_epoch"]) + usage["elapsed_seconds"],
            )
        )
        attempt_intervals.append(
            (worker["attempt"]["start_epoch"], worker["attempt"]["end_epoch"])
        )

    if any(identity != identities[0] for identity in identities[1:]):
        raise AuditError("{} workers disagree about verifier/Python identity".format(label))
    if topology == "B":
        starts = [item[0] for item in attempt_intervals]
        ends = [item[1] for item in attempt_intervals]
        if max(starts) - min(starts) > MAX_DUAL_START_SKEW_SECONDS:
            raise AuditError("{} dual-worker launch skew exceeds 30 seconds".format(label))
        if max(starts) >= min(ends):
            raise AuditError("{} dual-worker intervals do not overlap".format(label))

    merged.sort(key=lambda item: item[0])
    expected_indices = list(range(expected_range["start"], expected_range["end"]))
    if [item[0] for item in merged] != expected_indices:
        raise AuditError("{} merged records are not the exact pinned range".format(label))
    attempt_start = min(item[0] for item in attempt_intervals)
    attempt_end = max(item[1] for item in attempt_intervals)
    fetch = _fetch_metadata(root, run_id, attempt_end)
    return {
        "label": label,
        "topology": topology,
        "run_id": run_id,
        "range": expected_range,
        "records": [item[1] for item in merged],
        "intervals": intervals,
        "attempt_start_epoch": attempt_start,
        "attempt_end_epoch": attempt_end,
        "sum_peak_rss_bytes": base["resources"]["sum_worker_peak_rss_bytes"],
        "checksum_records": base["checksum_record_count"],
        "sha256sums_sha256": base["sha256sums_sha256"],
        "fetch": fetch,
        "runtime_identity": identities[0],
        "aggregate_counters": base["outcome"]["counters"],
    }


def _interval_union(intervals: List[Tuple[float, float]]) -> float:
    ordered = sorted(intervals)
    if not ordered:
        raise AuditError("cannot compute an empty interval union")
    total = 0.0
    start, end = ordered[0]
    for next_start, next_end in ordered[1:]:
        if next_start <= end:
            end = max(end, next_end)
        else:
            total += end - start
            start, end = next_start, next_end
    return total + end - start


def _pair(name: str, dual: Dict[str, Any], single: Dict[str, Any]) -> Dict[str, Any]:
    if dual["range"] != single["range"]:
        raise AuditError("pair {} B/A ranges differ".format(name))
    dual_projection = [_semantic_projection(record) for record in dual["records"]]
    single_projection = [_semantic_projection(record) for record in single["records"]]
    if dual_projection != single_projection:
        raise AuditError("pair {} B/A semantic projections differ".format(name))
    dual_wall = _interval_union(dual["intervals"])
    single_wall = _interval_union(single["intervals"])
    if dual_wall <= 0 or single_wall <= 0:
        raise AuditError("pair {} has nonpositive wall time".format(name))
    speedup = single_wall / dual_wall
    dual_rss = dual["sum_peak_rss_bytes"]
    gates = {
        "speedup_gt_1": speedup > MIN_PAIR_SPEEDUP_EXCLUSIVE,
        "dual_sum_peak_rss_lt_48_gib": dual_rss < MAX_DUAL_RSS_BYTES,
        "semantic_projections_identical": True,
        "no_certificates_or_heuristics": True,
    }
    return {
        "comparison": [dual["label"], single["label"]],
        "range": dual["range"],
        "records_per_side": len(dual_projection),
        "semantic_projection_sha256": _canonical_sha256(dual_projection),
        "dual_worker_intervals": [
            {"attempt_start_epoch": start, "attempt_end_epoch": end}
            for start, end in dual["intervals"]
        ],
        "dual_wall_union_seconds": dual_wall,
        "single_wall_seconds": single_wall,
        "single_over_dual_speedup": speedup,
        "dual_sum_peak_rss_bytes": dual_rss,
        "dual_sum_peak_rss_gib": dual_rss / float(1024 ** 3),
        "gates": gates,
    }


def audit(
    bx_root: Path,
    ax_root: Path,
    ay_root: Path,
    by_root: Path,
    build_provenance_path: Path,
    binary_path: Path,
    source_repo: Path,
) -> Dict[str, Any]:
    """Authenticate, compare, and summarize the pinned topology gate."""
    provenance = _build_provenance(build_provenance_path, binary_path, source_repo)
    roots = {"bx": bx_root, "ax": ax_root, "ay": ay_root, "by": by_root}
    runs = {
        label: _run(roots[label], label, RUN_SPECS[label][0], RUN_SPECS[label][1], provenance)
        for label in ("bx", "ax", "ay", "by")
    }

    order = ("bx", "ax", "ay", "by")
    for earlier, later in zip(order, order[1:]):
        if runs[earlier]["attempt_end_epoch"] >= runs[later]["attempt_start_epoch"]:
            raise AuditError(
                "run chronology is not strict B_X -> A_X -> A_Y -> B_Y"
            )
    if PAIR_RANGES["x"]["end"] != PAIR_RANGES["y"]["start"]:
        raise AuditError("predeclared X/Y ranges are not adjacent")
    pod_ids = [runs[name]["fetch"]["pod_id"] for name in order]
    if any(pod_id != pod_ids[0] for pod_id in pod_ids[1:]):
        raise AuditError("the four gate legs were not fetched from one pod")
    runtime_identities = [runs[name]["runtime_identity"] for name in order]
    if any(identity != runtime_identities[0] for identity in runtime_identities[1:]):
        raise AuditError("the four gate legs disagree about runtime verifier/Python identity")

    pair_x = _pair("x", runs["bx"], runs["ax"])
    pair_y = _pair("y", runs["by"], runs["ay"])
    speedups = (
        pair_x["single_over_dual_speedup"],
        pair_y["single_over_dual_speedup"],
    )
    geometric_mean = math.sqrt(speedups[0] * speedups[1])
    gates = {
        "pair_x_speedup_gt_1": speedups[0] > MIN_PAIR_SPEEDUP_EXCLUSIVE,
        "pair_y_speedup_gt_1": speedups[1] > MIN_PAIR_SPEEDUP_EXCLUSIVE,
        "geometric_mean_speedup_gt_1_05": geometric_mean
        > MIN_GEOMETRIC_MEAN_EXCLUSIVE,
        "all_pair_integrity_and_resource_gates": all(
            all(pair["gates"].values()) for pair in (pair_x, pair_y)
        ),
        "strict_b_a_a_b_chronology": True,
        "pinned_adjacent_ranges": True,
    }
    accepted = all(gates.values())
    return {
        "schema": SCHEMA,
        "accepted": accepted,
        "input_order": list(order),
        "pair_order": "pinned_adjacent_x_then_y",
        "build_provenance": provenance,
        "immutable_identity": {
            "deployment_commit": DEPLOYMENT_COMMIT,
            "binary_sha256": BINARY_SHA256,
            "smooth_cache_sha256": CACHE_SHA256,
            "schedule_sha256": SCHEDULE_SHA256,
            "table_manifest_sha256": TABLE_MANIFEST_SHA256,
            "prime": TARGET_PRIME,
            "seed": TARGET_SEED,
            "pod_id": pod_ids[0],
            "fixed_config_sha256": _canonical_sha256(FIXED_OPTIONS),
        },
        "checksum_record_counts": {
            name: runs[name]["checksum_records"] for name in order
        },
        "sha256sums_sha256": {
            name: runs[name]["sha256sums_sha256"] for name in order
        },
        "run_ids": {name: runs[name]["run_id"] for name in order},
        "chronology": [
            {
                "run": name,
                "attempt_start_epoch": runs[name]["attempt_start_epoch"],
                "attempt_end_epoch": runs[name]["attempt_end_epoch"],
            }
            for name in order
        ],
        "pairs": {"x": pair_x, "y": pair_y},
        "speedup_geometric_mean": geometric_mean,
        "thresholds": {
            "pair_speedup_minimum_exclusive": MIN_PAIR_SPEEDUP_EXCLUSIVE,
            "geometric_mean_minimum_exclusive": MIN_GEOMETRIC_MEAN_EXCLUSIVE,
            "dual_sum_peak_rss_maximum_exclusive_bytes": MAX_DUAL_RSS_BYTES,
            "dual_start_skew_maximum_inclusive_seconds": MAX_DUAL_START_SKEW_SECONDS,
        },
        "gates": gates,
    }


def _encoded(value: Dict[str, Any]) -> str:
    return json.dumps(value, sort_keys=True, indent=2, ensure_ascii=True) + "\n"


def _write_atomic(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name("{}.tmp.{}".format(path.name, os.getpid()))
    try:
        with temporary.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write(content)
        os.replace(str(temporary), str(path))
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bx", type=Path, help="dual-worker pair-X run root")
    parser.add_argument("ax", type=Path, help="single-worker pair-X run root")
    parser.add_argument("ay", type=Path, help="single-worker pair-Y run root")
    parser.add_argument("by", type=Path, help="dual-worker pair-Y run root")
    parser.add_argument("--build-provenance", required=True, type=Path)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--source-repo", required=True, type=Path)
    parser.add_argument("--output", type=Path, help="also write computed JSON")
    parser.add_argument(
        "--result",
        type=Path,
        help="require exact equality with a previously retained JSON result",
    )
    arguments = parser.parse_args(argv)
    try:
        result = audit(
            arguments.bx,
            arguments.ax,
            arguments.ay,
            arguments.by,
            arguments.build_provenance,
            arguments.binary,
            arguments.source_repo,
        )
        if arguments.result is not None:
            retained = runpod._load_json(arguments.result, "retained result")
            if retained != result:
                raise AuditError("retained result does not match recomputation")
        content = _encoded(result)
        if arguments.output is not None:
            _write_atomic(arguments.output, content)
        sys.stdout.write(content)
        return 0 if result["accepted"] else 1
    except AuditError as error:
        failure = {"schema": SCHEMA, "accepted": False, "error": str(error)}
        content = _encoded(failure)
        if arguments.output is not None:
            _write_atomic(arguments.output, content)
        sys.stdout.write(content)
        return 2


if __name__ == "__main__":
    sys.exit(main())
