#!/usr/bin/env python3
"""Recompute and authenticate the exact same-30 RunPod promotion gate."""

import calendar
import copy
from datetime import datetime
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import shlex


ROOT = Path(__file__).resolve().parent
BASELINE = (
    ROOT.parent.parent
    / "p125-runpod-cpu16-probe-20260802b"
    / "ohfo3hbov7ot8v"
)

BASELINE_RUN_ID = "p125-runpod-cpu16-probe-20260802b"
CANDIDATE_RUN_ID = "p125-runpod-cpu16-replay-550815e-20260802a"
BASELINE_COMMIT = "c08f0ed82923b7aee3a5a3c9326deb4d5e439b4c"
CANDIDATE_COMMIT = "550815e0013361f5eee4cdb6b044f5cec1a9ae2c"
BASELINE_BINARY_SHA256 = (
    "383207a8c0b1f0c05a26183e2073afbbf8fe6a7c771eac4a194077e2175aecaf"
)
CANDIDATE_BINARY_SHA256 = (
    "550c38acebb0407de4fc1021d905796798f9f18534c341a8a5b5238d34259737"
)
PRIME = (
    "100000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000237"
)
SMOOTH_CACHE_SHA256 = (
    "afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551"
)
SCHEDULE_SHA256 = (
    "1721cf2a6cba0287e9247e859cb0fa923166f4b3feb18d37d5428d2dd3e45a1b"
)
TABLE_MANIFEST_SHA256 = (
    "ac1fb3eafd991bccae2fcc05572108f318522b15fd6a3a164b8665c16f2d6bd5"
)
VERIFIER_SHA256 = (
    "e0ba3b8a7ed2ff48bd2fd824642bf67b0954a9f03f57daeb4ac4302691e1b666"
)
PYTHON_SHA256 = (
    "298a9e830ed52f36c299427565485d717d1ce0179c0597cc16560513eb780b06"
)

MINIMUM_EXCLUSIVE_WALL_SPEEDUP = 1.05
MAXIMUM_INCLUSIVE_RSS_RATIO = 1.05
RECORDED_HOURLY_RATE_USD = 0.643

COMPILER_PRODUCER = "GCC: (Ubuntu 11.5.0-10ubuntu1~20~ppa3) 11.5.0"
CXX_PRODUCER = (
    "GNU C++20 11.5.0 -mtune=generic -march=x86-64 -g -O2 -std=c++20 "
    "-fopenmp -fasynchronous-unwind-tables -fstack-protector-strong "
    "-fstack-clash-protection -fcf-protection"
)

BASELINE_REQUIRED_FILES = frozenset(
    {
        "fetch-metadata.json",
        "result.json",
        "worker-0/attempts.jsonl",
        "worker-0/checkpoint.json",
        "worker-0/command.sh",
        "worker-0/manifest.json",
        "worker-0/progress.jsonl",
        "worker-0/resource-usage.txt",
        "worker-0/worker.log",
    }
)
CANDIDATE_REQUIRED_FILES = frozenset(
    {
        "audit.py",
        "binaries/baseline.bin",
        "binaries/candidate.bin",
        "fetch-metadata.json",
        "result.json",
        "worker-0/attempts.jsonl",
        "worker-0/checkpoint.json",
        "worker-0/command.sh",
        "worker-0/manifest.json",
        "worker-0/progress.jsonl",
        "worker-0/resource-usage.txt",
        "worker-0/worker.log",
    }
)

OPTION_NAMES = (
    "--p",
    "--seed",
    "--range-start",
    "--range-end",
    "--worker-id",
    "--worker-count",
    "--max-level",
    "--table-dir",
    "--smooth-cache",
    "--smooth-cache-sha256",
    "--checkpoint",
    "--progress",
    "--certificate-out",
    "--build-id",
    "--curve-family",
    "--x1-require-point4",
    "--curve-threads",
    "--sea-level-telemetry",
    "--schoof-fallback",
    "--skip-incomplete-curves",
    "--smooth-coordinators",
    "--max-curves",
    "--checkpoint-every",
    "--trace-cap",
    "--sea-threads",
    "--smooth-threads",
    "--smooth-max-batch",
    "--smooth-root-auxiliary-bytes",
    "--smooth-build-segment-span",
    "--assembly-attempts",
    "--max-certificate-candidates",
    "--max-candidate-search-nodes",
)

FIXED_OPTION_VALUES = {
    "--p": PRIME,
    "--seed": "202607300000",
    "--range-start": "1000000",
    "--range-end": "1000030",
    "--worker-id": "0",
    "--worker-count": "1",
    "--max-level": "401",
    "--smooth-cache": "/workspace/OneShotSEA/caches/p125.cache",
    "--smooth-cache-sha256": SMOOTH_CACHE_SHA256,
    "--curve-family": "x1-27",
    "--x1-require-point4": "1",
    "--curve-threads": "16",
    "--sea-level-telemetry": "0",
    "--schoof-fallback": "1",
    "--skip-incomplete-curves": "0",
    "--smooth-coordinators": "0",
    "--max-curves": "30",
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

EXPECTED_RESOURCES = {
    "smooth_threads": "1",
    "smooth_max_batch": "128",
    "smooth_root_auxiliary_bytes": "134217728",
    "smooth_build_segment_span": "4000000000",
    "curve_threads": "16",
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

EXPECTED_CONFIGURATION = {
    "prime": PRIME,
    "seed": "202607300000",
    "range_start_inclusive": "1000000",
    "range_end_exclusive": "1000030",
    "curve_count": 30,
    "max_curves": 30,
    "worker_id": 0,
    "worker_count": 1,
    "curve_family": "x1-27",
    "x1_require_point_four": True,
    "max_level": 401,
    "trace_cap": 16,
    "schoof_fallback": True,
    "skip_incomplete_curves": False,
    "curve_threads": 16,
    "sea_level_telemetry": False,
    "sea_threads": 1,
    "smooth_threads": 1,
    "smooth_coordinators": 0,
    "smooth_max_batch": 128,
    "smooth_root_auxiliary_bytes": 134217728,
    "smooth_build_segment_span": 4000000000,
    "assembly_attempts": 400,
    "max_certificate_candidates": 100000,
    "max_candidate_search_nodes": 1000000,
    "checkpoint_every": 1,
    "wall_time_limit_seconds": 1800,
    "smooth_cache_sha256": SMOOTH_CACHE_SHA256,
    "schedule_sha256": SCHEDULE_SHA256,
    "table_manifest_sha256": TABLE_MANIFEST_SHA256,
    "verifier_sha256": VERIFIER_SHA256,
    "python_sha256": PYTHON_SHA256,
}


def fail(message):
    raise SystemExit("error: " + message)


def reject_json_constant(value):
    raise ValueError("non-finite JSON number: " + value)


def load_json(path):
    try:
        return json.loads(
            path.read_text(encoding="utf-8"),
            parse_constant=reject_json_constant,
        )
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as error:
        fail("cannot parse {}: {}".format(path, error))


def json_lines(path):
    values = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as error:
        fail("cannot read {}: {}".format(path, error))
    for line_number, line in enumerate(lines, 1):
        if not line:
            fail("empty JSONL record at {}:{}".format(path, line_number))
        try:
            value = json.loads(line, parse_constant=reject_json_constant)
        except (json.JSONDecodeError, ValueError) as error:
            fail("invalid JSON at {}:{}: {}".format(path, line_number, error))
        if not isinstance(value, dict):
            fail("JSONL record is not an object at {}:{}".format(path, line_number))
        values.append(value)
    return values


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def audit_checksums(root, required_files):
    manifest = root / "SHA256SUMS"
    listed = set()
    for line in manifest.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  \./(.+)", line)
        if match is None:
            fail("malformed checksum line in " + str(manifest))
        wanted, relative = match.groups()
        if relative in listed:
            fail("duplicate checksum path: " + relative)
        listed.add(relative)
        path = root / relative
        if not path.is_file() or path.is_symlink():
            fail("checksum path is missing or not a regular file: " + str(path))
        if sha256(path) != wanted:
            fail("checksum mismatch: " + str(path))
    actual = {
        str(path.relative_to(root))
        for path in root.rglob("*")
        if path.is_file() and path.name != "SHA256SUMS"
    }
    if listed != actual:
        fail("checksum manifest does not cover the exact file set in " + str(root))
    if listed != required_files:
        missing = sorted(required_files - listed)
        extra = sorted(listed - required_files)
        fail("unexpected retained file set; missing={}, extra={}".format(missing, extra))


def require_finite_number(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        fail(label + " is not numeric")
    value = float(value)
    if not math.isfinite(value):
        fail(label + " is not finite")
    return value


def close(actual, wanted, label):
    actual = require_finite_number(actual, label + " computed value")
    wanted = require_finite_number(wanted, label + " retained value")
    if abs(actual - wanted) > 1e-12 * max(1.0, abs(actual), abs(wanted)):
        fail("{} mismatch: computed {}, retained {}".format(label, actual, wanted))


def parse_utc(value, label):
    if not isinstance(value, str):
        fail(label + " is not a UTC timestamp string")
    try:
        parsed = datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ")
    except ValueError as error:
        fail("invalid {}: {}".format(label, error))
    return calendar.timegm(parsed.timetuple())


def projection(record):
    value = copy.deepcopy(record)
    value.pop("peak_rss_bytes", None)
    value.pop("timings_us", None)
    state = value.get("state")
    if isinstance(state, dict):
        state.pop("build_id", None)
    return value


def projection_digest(values):
    encoded = json.dumps(
        values, sort_keys=True, separators=(",", ":"), allow_nan=False
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def one_match(pattern, text, label, path):
    matches = re.findall(pattern, text)
    if len(matches) != 1:
        fail("expected one {} record in {}".format(label, path))
    return matches[0]


def time_record(path):
    text = path.read_text(encoding="utf-8")
    elapsed_text = one_match(
        r"Elapsed \(wall clock\) time \(h:mm:ss or m:ss\): ([^\n]+)",
        text,
        "elapsed-time",
        path,
    )
    try:
        components = [float(value) for value in elapsed_text.split(":")]
    except ValueError as error:
        fail("invalid elapsed time in {}: {}".format(path, error))
    if not 2 <= len(components) <= 3 or any(
        not math.isfinite(value) or value < 0 for value in components
    ):
        fail("invalid elapsed time in " + str(path))
    elapsed = sum(
        value * 60 ** position
        for position, value in enumerate(reversed(components))
    )
    command_text = one_match(
        r'Command being timed: "([^\n]*)"', text, "timed-command", path
    )
    try:
        command_argv = shlex.split(command_text)
    except ValueError as error:
        fail("invalid timed command in {}: {}".format(path, error))
    start = one_match(
        r"attempt_start utc=([^ ]+) epoch=(\d+)", text, "attempt-start", path
    )
    return {
        "command_argv": command_argv,
        "attempt_start_utc": start[0],
        "attempt_start_epoch": int(start[1]),
        "elapsed_seconds": elapsed,
        "user_seconds": float(
            one_match(r"User time \(seconds\): ([^\n]+)", text, "user-time", path)
        ),
        "system_seconds": float(
            one_match(r"System time \(seconds\): ([^\n]+)", text, "system-time", path)
        ),
        "average_cpu_percent": int(
            one_match(r"Percent of CPU this job got: (\d+)%", text, "CPU", path)
        ),
        "maximum_resident_set_kbytes": int(
            one_match(
                r"Maximum resident set size \(kbytes\): (\d+)", text, "RSS", path
            )
        ),
        "swaps": int(one_match(r"Swaps: (\d+)", text, "swap", path)),
        "exit_status": int(
            one_match(r"Exit status: (\d+)", text, "exit-status", path)
        ),
    }


def parse_options(argv, label):
    if not isinstance(argv, list) or len(argv) != 2 + 2 * len(OPTION_NAMES):
        fail(label + " command has the wrong argument count")
    if not all(isinstance(value, str) for value in argv):
        fail(label + " command contains a non-string argument")
    if argv[1] != "search":
        fail(label + " command is not a search")
    names = tuple(argv[index] for index in range(2, len(argv), 2))
    if names != OPTION_NAMES:
        fail(label + " command option order or set is wrong")
    return {
        argv[index]: argv[index + 1]
        for index in range(2, len(argv), 2)
    }


def checkpoint_crc64(payload):
    polynomial = 0x42F0E1EBA9EA3693
    mask = (1 << 64) - 1
    crc = 0
    for byte in payload:
        crc ^= byte << 56
        for _ in range(8):
            high = crc & 0x8000000000000000
            crc = (crc << 1) & mask
            if high:
                crc ^= polynomial
    return "{:016x}".format(crc)


def load_checkpoint(path):
    encoded = path.read_bytes()
    if not encoded.endswith(b"\n") or encoded.count(b"\n") != 1:
        fail("checkpoint lacks its canonical single line ending: " + str(path))
    marker = b',"crc64_ecma":"'
    position = encoded.rfind(marker)
    if position < 0 or not re.fullmatch(b'[0-9a-f]{16}"}\\n', encoded[position + len(marker):]):
        fail("checkpoint checksum field is malformed: " + str(path))
    payload = encoded[:position] + b"}"
    value = load_json(path)
    if checkpoint_crc64(payload) != value.get("crc64_ecma"):
        fail("checkpoint CRC-64 mismatch: " + str(path))
    return value


def timing_totals(records):
    names = (
        "generation",
        "sea",
        "smoothness",
        "candidate",
        "assembly",
        "verifier",
        "total",
    )
    totals = {name: 0 for name in names}
    for record in records:
        timings = record.get("timings_us")
        if not isinstance(timings, dict) or set(timings) != set(names):
            fail("curve timing fields are incomplete")
        for name in names:
            try:
                value = int(timings[name])
            except (TypeError, ValueError):
                fail("curve timing is not an integer: " + name)
            if value < 0:
                fail("curve timing is negative: " + name)
            totals[name] += value
    return totals


def aggregate_outcome(records):
    return {
        "curves_attempted": len(records),
        "sound_smoothness_rejections": sum(
            record.get("status") == "sound_smoothness_reject" for record in records
        ),
        "heuristic_rejections": sum(record.get("heuristic") is True for record in records),
        "full_point_counts_completed": sum(
            record.get("full_point_count") is True for record in records
        ),
        "candidates_reaching_smoothness": sum(
            record.get("reached_smoothness") is True for record in records
        ),
        "candidate_attempts": sum(int(record["candidate_attempts"]) for record in records),
        "certificate_assembly_calls": sum(int(record["assembly_calls"]) for record in records),
        "certificates_found": int(records[-1]["state"]["counters"]["certificates_found"]),
        "range_exhausted": True,
        "checkpoint_next_index": "1000030",
    }


def validate_manifest(manifest, run_id, commit, binary_sha256, label):
    wanted_keys = {
        "assigned_range",
        "binary_sha256",
        "build_id",
        "command_argv",
        "command_sha256",
        "deployment_commit",
        "global_range",
        "prime",
        "run_id",
        "run_kind",
        "schema",
        "seed",
        "started_utc",
        "wall_time_limit_seconds",
        "worker_count",
        "worker_id",
    }
    if not isinstance(manifest, dict) or set(manifest) != wanted_keys:
        fail(label + " manifest fields are wrong")
    wanted_range = {"start": "1000000", "end": "1000030", "count": "30"}
    expected = {
        "schema": "oneshotsea.runpod-worker.v3",
        "run_id": run_id,
        "run_kind": "benchmark",
        "worker_id": 0,
        "worker_count": 1,
        "global_range": wanted_range,
        "assigned_range": wanted_range,
        "seed": "202607300000",
        "prime": PRIME,
        "deployment_commit": commit,
        "binary_sha256": binary_sha256,
        "build_id": "git:{}+binary-sha256:{}".format(commit, binary_sha256),
        "wall_time_limit_seconds": 1800,
    }
    for name, wanted in expected.items():
        if manifest.get(name) != wanted:
            fail("{} manifest mismatch: {}".format(label, name))
    if not re.fullmatch(r"[0-9a-f]{64}", manifest["command_sha256"]):
        fail(label + " manifest command digest is malformed")
    parse_utc(manifest["started_utc"], label + " manifest start")


def validate_command_controls(root, manifest, options, time, label):
    worker = root / "worker-0"
    command_path = worker / "command.sh"
    if sha256(command_path) != manifest["command_sha256"]:
        fail(label + " command.sh digest differs from its manifest")

    expected_timed = [
        "timeout",
        "--signal=TERM",
        "--kill-after=60",
        str(manifest["wall_time_limit_seconds"]),
    ] + manifest["command_argv"]
    if time["command_argv"] != expected_timed:
        fail(label + " GNU time command differs from its manifest")

    lines = command_path.read_text(encoding="utf-8").splitlines()
    timed_lines = [line for line in lines if line.startswith("/usr/bin/time ")]
    if len(timed_lines) != 1:
        fail(label + " command.sh does not contain exactly one timed command")
    try:
        shell_argv = shlex.split(timed_lines[0])
    except ValueError as error:
        fail("cannot parse {} command.sh: {}".format(label, error))
    remote_worker = PurePosixPath(options["--progress"]).parent
    expected_prefix = [
        "/usr/bin/time",
        "-a",
        "-v",
        "-o",
        str(remote_worker / "resource-usage.txt"),
        "--",
    ]
    expected_suffix = [">>" + str(remote_worker / "worker.log"), "2>&1"]
    if (
        shell_argv[: len(expected_prefix)] != expected_prefix
        or shell_argv[len(expected_prefix) : -2] != expected_timed
        or shell_argv[-2:] != expected_suffix
    ):
        fail(label + " command.sh timed command is not bound to its manifest")

    deploy = PurePosixPath(manifest["command_argv"][0]).parent.parent
    cd_lines = [line for line in lines if line.startswith("cd ")]
    if len(cd_lines) != 1 or shlex.split(cd_lines[0]) != ["cd", str(deploy)]:
        fail(label + " command.sh deployment directory is wrong")
    if PurePosixPath(options["--table-dir"]) != deploy / "data/modpoly/weber_f":
        fail(label + " table path is outside the deployed tree")

    attempts_path = remote_worker / "attempts.jsonl"
    resource_path = remote_worker / "resource-usage.txt"
    log_path = remote_worker / "worker.log"
    expected_script_lines = [
        "#!/usr/bin/env bash",
        "set -uo pipefail",
        "cd " + str(deploy),
        "started_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)",
        "started_epoch=$(date +%s)",
        """printf '{"event":"start","utc":"%s","epoch":%s}""",
        """' "$started_utc" "$started_epoch" >>""" + str(attempts_path),
        "printf 'attempt_start utc=%s epoch=%s",
        """' "$started_utc" "$started_epoch" >>""" + str(resource_path),
        "set +e",
        " ".join(expected_prefix + expected_timed + expected_suffix),
        "status=$?",
        "set -e",
        "ended_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)",
        "ended_epoch=$(date +%s)",
        """printf '{"event":"end","utc":"%s","epoch":%s,"status":%s}""",
        """' "$ended_utc" "$ended_epoch" "$status" >>""" + str(attempts_path),
        "exit \"$status\"",
    ]
    expected_script = "\n".join(expected_script_lines) + "\n"
    if command_path.read_text(encoding="utf-8") != expected_script:
        fail(label + " command.sh differs from the exact worker template")


def validate_attempts(path, manifest, time, label):
    attempts = json_lines(path)
    if len(attempts) != 2:
        fail(label + " must contain exactly one start/end attempt")
    start, end = attempts
    if set(start) != {"event", "utc", "epoch"} or start["event"] != "start":
        fail(label + " attempt start record is malformed")
    if set(end) != {"event", "utc", "epoch", "status"} or end["event"] != "end":
        fail(label + " attempt end record is malformed")
    if any(isinstance(value, bool) or not isinstance(value, int) for value in (start["epoch"], end["epoch"], end["status"])):
        fail(label + " attempt epoch/status is not an integer")
    if parse_utc(start["utc"], label + " attempt start") != start["epoch"]:
        fail(label + " attempt start UTC/epoch mismatch")
    if parse_utc(end["utc"], label + " attempt end") != end["epoch"]:
        fail(label + " attempt end UTC/epoch mismatch")
    if start["utc"] != manifest["started_utc"]:
        fail(label + " manifest/attempt start mismatch")
    if (time["attempt_start_utc"], time["attempt_start_epoch"]) != (
        start["utc"],
        start["epoch"],
    ):
        fail(label + " resource/attempt start mismatch")
    if end["status"] != time["exit_status"]:
        fail(label + " resource/attempt status mismatch")
    epoch_seconds = end["epoch"] - start["epoch"]
    if epoch_seconds < 0 or abs(epoch_seconds - time["elapsed_seconds"]) > 1.0:
        fail(label + " attempt and GNU elapsed times disagree")
    return {"start": start, "end": end, "epoch_seconds": epoch_seconds}


def validate_worker_records(root, manifest, options, time, label):
    worker = root / "worker-0"
    progress_text = (worker / "progress.jsonl").read_text(encoding="utf-8")
    log_text = (worker / "worker.log").read_text(encoding="utf-8")
    if not progress_text.endswith("\n") or not log_text.endswith("\n"):
        fail(label + " progress/log lacks its final newline")
    progress_lines = progress_text.splitlines(keepends=True)
    log_lines = log_text.splitlines(keepends=True)
    if len(progress_lines) != 30 or len(log_lines) != 32:
        fail(label + " progress/log line count is wrong")
    if "".join(log_lines[1:-1]) != progress_text:
        fail(label + " worker.log does not exactly reproduce progress.jsonl")

    progress = json_lines(worker / "progress.jsonl")
    log = json_lines(worker / "worker.log")
    start, summary = log[0], log[-1]
    expected_start = {
        "schema": "oneshotsea.search-start.v1",
        "prime": PRIME,
        "seed": "202607300000",
        "curve_family": "x1-27",
        "worker_id": "0",
        "worker_count": "1",
        "range_start": "1000000",
        "range_end": "1000030",
        "next_index": "1000000",
        "schedule_sha256": SCHEDULE_SHA256,
        "table_manifest_sha256": TABLE_MANIFEST_SHA256,
        "smooth_cache_sha256": SMOOTH_CACHE_SHA256,
        "verifier_sha256": VERIFIER_SHA256,
        "python_executable": "/usr/bin/python3.8",
        "python_sha256": PYTHON_SHA256,
        "build_id": manifest["build_id"],
        "heuristic_rejection": False,
        "resources": EXPECTED_RESOURCES,
    }
    if start != expected_start:
        fail(label + " search-start record/configuration is wrong")

    full_counts = 0
    for ordinal, record in enumerate(progress, 1):
        wanted_index = str(999999 + ordinal)
        if record.get("schema") != "oneshotsea.search-curve.v1" or record.get("index") != wanted_index:
            fail("{} curve record {} has the wrong identity".format(label, ordinal))
        if (
            record.get("status") != "sound_smoothness_reject"
            or record.get("outcome_class") != "sound_rejection"
            or record.get("sound_early_abort") is not True
            or record.get("heuristic") is not False
            or record.get("reached_smoothness") is not True
        ):
            fail("{} curve {} is not the retained sound rejection".format(label, wanted_index))
        if int(record["candidate_attempts"]) != 0 or int(record["assembly_calls"]) != 0:
            fail(label + " unexpectedly reached candidate/certificate assembly")
        if int(record["peak_rss_bytes"]) != time["maximum_resident_set_kbytes"] * 1024:
            fail(label + " curve peak RSS differs from GNU time")
        timing_totals([record])
        if record.get("full_point_count") is True:
            full_counts += 1

        state = record.get("state")
        if not isinstance(state, dict):
            fail(label + " curve state is missing")
        wanted_counters = {
            "curves_attempted": str(ordinal),
            "rejections": {
                "invalid_curve": "0",
                "sea": "0",
                "sound_early_abort": str(ordinal),
                "heuristic": "0",
                "certificate_assembly": "0",
            },
            "completed_without_certificate": "0",
            "full_point_counts_completed": str(full_counts),
            "candidates_reaching_smoothness": str(ordinal),
            "certificates_found": "0",
        }
        wanted_state = {
            "schema": "oneshotsea.search-progress.v1",
            "prime": PRIME,
            "seed": "202607300000",
            "worker_id": "0",
            "worker_count": "1",
            "range_start": "1000000",
            "range_end": "1000030",
            "schedule_sha256": SCHEDULE_SHA256,
            "table_manifest_sha256": TABLE_MANIFEST_SHA256,
            "build_id": manifest["build_id"],
            "next_index": str(1000000 + ordinal),
            "complete": ordinal == 30,
            "counters": wanted_counters,
        }
        if state != wanted_state:
            fail("{} curve {} state/counters are wrong".format(label, wanted_index))

    if full_counts != 14:
        fail(label + " full-point-count total is wrong")
    if summary.get("schema") != "oneshotsea.search-summary.v1":
        fail(label + " summary schema is wrong")
    if (
        summary.get("processed") != "30"
        or summary.get("range_exhausted") is not True
        or summary.get("verified") is not False
        or summary.get("state") != progress[-1]["state"]
    ):
        fail(label + " summary does not close the completed progress stream")

    checkpoint = load_checkpoint(worker / "checkpoint.json")
    wanted_checkpoint_counters = {
        "curves_attempted": "30",
        "rejected_invalid_curve": "0",
        "rejected_sea": "0",
        "rejected_sound_early_abort": "30",
        "rejected_heuristic": "0",
        "rejected_certificate_assembly": "0",
        "completed_without_certificate": "0",
        "full_point_counts_completed": "14",
        "candidates_reaching_smoothness": "30",
        "certificates_found": "0",
    }
    wanted_checkpoint = {
        "schema_version": 1,
        "prime": PRIME,
        "seed": "202607300000",
        "worker_id": "0",
        "worker_count": "1",
        "range_start": "1000000",
        "range_end": "1000030",
        "schedule_sha256": SCHEDULE_SHA256,
        "table_manifest_sha256": TABLE_MANIFEST_SHA256,
        "build_id": manifest["build_id"],
        "next_index": "1000030",
        "counters": wanted_checkpoint_counters,
        "crc64_ecma": checkpoint["crc64_ecma"],
    }
    if checkpoint != wanted_checkpoint:
        fail(label + " checkpoint identity/counters are wrong")
    if (worker / "certificate.txt").exists():
        fail(label + " unexpectedly retained a certificate")

    return {
        "progress": progress,
        "start": start,
        "summary": summary,
        "checkpoint": checkpoint,
        "outcome": aggregate_outcome(progress),
        "timing_totals": timing_totals(progress),
    }


def validate_fetch_metadata(root, run_id, attempts, label):
    fetch = load_json(root / "fetch-metadata.json")
    if not isinstance(fetch, dict) or fetch.get("schema") != 1:
        fail(label + " fetch metadata schema is wrong")
    if fetch.get("run_id") != run_id or fetch.get("pod_id") != "ohfo3hbov7ot8v":
        fail(label + " fetch identity is wrong")
    if not str(fetch.get("remote_source", "")).endswith("/runs/" + run_id):
        fail(label + " remote fetch source is wrong")
    if parse_utc(fetch.get("fetched_at"), label + " fetch time") < attempts["end"]["epoch"]:
        fail(label + " fetch predates the completed attempt")
    return fetch


def validate_side(root, run_id, commit, binary_sha256, label):
    manifest = load_json(root / "worker-0/manifest.json")
    validate_manifest(manifest, run_id, commit, binary_sha256, label)
    options = parse_options(manifest["command_argv"], label)
    for name, wanted in FIXED_OPTION_VALUES.items():
        if options.get(name) != wanted:
            fail("{} command option mismatch: {}".format(label, name))
    deploy = PurePosixPath(manifest["command_argv"][0]).parent.parent
    remote_worker = PurePosixPath(options["--progress"]).parent
    if PurePosixPath(manifest["command_argv"][0]) != deploy / "build/oneshotsea":
        fail(label + " executable path is wrong")
    if PurePosixPath(options["--table-dir"]) != deploy / "data/modpoly/weber_f":
        fail(label + " table path is wrong")
    for option, filename in (
        ("--checkpoint", "checkpoint.json"),
        ("--progress", "progress.jsonl"),
        ("--certificate-out", "certificate.txt"),
    ):
        if PurePosixPath(options[option]) != remote_worker / filename:
            fail("{} output option mismatch: {}".format(label, option))
    if remote_worker.name != "worker-0" or remote_worker.parent.name != run_id:
        fail(label + " output directory/run id is wrong")
    if options["--build-id"] != manifest["build_id"]:
        fail(label + " command build id differs from its manifest")

    time = time_record(root / "worker-0/resource-usage.txt")
    for name in ("elapsed_seconds", "user_seconds", "system_seconds"):
        require_finite_number(time[name], label + " " + name)
    validate_command_controls(root, manifest, options, time, label)
    attempts = validate_attempts(
        root / "worker-0/attempts.jsonl", manifest, time, label
    )
    records = validate_worker_records(root, manifest, options, time, label)
    fetch = validate_fetch_metadata(root, run_id, attempts, label)
    return {
        "manifest": manifest,
        "options": options,
        "time": time,
        "attempts": attempts,
        "fetch": fetch,
        **records,
    }


def validate_normalized_commands(baseline, candidate):
    dynamic = {
        "--table-dir",
        "--checkpoint",
        "--progress",
        "--certificate-out",
        "--build-id",
    }
    for name in OPTION_NAMES:
        if name not in dynamic and baseline["options"][name] != candidate["options"][name]:
            fail("baseline/candidate command mismatch: " + name)
    if baseline["start"]["resources"] != candidate["start"]["resources"]:
        fail("baseline/candidate resource configuration differs")
    for name in (
        "schedule_sha256",
        "table_manifest_sha256",
        "smooth_cache_sha256",
        "verifier_sha256",
        "python_sha256",
    ):
        if baseline["start"][name] != candidate["start"][name]:
            fail("baseline/candidate start identity differs: " + name)


def validate_baseline_result(baseline):
    result = load_json(BASELINE / "result.json")
    if result.get("schema") != "oneshotsea.runpod-p125-probe.v1":
        fail("unexpected retained baseline result schema")
    run = result.get("run", {})
    expected_run = {
        "run_id": BASELINE_RUN_ID,
        "pod_id": "ohfo3hbov7ot8v",
        "run_kind": "benchmark",
        "prime": PRIME,
        "seed": "202607300000",
        "range": {"start_inclusive": "1000000", "end_exclusive": "1000030", "curves": 30},
        "deployment_commit": BASELINE_COMMIT,
        "binary_sha256": BASELINE_BINARY_SHA256,
        "smooth_cache_sha256": SMOOTH_CACHE_SHA256,
        "command_sha256": baseline["manifest"]["command_sha256"],
        "start_utc": baseline["attempts"]["start"]["utc"],
        "end_utc": baseline["attempts"]["end"]["utc"],
        "exit_status": baseline["time"]["exit_status"],
    }
    if run != expected_run:
        fail("retained baseline result run identity is wrong")
    if result.get("outcome") != baseline["outcome"]:
        fail("retained baseline result outcome is wrong")
    aggregate = result.get("aggregate_curve_work", {})
    sums = {
        "generator_rejections": sum(int(row["generator_rejections"]) for row in baseline["progress"]),
        "sea_levels": sum(int(row["sea_levels"]) for row in baseline["progress"]),
        "exact_sea_levels": sum(int(row["exact_sea_levels"]) for row in baseline["progress"]),
        "atkin_sea_levels": sum(int(row["atkin_sea_levels"]) for row in baseline["progress"]),
        "schoof_fallback_levels": sum(int(row["schoof_fallback_level_count"]) for row in baseline["progress"]),
    }
    for name, wanted in sums.items():
        if aggregate.get(name) != wanted:
            fail("retained baseline aggregate is wrong: " + name)
    if aggregate.get("timings_us", {}).get("generation") != baseline["timing_totals"]["generation"] or aggregate.get("timings_us", {}).get("sea") != baseline["timing_totals"]["sea"] or aggregate.get("timings_us", {}).get("smoothness") != baseline["timing_totals"]["smoothness"] or aggregate.get("timings_us", {}).get("total") != baseline["timing_totals"]["total"]:
        fail("retained baseline timing aggregate is wrong")
    host = result.get("host_resource_record", {})
    for retained, source in (
        ("elapsed_seconds", "elapsed_seconds"),
        ("user_seconds", "user_seconds"),
        ("system_seconds", "system_seconds"),
        ("average_cpu_percent", "average_cpu_percent"),
        ("maximum_resident_set_kbytes", "maximum_resident_set_kbytes"),
        ("swaps", "swaps"),
    ):
        close(baseline["time"][source], host.get(retained), "baseline result " + retained)
    if host.get("attempt_epoch_seconds") != baseline["attempts"]["epoch_seconds"]:
        fail("retained baseline attempt duration is wrong")
    if host.get("maximum_resident_set_bytes") != baseline["time"]["maximum_resident_set_kbytes"] * 1024:
        fail("retained baseline RSS byte conversion is wrong")


def validate_binary(path, wanted_sha256, label):
    if sha256(path) != wanted_sha256:
        fail(label + " retained binary digest is wrong")
    encoded = path.read_bytes()
    if not encoded.startswith(b"\x7fELF"):
        fail(label + " retained binary is not ELF")
    for producer in (COMPILER_PRODUCER, CXX_PRODUCER):
        if encoded.count(producer.encode("ascii") + b"\0") != 1:
            fail(label + " retained binary lacks its unique producer string")
    if " -O2 " not in CXX_PRODUCER or " -std=c++20 " not in CXX_PRODUCER:
        fail("trusted C++ producer policy is malformed")


def validate_result_side(retained, side, artifact, run_id, commit, binary_sha256, binary_path, label):
    expected_keys = {
        "artifact",
        "run_id",
        "deployment_commit",
        "binary_sha256",
        "retained_binary_path",
        "start_utc",
        "end_utc",
        "attempt_epoch_seconds",
        "elapsed_seconds",
        "user_seconds",
        "system_seconds",
        "average_cpu_percent",
        "maximum_resident_set_kbytes",
        "maximum_resident_set_bytes",
        "swaps",
        "exit_status",
        "recorded_hourly_rate_usd",
        "estimated_attempt_compute_cost_usd",
    }
    if not isinstance(retained, dict) or set(retained) != expected_keys:
        fail(label + " retained result fields are wrong")
    exact = {
        "artifact": artifact,
        "run_id": run_id,
        "deployment_commit": commit,
        "binary_sha256": binary_sha256,
        "retained_binary_path": binary_path,
        "start_utc": side["attempts"]["start"]["utc"],
        "end_utc": side["attempts"]["end"]["utc"],
        "attempt_epoch_seconds": side["attempts"]["epoch_seconds"],
        "average_cpu_percent": side["time"]["average_cpu_percent"],
        "maximum_resident_set_kbytes": side["time"]["maximum_resident_set_kbytes"],
        "maximum_resident_set_bytes": side["time"]["maximum_resident_set_kbytes"] * 1024,
        "swaps": side["time"]["swaps"],
        "exit_status": side["time"]["exit_status"],
    }
    for name, wanted in exact.items():
        if retained.get(name) != wanted:
            fail("{}.{} mismatch".format(label, name))
    for name in ("elapsed_seconds", "user_seconds", "system_seconds"):
        close(side["time"][name], retained[name], label + "." + name)
    close(RECORDED_HOURLY_RATE_USD, retained["recorded_hourly_rate_usd"], label + " hourly rate")
    cost = side["time"]["elapsed_seconds"] / 3600.0 * RECORDED_HOURLY_RATE_USD
    close(cost, retained["estimated_attempt_compute_cost_usd"], label + " attempt cost")


def main():
    audit_checksums(BASELINE, BASELINE_REQUIRED_FILES)
    audit_checksums(ROOT, CANDIDATE_REQUIRED_FILES)

    baseline = validate_side(
        BASELINE,
        BASELINE_RUN_ID,
        BASELINE_COMMIT,
        BASELINE_BINARY_SHA256,
        "baseline",
    )
    candidate = validate_side(
        ROOT,
        CANDIDATE_RUN_ID,
        CANDIDATE_COMMIT,
        CANDIDATE_BINARY_SHA256,
        "candidate",
    )
    validate_normalized_commands(baseline, candidate)
    validate_baseline_result(baseline)

    result = load_json(ROOT / "result.json")
    if result.get("schema") != "oneshotsea.runpod-p125-replay.v1":
        fail("unexpected result schema")
    if result.get("classification") != "exact same-30 production-path promotion gate; not a certificate-rate estimate":
        fail("unexpected result classification")
    if result.get("configuration") != EXPECTED_CONFIGURATION:
        fail("retained result configuration is incomplete or wrong")

    validate_binary(ROOT / "binaries/baseline.bin", BASELINE_BINARY_SHA256, "baseline")
    validate_binary(ROOT / "binaries/candidate.bin", CANDIDATE_BINARY_SHA256, "candidate")
    expected_provenance = {
        "compiler_producer": COMPILER_PRODUCER,
        "cxx_producer": CXX_PRODUCER,
        "required_cxx_markers": ["GNU C++20 11.5.0", "-O2", "-std=c++20"],
    }
    if result.get("binary_provenance") != expected_provenance:
        fail("retained binary provenance is wrong")

    validate_result_side(
        result.get("baseline"),
        baseline,
        "artifacts/runpod/p125-runpod-cpu16-probe-20260802b/ohfo3hbov7ot8v",
        BASELINE_RUN_ID,
        BASELINE_COMMIT,
        BASELINE_BINARY_SHA256,
        "binaries/baseline.bin",
        "baseline",
    )
    validate_result_side(
        result.get("candidate"),
        candidate,
        "artifacts/runpod/p125-runpod-cpu16-replay-550815e-20260802a/ohfo3hbov7ot8v",
        CANDIDATE_RUN_ID,
        CANDIDATE_COMMIT,
        CANDIDATE_BINARY_SHA256,
        "binaries/candidate.bin",
        "candidate",
    )

    baseline_projection = [projection(record) for record in baseline["progress"]]
    candidate_projection = [projection(record) for record in candidate["progress"]]
    if baseline_projection != candidate_projection:
        fail("candidate semantic records differ from the baseline")
    digest = projection_digest(baseline_projection)
    semantic = result.get("semantic_gate", {})
    expected_semantic = {
        "projection_excludes": ["peak_rss_bytes", "timings_us", "state.build_id"],
        "baseline_projection_sha256": digest,
        "candidate_projection_sha256": digest,
        "records_identical": True,
        "generator_rejections": sum(int(record["generator_rejections"]) for record in candidate["progress"]),
        "sea_levels": sum(int(record["sea_levels"]) for record in candidate["progress"]),
        "exact_sea_levels": sum(int(record["exact_sea_levels"]) for record in candidate["progress"]),
        "atkin_sea_levels": sum(int(record["atkin_sea_levels"]) for record in candidate["progress"]),
        "schoof_fallback_levels": sum(int(record["schoof_fallback_level_count"]) for record in candidate["progress"]),
    }
    if semantic != expected_semantic:
        fail("retained semantic gate is incomplete or wrong")
    if result.get("outcome") != candidate["outcome"]:
        fail("retained candidate outcome is incomplete or wrong")
    if result.get("timing_totals_us") != {
        "baseline": baseline["timing_totals"],
        "candidate": candidate["timing_totals"],
    }:
        fail("retained timing totals are wrong")

    wall_speedup = baseline["time"]["elapsed_seconds"] / candidate["time"]["elapsed_seconds"]
    total_speedup = baseline["timing_totals"]["total"] / candidate["timing_totals"]["total"]
    sea_speedup = baseline["timing_totals"]["sea"] / candidate["timing_totals"]["sea"]
    baseline_rss = baseline["time"]["maximum_resident_set_kbytes"]
    rss_ratio = candidate["time"]["maximum_resident_set_kbytes"] / baseline_rss
    semantic_passed = baseline_projection == candidate_projection
    wall_passed = wall_speedup > MINIMUM_EXCLUSIVE_WALL_SPEEDUP
    rss_passed = rss_ratio <= MAXIMUM_INCLUSIVE_RSS_RATIO
    zero_swap_passed = baseline["time"]["swaps"] == candidate["time"]["swaps"] == 0
    zero_exit_passed = baseline["time"]["exit_status"] == candidate["time"]["exit_status"] == 0
    accepted = semantic_passed and wall_passed and rss_passed and zero_swap_passed and zero_exit_passed
    expected_gate = {
        "minimum_exclusive_wall_speedup": MINIMUM_EXCLUSIVE_WALL_SPEEDUP,
        "maximum_inclusive_rss_ratio": MAXIMUM_INCLUSIVE_RSS_RATIO,
        "wall_speedup": wall_speedup,
        "embedded_total_speedup": total_speedup,
        "embedded_sea_speedup": sea_speedup,
        "rss_ratio": rss_ratio,
        "rss_delta_kbytes": candidate["time"]["maximum_resident_set_kbytes"] - baseline_rss,
        "semantic_gate_passed": semantic_passed,
        "wall_speed_gate_passed": wall_passed,
        "rss_gate_passed": rss_passed,
        "zero_swap_gate_passed": zero_swap_passed,
        "zero_exit_gate_passed": zero_exit_passed,
        "accepted": accepted,
        "decision": "Promote the combined Kronecker-substitution and reciprocal-reduction production path with quotient-context reuse disabled by default.",
    }
    gate = result.get("promotion_gate")
    if not isinstance(gate, dict) or set(gate) != set(expected_gate):
        fail("retained promotion gate fields are incomplete or wrong")
    for name, wanted in expected_gate.items():
        if isinstance(wanted, float):
            close(wanted, gate[name], "promotion_gate." + name)
        elif gate[name] != wanted:
            fail("retained promotion gate mismatch: " + name)
    if not accepted:
        fail("promotion gate did not pass")

    print(
        "p125 same-30 replay audit ok: semantic SHA-256 {}, wall {:.6f}x, RSS {:.6f}x, cost ${:.6f}".format(
            digest,
            wall_speedup,
            rss_ratio,
            candidate["time"]["elapsed_seconds"] / 3600.0 * RECORDED_HOURLY_RATE_USD,
        )
    )


if __name__ == "__main__":
    main()
