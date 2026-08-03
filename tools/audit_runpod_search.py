#!/usr/bin/env python3
"""Audit one fetched, checksummed RunPod search root.

The auditor is intentionally independent of a retained result summary.  It
recomputes worker topology, command, progress, checkpoint, certificate, and
resource facts from the immutable files produced by launch-worker.sh.
"""

from __future__ import print_function

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import stat
import subprocess
import sys
from typing import Any, Dict, List, Optional, Sequence, Set, Tuple


SCHEMA = "oneshotsea.runpod-search-audit.v2"
PROFILE_SCHEMA = "oneshotsea.runpod-search-audit-profile.v1"
WORKER_SCHEMA = "oneshotsea.runpod-worker.v3"
CHECKPOINT_SCHEMA = 1
PINNED_VERIFIER_SHA256 = (
    "e0ba3b8a7ed2ff48bd2fd824642bf67b0954a9f03f57daeb4ac4302691e1b666"
)
GIT_COMMIT = re.compile(r"[0-9a-f]{40}")
SHA256 = re.compile(r"[0-9a-f]{64}")
WORKER_DIRECTORY = re.compile(r"worker-(0|[1-9][0-9]*)")
UTC = re.compile(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z")
UINT64_MAX = (1 << 64) - 1

CHECKPOINT_COUNTERS = (
    "curves_attempted",
    "rejected_invalid_curve",
    "rejected_sea",
    "rejected_sound_early_abort",
    "rejected_heuristic",
    "rejected_certificate_assembly",
    "completed_without_certificate",
    "full_point_counts_completed",
    "candidates_reaching_smoothness",
    "certificates_found",
)
# Raw implementation-limit/no-lift records deliberately do not retire their
# index.  Their heuristic counterparts do.  This distinction is the core sound
# search invariant and must not be inferred merely from a counter name.
STATUS_SEMANTICS = {
    "no_rational_weber_lift": {
        "counter": None, "advances": False, "heuristic": False,
        "outcome_class": "implementation_no_lift", "sound": False,
        "full": False, "smooth": False,
    },
    "sea_level_limit": {
        "counter": None, "advances": False, "heuristic": False,
        "outcome_class": "implementation_level_limit", "sound": False,
        "full": False, "smooth": None,
    },
    "heuristic_no_lift_skip": {
        "counter": "rejected_heuristic", "advances": True,
        "heuristic": True, "outcome_class": "heuristic_rejection",
        "sound": False, "full": False, "smooth": False,
    },
    "heuristic_level_limit_skip": {
        "counter": "rejected_heuristic", "advances": True,
        "heuristic": True, "outcome_class": "heuristic_rejection",
        "sound": False, "full": False, "smooth": None,
    },
    "sound_smoothness_reject": {
        "counter": "rejected_sound_early_abort", "advances": True,
        "heuristic": False, "outcome_class": "sound_rejection",
        "sound": True, "full": None, "smooth": True,
    },
    "no_certificate_candidate": {
        "counter": "completed_without_certificate", "advances": True,
        "heuristic": False, "outcome_class": "terminal", "sound": False,
        "full": True, "smooth": True,
    },
    "certificate_assembly_failed": {
        "counter": "rejected_certificate_assembly", "advances": True,
        "heuristic": False, "outcome_class": "terminal", "sound": False,
        "full": True, "smooth": True,
    },
    "canonical_verifier_rejected": {
        "counter": "rejected_certificate_assembly", "advances": True,
        "heuristic": False, "outcome_class": "terminal", "sound": False,
        "full": True, "smooth": True,
    },
    "verified_certificate": {
        "counter": "certificates_found", "advances": True,
        "heuristic": False, "outcome_class": "terminal", "sound": False,
        "full": True, "smooth": True,
    },
}
# Kept for in-process topology-auditor compatibility while the richer status
# table above is authoritative.
TERMINAL_COUNTER = {
    name: semantics["counter"] for name, semantics in STATUS_SEMANTICS.items()
    if semantics["counter"] is not None
}

DERIVED_OPTIONS = {
    "--p", "--seed", "--range-start", "--range-end", "--worker-id",
    "--worker-count", "--smooth-cache-sha256", "--checkpoint", "--progress",
    "--certificate-out", "--build-id", "--max-curves",
}
REQUIRED_POLICY_OPTIONS = {
    "--max-level", "--table-dir", "--smooth-cache", "--curve-family",
    "--x1-require-point4", "--curve-threads", "--sea-level-telemetry",
    "--schoof-fallback", "--skip-incomplete-curves",
    "--smooth-coordinators", "--checkpoint-every", "--trace-cap",
    "--sea-threads", "--smooth-threads", "--smooth-max-batch",
    "--smooth-root-auxiliary-bytes", "--smooth-build-segment-span",
    "--assembly-attempts", "--max-certificate-candidates",
    "--max-candidate-search-nodes",
}
OPTIONAL_POLICY_OPTIONS = {"--certificate-seed", "--python"}

GNU_TIME_FIELDS = (
    "Command being timed", "User time (seconds)", "System time (seconds)",
    "Percent of CPU this job got",
    "Elapsed (wall clock) time (h:mm:ss or m:ss)",
    "Average shared text size (kbytes)",
    "Average unshared data size (kbytes)", "Average stack size (kbytes)",
    "Average total size (kbytes)", "Maximum resident set size (kbytes)",
    "Average resident set size (kbytes)",
    "Major (requiring I/O) page faults", "Minor (reclaiming a frame) page faults",
    "Voluntary context switches", "Involuntary context switches", "Swaps",
    "File system inputs", "File system outputs", "Socket messages sent",
    "Socket messages received", "Signals delivered", "Page size (bytes)",
    "Exit status",
)


class AuditError(ValueError):
    """The retained run is unauthenticated, malformed, or inconsistent."""


def _reject_json_constant(value: str) -> Any:
    raise AuditError("non-finite JSON constant: {}".format(value))


def _object(pairs: Sequence[Tuple[str, Any]]) -> Dict[str, Any]:
    value = {}  # type: Dict[str, Any]
    for key, item in pairs:
        if key in value:
            raise AuditError("duplicate JSON key: {!r}".format(key))
        value[key] = item
    return value


def _load_json(path: Path, label: str) -> Dict[str, Any]:
    if not path.is_file() or path.is_symlink():
        raise AuditError("missing or non-regular {}".format(label))
    try:
        with path.open(encoding="utf-8") as stream:
            value = json.load(
                stream, object_pairs_hook=_object,
                parse_constant=_reject_json_constant,
            )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise AuditError("invalid {}: {}".format(label, error))
    if not isinstance(value, dict):
        raise AuditError("{} is not a JSON object".format(label))
    return value


def _load_jsonl(path: Path, label: str) -> List[Dict[str, Any]]:
    if not path.is_file() or path.is_symlink():
        raise AuditError("missing or non-regular {}".format(label))
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as error:
        raise AuditError("cannot read {}: {}".format(label, error))
    values = []  # type: List[Dict[str, Any]]
    for number, line in enumerate(lines, 1):
        if not line.strip():
            raise AuditError("blank JSONL record in {} at line {}".format(label, number))
        try:
            value = json.loads(
                line, object_pairs_hook=_object,
                parse_constant=_reject_json_constant,
            )
        except (AuditError, json.JSONDecodeError) as error:
            raise AuditError(
                "invalid JSONL record in {} at line {}: {}".format(
                    label, number, error
                )
            )
        if not isinstance(value, dict):
            raise AuditError("non-object JSONL record in {} at line {}".format(label, number))
        values.append(value)
    return values


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as error:
        raise AuditError("cannot hash {}: {}".format(path.name, error))
    return digest.hexdigest()


def _canonical_sha256(value: Any) -> str:
    encoded = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def _verify_checksums(root: Path) -> Tuple[int, str]:
    if not root.is_dir() or root.is_symlink():
        raise AuditError("run root is missing or not a real directory")
    manifest_path = root / "SHA256SUMS"
    if not manifest_path.is_file() or manifest_path.is_symlink():
        raise AuditError("run root has no regular SHA256SUMS")
    try:
        lines = manifest_path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as error:
        raise AuditError("cannot read SHA256SUMS: {}".format(error))
    if not lines:
        raise AuditError("SHA256SUMS is empty")
    listed = set()  # type: Set[Path]
    for number, line in enumerate(lines, 1):
        match = re.fullmatch(r"([0-9a-f]{64})[ \t]+([ *])(.+)", line)
        if match is None:
            raise AuditError("malformed SHA256SUMS line {}".format(number))
        wanted, _mode, raw_relative = match.groups()
        relative = Path(raw_relative)
        parts = tuple(part for part in relative.parts if part != ".")
        normalized = Path(*parts)
        if (
            relative.is_absolute()
            or not parts
            or ".." in parts
            or normalized.name == "SHA256SUMS"
            or raw_relative != "./{}".format(normalized.as_posix())
        ):
            raise AuditError("unsafe SHA256SUMS path on line {}".format(number))
        if normalized in listed:
            raise AuditError("duplicate SHA256SUMS path: {}".format(normalized))
        listed.add(normalized)
        target = root / normalized
        if not target.is_file() or target.is_symlink():
            raise AuditError("missing or non-regular checksummed file: {}".format(normalized))
        if _sha256_file(target) != wanted:
            raise AuditError("checksum mismatch: {}".format(normalized))
    actual = set()  # type: Set[Path]
    for path in root.rglob("*"):
        try:
            mode = path.lstat().st_mode
        except OSError as error:
            raise AuditError("cannot inspect run-root entry {}: {}".format(
                path.relative_to(root), error))
        if stat.S_ISLNK(mode):
            raise AuditError("symlink present in run root: {}".format(path.relative_to(root)))
        if stat.S_ISDIR(mode):
            continue
        if not stat.S_ISREG(mode):
            raise AuditError("non-regular entry present in run root: {}".format(
                path.relative_to(root)))
        if path != manifest_path:
            actual.add(path.relative_to(root))
    if listed != actual:
        raise AuditError(
            "SHA256SUMS does not cover the exact file set "
            "(unlisted={}, nonexistent={})".format(
                sorted(str(path) for path in actual - listed),
                sorted(str(path) for path in listed - actual),
            )
        )
    return len(listed), _sha256_file(manifest_path)


def _integer(
    value: Any, label: str, minimum: int = 0, maximum: Optional[int] = None,
) -> int:
    if isinstance(value, bool):
        raise AuditError("{} is not an integer".format(label))
    try:
        result = int(value)
    except (TypeError, ValueError):
        raise AuditError("{} is not an integer".format(label))
    if not isinstance(value, int) and str(result) != str(value):
        raise AuditError("{} is not a canonical integer".format(label))
    if result < minimum:
        raise AuditError("{} is below {}".format(label, minimum))
    if maximum is not None and result > maximum:
        raise AuditError("{} exceeds {}".format(label, maximum))
    return result


def _signed_integer(value: Any, label: str) -> int:
    if isinstance(value, bool):
        raise AuditError("{} is not an integer".format(label))
    try:
        result = int(value)
    except (TypeError, ValueError):
        raise AuditError("{} is not an integer".format(label))
    if not isinstance(value, int) and str(result) != str(value):
        raise AuditError("{} is not a canonical integer".format(label))
    return result


def _finite_number(value: Any, label: str, minimum: float = 0.0) -> float:
    if isinstance(value, bool):
        raise AuditError("{} is not a finite number".format(label))
    try:
        result = float(value)
    except (TypeError, ValueError):
        raise AuditError("{} is not a finite number".format(label))
    if not math.isfinite(result) or result < minimum:
        raise AuditError("{} is not finite and at least {}".format(label, minimum))
    return result


def _utc_epoch(value: Any, label: str) -> int:
    if not isinstance(value, str) or UTC.fullmatch(value) is None:
        raise AuditError("{} is malformed".format(label))
    try:
        parsed = datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ").replace(
            tzinfo=timezone.utc)
    except ValueError as error:
        raise AuditError("{} is not a real UTC timestamp: {}".format(label, error))
    return int(parsed.timestamp())


def _boolean(value: Any, label: str) -> bool:
    if not isinstance(value, bool):
        raise AuditError("{} is not a Boolean".format(label))
    return value


def _range(value: Any, label: str) -> Dict[str, int]:
    if not isinstance(value, dict):
        raise AuditError("{} is not an object".format(label))
    start = _integer(value.get("start"), label + ".start")
    end = _integer(value.get("end"), label + ".end")
    count = _integer(value.get("count"), label + ".count")
    if end < start or count != end - start:
        raise AuditError("{} has inconsistent half-open bounds".format(label))
    return {"start": start, "end": end, "count": count}


def _outside_root(path: Path, root: Path, label: str) -> None:
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError:
        return
    raise AuditError("{} must be outside the audited run root".format(label))


def _remote_path(value: Any, label: str) -> PurePosixPath:
    if not isinstance(value, str) or not value.startswith("/"):
        raise AuditError("{} is not an absolute POSIX path".format(label))
    path = PurePosixPath(value)
    if str(path) != value or ".." in path.parts or path == PurePosixPath("/"):
        raise AuditError("{} is not a canonical safe POSIX path".format(label))
    return path


def _remote_child(value: Any, root: PurePosixPath, label: str) -> PurePosixPath:
    path = _remote_path(value, label)
    try:
        path.relative_to(root)
    except ValueError:
        raise AuditError("{} is outside the trusted remote root".format(label))
    return path


def _counter_object(value: Any, label: str) -> Dict[str, int]:
    if not isinstance(value, dict) or set(value) != set(CHECKPOINT_COUNTERS):
        raise AuditError("{} has an unexpected counter field set".format(label))
    return {
        name: _integer(value[name], "{} {}".format(label, name))
        for name in CHECKPOINT_COUNTERS
    }


def _profile(path: Path, root: Path) -> Tuple[Dict[str, Any], str]:
    _outside_root(path, root, "audit profile")
    value = _load_json(path, "audit profile")
    required = {
        "schema", "run_id", "run_kind", "prime", "seed",
        "deployment_commit", "binary_sha256", "smooth_cache_sha256",
        "schedule_sha256", "table_manifest_sha256", "verifier_sha256",
        "python_executable", "python_sha256", "remote_root",
        "working_directory", "executable_path", "worker_count",
        "global_range", "wall_time_limit_seconds", "option_policy",
        "allow_resume", "allow_timeout_124", "expected_attempt_statuses",
        "expected_checkpoint_next_indices", "minimum_completed_count",
        "expected_counters", "minimum_aggregate_curves_per_hour",
        "maximum_worker_rss_bytes", "maximum_swaps_sum",
        "require_all_assigned_ranges_exhausted",
    }
    if set(value) != required:
        raise AuditError("audit profile has an unexpected field set")
    if value.get("schema") != PROFILE_SCHEMA:
        raise AuditError("audit profile has the wrong schema")
    for name in ("run_id", "prime", "seed", "python_executable"):
        if not isinstance(value.get(name), str) or not value[name]:
            raise AuditError("audit profile has invalid {}".format(name))
    if value.get("run_kind") not in ("benchmark", "production"):
        raise AuditError("audit profile run_kind is not benchmark or production")
    if GIT_COMMIT.fullmatch(str(value.get("deployment_commit", ""))) is None:
        raise AuditError("audit profile deployment_commit is malformed")
    for name in (
        "binary_sha256", "smooth_cache_sha256", "schedule_sha256",
        "table_manifest_sha256", "verifier_sha256", "python_sha256",
    ):
        if SHA256.fullmatch(str(value.get(name, ""))) is None:
            raise AuditError("audit profile {} is malformed".format(name))
    if value["verifier_sha256"] != PINNED_VERIFIER_SHA256:
        raise AuditError("audit profile does not pin the canonical verifier")
    prime = _integer(value["prime"], "audit profile prime", minimum=5)
    if prime % 2 == 0:
        raise AuditError("audit profile target is even")
    _integer(value["seed"], "audit profile seed", maximum=UINT64_MAX)
    worker_count = _integer(
        value["worker_count"], "audit profile worker_count", minimum=1,
        maximum=UINT64_MAX,
    )
    global_range = _range(value["global_range"], "audit profile global_range")
    if global_range["count"] == 0 or worker_count > global_range["count"]:
        raise AuditError("audit profile has an empty worker partition")
    value["worker_count"] = worker_count
    value["global_range"] = global_range
    value["wall_time_limit_seconds"] = _integer(
        value["wall_time_limit_seconds"], "audit profile wall limit",
        maximum=UINT64_MAX,
    )
    for name in ("allow_resume", "allow_timeout_124",
                 "require_all_assigned_ranges_exhausted"):
        value[name] = _boolean(value[name], "audit profile " + name)
    policy = value.get("option_policy")
    if not isinstance(policy, dict) or not all(
        isinstance(name, str) and isinstance(argument, str)
        for name, argument in policy.items()
    ):
        raise AuditError("audit profile option_policy is not a string map")
    if set(policy) & DERIVED_OPTIONS:
        raise AuditError("audit profile option_policy contains derived options")
    if not REQUIRED_POLICY_OPTIONS.issubset(policy):
        raise AuditError("audit profile option_policy omits required options")
    if not set(policy).issubset(REQUIRED_POLICY_OPTIONS | OPTIONAL_POLICY_OPTIONS):
        raise AuditError("audit profile option_policy contains unknown options")
    for name in ("--x1-require-point4", "--sea-level-telemetry",
                 "--schoof-fallback", "--skip-incomplete-curves"):
        if policy[name] not in ("0", "1"):
            raise AuditError("audit profile {} is not Boolean".format(name))
    for name in REQUIRED_POLICY_OPTIONS - {
        "--table-dir", "--smooth-cache", "--curve-family",
        "--x1-require-point4", "--sea-level-telemetry", "--schoof-fallback",
        "--skip-incomplete-curves",
    }:
        minimum = 0 if name in ("--smooth-coordinators",) else 1
        _integer(policy[name], "audit profile " + name, minimum=minimum,
                 maximum=UINT64_MAX)
    if policy["--curve-family"] not in ("weber-f", "x1-11", "x1-27"):
        raise AuditError("audit profile curve family is invalid")
    remote_root = _remote_path(value["remote_root"], "audit profile remote_root")
    # RUNPOD_REMOTE_ROOT owns durable run artifacts, while immutable deployment
    # snapshots are intentionally published as sibling directories below the
    # same workspace parent (for example OneShotSEA and OneShotSEA-<commit>).
    # Pin every exact path in the external profile, but admit only that common
    # non-root parent rather than incorrectly requiring the deploy snapshot to
    # be nested inside the mutable run root.
    workspace_root = remote_root.parent
    if workspace_root == PurePosixPath("/"):
        raise AuditError("audit profile remote root has an unsafe parent")
    _remote_child(value["working_directory"], workspace_root, "working directory")
    _remote_child(value["executable_path"], workspace_root, "executable path")
    _remote_child(policy["--table-dir"], workspace_root, "table directory")
    _remote_child(policy["--smooth-cache"], workspace_root, "smooth cache")
    statuses = value.get("expected_attempt_statuses")
    if not isinstance(statuses, list) or len(statuses) != worker_count:
        raise AuditError("audit profile expected_attempt_statuses has wrong length")
    normalized_statuses = []  # type: List[List[int]]
    for worker_id, worker_statuses in enumerate(statuses):
        if not isinstance(worker_statuses, list) or not worker_statuses:
            raise AuditError("audit profile worker {} has no expected attempts".format(worker_id))
        parsed = [
            _integer(item, "audit profile attempt status", maximum=255)
            for item in worker_statuses
        ]
        if any(item not in (0, 124) for item in parsed):
            raise AuditError("audit profile permits an unsupported exit status")
        if len(parsed) > 1 and not value["allow_resume"]:
            raise AuditError("audit profile has resumed attempts but disallows resume")
        if 124 in parsed and not value["allow_timeout_124"]:
            raise AuditError("audit profile expects timeout 124 but disallows it")
        normalized_statuses.append(parsed)
    value["expected_attempt_statuses"] = normalized_statuses
    cursors = value.get("expected_checkpoint_next_indices")
    if not isinstance(cursors, list) or len(cursors) != worker_count:
        raise AuditError("audit profile expected cursor list has wrong length")
    value["expected_checkpoint_next_indices"] = [
        _integer(item, "audit profile expected cursor", maximum=UINT64_MAX)
        for item in cursors
    ]
    value["minimum_completed_count"] = _integer(
        value["minimum_completed_count"], "audit profile minimum completed",
        maximum=UINT64_MAX,
    )
    value["expected_counters"] = _counter_object(
        value["expected_counters"], "audit profile expected counters")
    value["minimum_aggregate_curves_per_hour"] = _finite_number(
        value["minimum_aggregate_curves_per_hour"],
        "audit profile minimum throughput")
    value["maximum_worker_rss_bytes"] = _integer(
        value["maximum_worker_rss_bytes"], "audit profile maximum RSS", minimum=1)
    value["maximum_swaps_sum"] = _integer(
        value["maximum_swaps_sum"], "audit profile maximum swaps")
    return value, _sha256_file(path)


def _partition(start: int, end: int, worker_id: int, worker_count: int) -> Dict[str, int]:
    count = end - start
    base = count // worker_count
    remainder = count % worker_count
    offset = worker_id * base + min(worker_id, remainder)
    size = base + (1 if worker_id < remainder else 0)
    return {"start": start + offset, "end": start + offset + size, "count": size}


def _argv_options(value: Any, label: str) -> Tuple[List[str], Dict[str, str]]:
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise AuditError("{} command_argv is not a string array".format(label))
    argv = list(value)
    if len(argv) < 4 or argv[1] != "search" or (len(argv) - 2) % 2:
        raise AuditError("{} command is not a flag/value search invocation".format(label))
    options = {}  # type: Dict[str, str]
    for position in range(2, len(argv), 2):
        name = argv[position]
        argument = argv[position + 1]
        if not name.startswith("--") or name in options:
            raise AuditError("{} has malformed or duplicate option {}".format(label, name))
        options[name] = argument
    return argv, options


def _canonical_command_script(
    working_directory: str, worker_directory: str, launch: List[str],
) -> str:
    worker = PurePosixPath(worker_directory)
    attempts = str(worker / "attempts.jsonl")
    resource = str(worker / "resource-usage.txt")
    log = str(worker / "worker.log")
    joined = " ".join(shlex.quote(item) for item in launch)
    return (
        "#!/usr/bin/env bash\n"
        "set -uo pipefail\n"
        "cd {}\n"
        "started_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)\n"
        "started_epoch=$(date +%s)\n"
        "printf '{{\"event\":\"start\",\"utc\":\"%s\",\"epoch\":%s}}\n' "
        "\"$started_utc\" \"$started_epoch\" >>{}\n"
        "printf 'attempt_start utc=%s epoch=%s\n' "
        "\"$started_utc\" \"$started_epoch\" >>{}\n"
        "set +e\n"
        "/usr/bin/time -a -v -o {} -- {} >>{} 2>&1\n"
        "status=$?\n"
        "set -e\n"
        "ended_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)\n"
        "ended_epoch=$(date +%s)\n"
        "printf '{{\"event\":\"end\",\"utc\":\"%s\",\"epoch\":%s,"
        "\"status\":%s}}\n' \"$ended_utc\" \"$ended_epoch\" \"$status\" >>{}\n"
        "exit \"$status\"\n"
    ).format(
        shlex.quote(working_directory), shlex.quote(attempts),
        shlex.quote(resource), shlex.quote(resource), joined,
        shlex.quote(log), shlex.quote(attempts),
    )


def _command_tokens(
    path: Path, label: str, expected_script: Optional[str] = None,
) -> List[str]:
    if not path.is_file() or path.is_symlink():
        raise AuditError("missing or non-regular {} command.sh".format(label))
    try:
        content = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise AuditError("cannot read {} command.sh: {}".format(label, error))
    if expected_script is not None and content != expected_script:
        raise AuditError("{} command.sh differs from the canonical launcher wrapper".format(label))
    lines = content.splitlines()
    command_lines = [line for line in lines if line.startswith("/usr/bin/time ")]
    if len(command_lines) != 1:
        raise AuditError("{} command.sh has {} GNU-time command lines".format(label, len(command_lines)))
    try:
        tokens = shlex.split(command_lines[0])
    except ValueError as error:
        raise AuditError("cannot tokenize {} command.sh: {}".format(label, error))
    if len(tokens) < 9 or tokens[:3] != ["/usr/bin/time", "-a", "-v"]:
        raise AuditError("{} command.sh does not use the expected GNU time prefix".format(label))
    if tokens[3] != "-o" or tokens[5] != "--":
        raise AuditError("{} command.sh has malformed GNU time output arguments".format(label))
    if not tokens[-2].startswith(">>") or tokens[-1] != "2>&1":
        raise AuditError("{} command.sh has malformed worker.log redirection".format(label))
    resource_path = Path(tokens[4])
    log_path = Path(tokens[-2][2:])
    if resource_path.name != "resource-usage.txt" or resource_path.parent.name != label:
        raise AuditError("{} command.sh writes GNU time to the wrong worker path".format(label))
    if log_path.name != "worker.log" or log_path.parent.name != label:
        raise AuditError("{} command.sh redirects output to the wrong worker path".format(label))
    return tokens[6:-2]


def _expected_launch(argv: List[str], wall_limit: int) -> List[str]:
    if wall_limit == 0:
        return list(argv)
    return [
        "timeout",
        "--signal=TERM",
        "--kill-after=60",
        str(wall_limit),
    ] + list(argv)


def _elapsed_seconds(value: str, label: str) -> float:
    fields = value.split(":")
    if len(fields) not in (2, 3):
        raise AuditError("{} has malformed GNU-time elapsed value".format(label))
    try:
        elapsed = sum(
            float(item) * (60 ** position)
            for position, item in enumerate(reversed(fields))
        )
    except ValueError:
        raise AuditError("{} has malformed GNU-time elapsed value".format(label))
    if not math.isfinite(elapsed) or elapsed <= 0:
        raise AuditError("{} has nonpositive GNU-time elapsed value".format(label))
    return elapsed


def _resource_usage(
    path: Path, label: str, expected_launch: List[str],
    attempts: Any, wall_limit: Any, *legacy: Any,
) -> Any:
    legacy_single = bool(legacy)
    if legacy_single:
        # Historical private API:
        # (start_epoch, end_epoch, start_utc, status, wall_limit).
        if len(legacy) != 3:
            raise AuditError("{} legacy resource arguments are malformed".format(label))
        start_epoch = _integer(attempts, label + " legacy start epoch")
        end_epoch = _integer(wall_limit, label + " legacy end epoch")
        start_utc, status, legacy_wall = legacy
        attempts = [{
            "start_utc": start_utc, "end_utc": "",
            "start_epoch": start_epoch, "end_epoch": end_epoch,
            "status": _integer(status, label + " legacy status"),
        }]
        wall_limit = _integer(legacy_wall, label + " legacy wall limit")
    if not path.is_file() or path.is_symlink():
        raise AuditError("missing or non-regular {} resource-usage.txt".format(label))
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise AuditError("cannot read {} resource record: {}".format(label, error))

    if legacy_single:
        attempt = attempts[0]
        def legacy_one(pattern: str, field: str) -> str:
            matches = re.findall(pattern, text, flags=re.MULTILINE)
            if len(matches) != 1:
                raise AuditError("{} resource record has {} {} values".format(
                    label, len(matches), field))
            return matches[0]
        starts = re.findall(
            r"^attempt_start utc=(\S+) epoch=([0-9]+)$", text,
            flags=re.MULTILINE)
        if len(starts) != 1 or starts[0][0] != attempt["start_utc"] or (
            _integer(starts[0][1], label + " resource start epoch") !=
            attempt["start_epoch"]
        ):
            raise AuditError("{} resource and attempt starts differ".format(label))
        timed_text = legacy_one(
            r'^\s*Command being timed: "(.*)"\s*$', "timed-command")
        try:
            timed = shlex.split(timed_text)
        except ValueError as error:
            raise AuditError("cannot tokenize {} GNU-time argv: {}".format(label, error))
        if timed != expected_launch:
            raise AuditError("{} GNU-time argv differs from manifest command_argv".format(label))
        elapsed = _elapsed_seconds(legacy_one(
            r"^\s*Elapsed \(wall clock\) time \(h:mm:ss or m:ss\):\s*(\S+)\s*$",
            "elapsed"), label)
        if abs(attempt["end_epoch"] - attempt["start_epoch"] - elapsed) > 2.0:
            raise AuditError("{} attempt and GNU-time elapsed values differ".format(label))
        rss_kib = _integer(legacy_one(
            r"^\s*Maximum resident set size \(kbytes\):\s*([0-9]+)\s*$",
            "maximum-RSS"), label + " maximum RSS", minimum=1)
        swaps = _integer(legacy_one(
            r"^\s*Swaps:\s*([0-9]+)\s*$", "swap"), label + " swaps")
        exit_status = _integer(legacy_one(
            r"^\s*Exit status:\s*([0-9]+)\s*$", "exit-status"),
            label + " exit status")
        if exit_status != attempt["status"]:
            raise AuditError("{} GNU-time and attempt exit statuses differ".format(label))
        if exit_status == 124 and (
            wall_limit == 0 or elapsed < max(0, wall_limit - 2) or
            elapsed > wall_limit + 62
        ):
            raise AuditError("{} status 124 is inconsistent with its wall limit".format(label))
        return {
            "attempt_start_epoch": attempt["start_epoch"],
            "attempt_end_epoch": attempt["end_epoch"],
            "elapsed_seconds": elapsed,
            "maximum_resident_set_kib": rss_kib,
            "peak_rss_bytes": rss_kib * 1024,
            "swaps": swaps, "exit_status": exit_status,
        }

    if not text.endswith("\n"):
        raise AuditError("{} resource record lacks a final newline".format(label))
    raw_blocks = []  # type: List[List[str]]
    for line in text.splitlines():
        if line.startswith("attempt_start "):
            raw_blocks.append([line])
        elif not raw_blocks:
            raise AuditError("{} resource record has data before its first attempt".format(label))
        else:
            raw_blocks[-1].append(line)
    if len(raw_blocks) != len(attempts):
        raise AuditError("{} resource/attempt block counts differ".format(label))
    parsed_blocks = []  # type: List[Dict[str, Any]]
    integer_fields = set(GNU_TIME_FIELDS[5:]) - {"Exit status"}
    for attempt_number, (lines, attempt) in enumerate(zip(raw_blocks, attempts), 1):
        start_match = re.fullmatch(
            r"attempt_start utc=(\S+) epoch=([0-9]+)", lines[0])
        if start_match is None:
            raise AuditError("{} resource attempt {} has malformed start".format(
                label, attempt_number))
        recorded_utc, recorded_epoch = start_match.groups()
        if recorded_utc != attempt["start_utc"] or _integer(
            recorded_epoch, label + " resource start epoch") != attempt["start_epoch"]:
            raise AuditError("{} resource and attempt starts differ".format(label))
        field_lines = lines[1:]
        nonzero_line = "Command exited with non-zero status {}".format(
            attempt["status"])
        if attempt["status"] != 0:
            if not field_lines or field_lines[0] != nonzero_line:
                raise AuditError("{} GNU-time nonzero-status preamble differs".format(label))
            field_lines = field_lines[1:]
        elif field_lines and field_lines[0].startswith("Command exited with non-zero status"):
            raise AuditError("{} successful GNU-time block has a failure preamble".format(label))
        fields = {}  # type: Dict[str, str]
        order = []  # type: List[str]
        for line in field_lines:
            if not line.startswith("\t"):
                raise AuditError("{} GNU-time line is not tab-indented".format(label))
            body = line[1:]
            matched = None
            for name in GNU_TIME_FIELDS:
                prefix = name + ": "
                if body.startswith(prefix):
                    matched = name
                    argument = body[len(prefix):]
                    break
            if matched is None or matched in fields:
                raise AuditError("{} GNU-time block has an unknown/duplicate field".format(label))
            fields[matched] = argument
            order.append(matched)
        if tuple(order) != GNU_TIME_FIELDS:
            raise AuditError("{} GNU-time block has an unexpected field sequence".format(label))
        timed_text = fields["Command being timed"]
        if len(timed_text) < 2 or timed_text[0] != '"' or timed_text[-1] != '"':
            raise AuditError("{} GNU-time command is not quoted".format(label))
        try:
            timed = shlex.split(timed_text[1:-1])
        except ValueError as error:
            raise AuditError("cannot tokenize {} GNU-time argv: {}".format(label, error))
        if timed != expected_launch:
            raise AuditError("{} GNU-time argv differs from manifest command_argv".format(label))
        user_seconds = _finite_number(
            fields["User time (seconds)"], label + " user seconds")
        system_seconds = _finite_number(
            fields["System time (seconds)"], label + " system seconds")
        cpu_match = re.fullmatch(r"([0-9]+)%", fields["Percent of CPU this job got"])
        if cpu_match is None:
            raise AuditError("{} GNU-time CPU percentage is malformed".format(label))
        cpu_percent = _integer(cpu_match.group(1), label + " CPU percentage")
        elapsed = _elapsed_seconds(
            fields["Elapsed (wall clock) time (h:mm:ss or m:ss)"], label)
        numeric = {
            name: _integer(fields[name], "{} GNU-time {}".format(label, name))
            for name in integer_fields
        }
        rss_kib = numeric["Maximum resident set size (kbytes)"]
        if rss_kib == 0:
            raise AuditError("{} maximum RSS is zero".format(label))
        exit_status = _integer(fields["Exit status"], label + " exit status", maximum=255)
        if exit_status != attempt["status"]:
            raise AuditError("{} GNU-time and attempt exit statuses differ".format(label))
        attempt_elapsed = attempt["end_epoch"] - attempt["start_epoch"]
        if abs(attempt_elapsed - elapsed) > 2.0:
            raise AuditError("{} attempt and GNU-time elapsed values differ".format(label))
        if wall_limit != 0 and exit_status == 0 and elapsed > wall_limit + 2:
            raise AuditError("{} successful attempt exceeded its wall limit".format(label))
        if exit_status == 124 and (
            wall_limit == 0 or elapsed < max(0, wall_limit - 2) or
            elapsed > wall_limit + 62
        ):
            raise AuditError("{} status 124 is inconsistent with its wall limit".format(label))
        parsed_blocks.append({
            "attempt_start_epoch": attempt["start_epoch"],
            "attempt_end_epoch": attempt["end_epoch"],
            "elapsed_seconds": elapsed,
            "user_seconds": user_seconds,
            "system_seconds": system_seconds,
            "average_cpu_percent": cpu_percent,
            "maximum_resident_set_kib": rss_kib,
            "peak_rss_bytes": rss_kib * 1024,
            "swaps": numeric["Swaps"],
            "exit_status": exit_status,
        })
    return parsed_blocks


def _attempts(path: Path, label: str, started_utc: Any) -> List[Dict[str, Any]]:
    values = _load_jsonl(path, label + " attempts")
    if not values or len(values) % 2:
        raise AuditError("{} must contain completed start/end attempt pairs".format(label))
    attempts = []  # type: List[Dict[str, Any]]
    previous_end = None  # type: Optional[int]
    manifest_started_epoch = _utc_epoch(started_utc, label + " manifest start UTC")
    for position in range(0, len(values), 2):
        start, end = values[position:position + 2]
        if start.get("event") != "start" or end.get("event") != "end":
            raise AuditError("{} attempt records are not ordered start/end pairs".format(label))
        if set(start) != {"event", "utc", "epoch"} or set(end) != {
            "event", "utc", "epoch", "status"
        }:
            raise AuditError("{} attempt records have an unexpected field set".format(label))
        start_utc_epoch = _utc_epoch(start.get("utc"), label + " attempt start UTC")
        end_utc_epoch = _utc_epoch(end.get("utc"), label + " attempt end UTC")
        start_epoch = _integer(start.get("epoch"), label + " start epoch")
        end_epoch = _integer(end.get("epoch"), label + " end epoch")
        status = _integer(end.get("status"), label + " attempt status", maximum=255)
        if start_utc_epoch != start_epoch or end_utc_epoch != end_epoch:
            raise AuditError("{} UTC and epoch differ".format(label))
        if end_epoch <= start_epoch or (
            previous_end is not None and start_epoch < previous_end
        ):
            raise AuditError("{} attempts overlap or have nonpositive duration".format(label))
        if not attempts and not (
            manifest_started_epoch <= start_epoch <= manifest_started_epoch + 60
        ):
            raise AuditError(
                "{} first attempt does not start within 60 seconds of the manifest".format(label)
            )
        attempts.append({
            "start_utc": start["utc"], "end_utc": end["utc"],
            "start_epoch": start_epoch, "end_epoch": end_epoch,
            "status": status,
        })
        previous_end = end_epoch
    return attempts


def _attempt(path: Path, label: str, started_utc: Any) -> Dict[str, Any]:
    """Backward-compatible single-attempt helper."""
    attempts = _attempts(path, label, started_utc)
    if len(attempts) != 1:
        raise AuditError("{} does not contain exactly one attempt".format(label))
    return attempts[0]


def _crc64_ecma(payload: bytes) -> int:
    crc = 0
    polynomial = 0x42F0E1EBA9EA3693
    for byte in payload:
        crc ^= byte << 56
        for _bit in range(8):
            high = crc & 0x8000000000000000
            crc = (crc << 1) & 0xFFFFFFFFFFFFFFFF
            if high:
                crc ^= polynomial
    return crc


def _checkpoint(path: Path, label: str) -> Tuple[Dict[str, Any], Dict[str, int], str]:
    if not path.is_file() or path.is_symlink():
        raise AuditError("missing or non-regular {} checkpoint".format(label))
    try:
        encoded = path.read_bytes()
    except OSError as error:
        raise AuditError("cannot read {} checkpoint: {}".format(label, error))
    marker = b',"crc64_ecma":"'
    position = encoded.rfind(marker)
    if not encoded.endswith(b'"}\n') or position < 0:
        raise AuditError("{} checkpoint checksum field is malformed".format(label))
    crc_start = position + len(marker)
    recorded_bytes = encoded[crc_start:crc_start + 16]
    if crc_start + 19 != len(encoded) or re.fullmatch(b"[0-9a-f]{16}", recorded_bytes) is None:
        raise AuditError("{} checkpoint checksum field is malformed".format(label))
    payload = encoded[:position] + b"}"
    recorded_crc = int(recorded_bytes.decode("ascii"), 16)
    if _crc64_ecma(payload) != recorded_crc:
        raise AuditError("{} checkpoint CRC64-ECMA mismatch".format(label))
    try:
        value = json.loads(encoded.decode("utf-8"), object_pairs_hook=_object)
    except (UnicodeError, json.JSONDecodeError, AuditError) as error:
        raise AuditError("invalid {} checkpoint JSON: {}".format(label, error))
    if not isinstance(value, dict):
        raise AuditError("{} checkpoint is not an object".format(label))
    required = {
        "schema_version", "prime", "seed", "worker_id", "worker_count",
        "range_start", "range_end", "schedule_sha256",
        "table_manifest_sha256", "build_id", "next_index", "counters",
        "crc64_ecma",
    }
    if set(value) != required:
        raise AuditError("{} checkpoint has an unexpected field set".format(label))
    if value.get("schema_version") != CHECKPOINT_SCHEMA:
        raise AuditError("{} checkpoint schema is not version 1".format(label))
    counters_value = value.get("counters")
    if not isinstance(counters_value, dict) or set(counters_value) != set(CHECKPOINT_COUNTERS):
        raise AuditError("{} checkpoint counters have an unexpected field set".format(label))
    counters = {
        name: _integer(counters_value.get(name), label + " checkpoint " + name)
        for name in CHECKPOINT_COUNTERS
    }
    # The producer writes one canonical field order.  Comparing the rebuilt
    # payload prevents a recomputed CRC from blessing a noncanonical encoding.
    canonical = (
        '{{"schema_version":{},"prime":"{}","seed":"{}",'
        '"worker_id":"{}","worker_count":"{}","range_start":"{}",'
        '"range_end":"{}","schedule_sha256":"{}",'
        '"table_manifest_sha256":"{}","build_id":"{}",'
        '"next_index":"{}","counters":{{{}}}}}'
    ).format(
        value["schema_version"], value["prime"], value["seed"],
        value["worker_id"], value["worker_count"], value["range_start"],
        value["range_end"], value["schedule_sha256"],
        value["table_manifest_sha256"], value["build_id"],
        value["next_index"],
        ",".join('"{}":"{}"'.format(name, value["counters"][name]) for name in CHECKPOINT_COUNTERS),
    ).encode("utf-8")
    if payload != canonical:
        raise AuditError("{} checkpoint is not in canonical producer encoding".format(label))
    return value, counters, _sha256_file(path)


def _progress_identity(
    manifest: Dict[str, Any], assigned: Dict[str, int], start: Dict[str, Any]
) -> Dict[str, Any]:
    return {
        "schema": "oneshotsea.search-progress.v1",
        "prime": manifest["prime"],
        "seed": manifest["seed"],
        "worker_id": str(manifest["worker_id"]),
        "worker_count": str(manifest["worker_count"]),
        "range_start": str(assigned["start"]),
        "range_end": str(assigned["end"]),
        "schedule_sha256": start["schedule_sha256"],
        "table_manifest_sha256": start["table_manifest_sha256"],
        "build_id": manifest["build_id"],
    }


def _state_counters(counters: Dict[str, int]) -> Dict[str, Any]:
    return {
        "curves_attempted": str(counters["curves_attempted"]),
        "rejections": {
            "invalid_curve": str(counters["rejected_invalid_curve"]),
            "sea": str(counters["rejected_sea"]),
            "sound_early_abort": str(counters["rejected_sound_early_abort"]),
            "heuristic": str(counters["rejected_heuristic"]),
            "certificate_assembly": str(counters["rejected_certificate_assembly"]),
        },
        "completed_without_certificate": str(counters["completed_without_certificate"]),
        "full_point_counts_completed": str(counters["full_point_counts_completed"]),
        "candidates_reaching_smoothness": str(counters["candidates_reaching_smoothness"]),
        "certificates_found": str(counters["certificates_found"]),
    }


def _expected_state(
    identity: Dict[str, Any], counters: Dict[str, int], next_index: int,
    complete: bool,
) -> Dict[str, Any]:
    value = dict(identity)
    value["next_index"] = str(next_index)
    value["complete"] = complete
    value["counters"] = _state_counters(counters)
    return value


def _string_uint(value: Any, label: str, minimum: int = 0) -> int:
    if not isinstance(value, str):
        raise AuditError("{} is not a canonical integer string".format(label))
    return _integer(value, label, minimum=minimum)


def _validate_sea_level(value: Any, label: str, standalone: bool = False) -> None:
    if not isinstance(value, dict):
        raise AuditError("{} is not an object".format(label))
    keys = {
        "pass", "ell", "exact", "exact_modulus", "constraint_modulus",
        "exact_trace_candidate_count", "trace_candidate_count",
        "atkin_projective_order", "atkin_residue_count",
        "compatible_source_lifts", "modular_root_workers",
        "modular_root_orbits", "modular_root_reused_lifts",
        "modular_root_orbit_reuse", "source_lifts_us", "modular_roots_us",
        "normalized_codomain_us", "bmss_us", "eigenvalue_us",
        "conjugate_eigenvalue_reuse", "eigenvalue_attempts",
        "independent_eigenvalue_recoveries", "conjugate_eigenvalues_derived",
    }
    if standalone:
        keys |= {"schema", "index"}
    if "trace_residue" in value:
        keys.add("trace_residue")
    if set(value) != keys:
        raise AuditError("{} has an unexpected field set".format(label))
    if standalone:
        if value["schema"] != "oneshotsea.search-sea-level.v1":
            raise AuditError("{} has the wrong schema".format(label))
        _string_uint(value["index"], label + " index")
    for name in ("pass", "ell", "exact_modulus", "constraint_modulus",
                 "exact_trace_candidate_count", "trace_candidate_count",
                 "atkin_residue_count", "compatible_source_lifts",
                 "modular_root_workers", "modular_root_orbits",
                 "modular_root_reused_lifts", "source_lifts_us",
                 "modular_roots_us", "normalized_codomain_us", "bmss_us",
                 "eigenvalue_us", "eigenvalue_attempts",
                 "independent_eigenvalue_recoveries",
                 "conjugate_eigenvalues_derived"):
        _string_uint(value[name], "{} {}".format(label, name))
    _boolean(value["exact"], label + " exact")
    _boolean(value["modular_root_orbit_reuse"], label + " orbit reuse")
    _boolean(value["conjugate_eigenvalue_reuse"], label + " eigenvalue reuse")
    if "trace_residue" in value:
        _signed_integer(value["trace_residue"], label + " trace residue")
    order = value["atkin_projective_order"]
    if order is not None:
        _string_uint(order, label + " Atkin projective order", minimum=1)


def _validate_schoof_level(value: Any, label: str) -> None:
    expected = {
        "pass", "ell", "trace_residue", "exact_modulus",
        "constraint_modulus", "exact_trace_candidate_count",
        "trace_candidate_count", "elapsed_us",
    }
    if not isinstance(value, dict) or set(value) != expected:
        raise AuditError("{} has an unexpected field set".format(label))
    for name in expected - {"trace_residue"}:
        _string_uint(value[name], "{} {}".format(label, name))
    _signed_integer(value["trace_residue"], label + " trace residue")


def _validate_curve_record(
    record: Dict[str, Any], label: str, telemetry_enabled: bool,
    heuristic_enabled: bool,
) -> Dict[str, int]:
    required = {
        "schema", "index", "status", "peak_rss_bytes", "heuristic",
        "outcome_class", "sound_early_abort", "full_point_count",
        "reached_smoothness", "generator_rejections", "trace_prior",
        "sea_passes", "sea_levels", "exact_sea_levels", "atkin_sea_levels",
        "schoof_fallback_level_count", "initial_trace_count",
        "candidate_attempts", "candidate_search_nodes", "assembly_calls",
        "canonical_rejections", "timings_us", "sea_level_timings",
        "schoof_fallback_levels", "state",
    }
    optional = {"final_exact_trace_candidates", "final_trace_candidates", "trace", "certificate"}
    if not required.issubset(record) or not set(record).issubset(required | optional):
        raise AuditError("{} has an unexpected field set".format(label))
    if record["schema"] != "oneshotsea.search-curve.v1":
        raise AuditError("{} has the wrong schema".format(label))
    _string_uint(record["index"], label + " index")
    status = record.get("status")
    semantics = STATUS_SEMANTICS.get(status)
    if semantics is None:
        raise AuditError("{} has unknown status {!r}".format(label, status))
    heuristic = _boolean(record["heuristic"], label + " heuristic")
    sound = _boolean(record["sound_early_abort"], label + " sound early abort")
    full = _boolean(record["full_point_count"], label + " full point count")
    smooth = _boolean(record["reached_smoothness"], label + " reached smoothness")
    if heuristic != semantics["heuristic"] or sound != semantics["sound"] or (
        record["outcome_class"] != semantics["outcome_class"]
    ):
        raise AuditError("{} status flags/outcome class are inconsistent".format(label))
    if heuristic and not heuristic_enabled:
        raise AuditError("{} contains a heuristic skip while disabled".format(label))
    if semantics["full"] is not None and full != semantics["full"]:
        raise AuditError("{} full-point-count state contradicts status".format(label))
    if semantics["smooth"] is not None and smooth != semantics["smooth"]:
        raise AuditError("{} smoothness state contradicts status".format(label))
    if full != ("trace" in record):
        raise AuditError("{} trace presence differs from full point-count state".format(label))
    if "trace" in record:
        _signed_integer(record["trace"], label + " trace")
    final_pair = (
        "final_exact_trace_candidates" in record,
        "final_trace_candidates" in record,
    )
    if final_pair[0] != final_pair[1]:
        raise AuditError("{} has only one final trace-candidate field".format(label))
    if final_pair[0]:
        _string_uint(record["final_exact_trace_candidates"], label + " exact candidates")
        _string_uint(record["final_trace_candidates"], label + " candidates")
    metrics = {}  # type: Dict[str, int]
    for name in ("peak_rss_bytes", "generator_rejections", "sea_passes",
                 "sea_levels", "exact_sea_levels", "atkin_sea_levels",
                 "schoof_fallback_level_count", "initial_trace_count",
                 "candidate_attempts", "candidate_search_nodes",
                 "assembly_calls", "canonical_rejections"):
        metrics[name] = _string_uint(
            record[name], "{} {}".format(label, name),
            minimum=1 if name == "peak_rss_bytes" else 0)
    if metrics["exact_sea_levels"] > metrics["sea_levels"] or (
        metrics["atkin_sea_levels"] > metrics["sea_levels"]
    ):
        raise AuditError("{} SEA level counters are inconsistent".format(label))
    if metrics["canonical_rejections"] > metrics["assembly_calls"]:
        raise AuditError("{} canonical rejections exceed assembly calls".format(label))
    prior = record["trace_prior"]
    if prior is not None:
        if not isinstance(prior, dict) or set(prior) != {"modulus", "residue"}:
            raise AuditError("{} trace prior is malformed".format(label))
        _string_uint(prior["modulus"], label + " trace-prior modulus", minimum=1)
        _signed_integer(prior["residue"], label + " trace-prior residue")
    timings = record["timings_us"]
    timing_names = {"generation", "sea", "smoothness", "candidate", "assembly", "verifier", "total"}
    if not isinstance(timings, dict) or set(timings) != timing_names:
        raise AuditError("{} timings have an unexpected field set".format(label))
    parsed_timings = {
        name: _string_uint(timings[name], "{} {} timing".format(label, name))
        for name in timing_names
    }
    if parsed_timings["total"] < sum(
        parsed_timings[name] for name in timing_names - {"total"}
    ):
        raise AuditError("{} total timing is below component timings".format(label))
    levels = record["sea_level_timings"]
    if not isinstance(levels, list):
        raise AuditError("{} SEA level timings are not an array".format(label))
    if not telemetry_enabled and levels:
        raise AuditError("{} retains SEA telemetry while it is disabled".format(label))
    if telemetry_enabled and len(levels) != metrics["sea_levels"]:
        raise AuditError("{} SEA telemetry count differs from sea_levels".format(label))
    for number, level in enumerate(levels, 1):
        _validate_sea_level(level, "{} SEA level {}".format(label, number))
    schoof = record["schoof_fallback_levels"]
    if not isinstance(schoof, list) or len(schoof) != metrics["schoof_fallback_level_count"]:
        raise AuditError("{} Schoof telemetry count differs".format(label))
    for number, level in enumerate(schoof, 1):
        _validate_schoof_level(level, "{} Schoof level {}".format(label, number))
    if status == "verified_certificate":
        certificate = record.get("certificate")
        if not isinstance(certificate, dict) or set(certificate) != {
            "order_source", "odd_only", "montgomery_side", "line"
        }:
            raise AuditError("{} certificate data is malformed".format(label))
        if certificate["order_source"] not in ("curve", "twist") or (
            certificate["montgomery_side"] not in ("curve", "twist")
        ):
            raise AuditError("{} certificate side is invalid".format(label))
        _boolean(certificate["odd_only"], label + " certificate odd_only")
    elif "certificate" in record:
        raise AuditError("{} non-certificate status contains certificate data".format(label))
    metrics.update({"timing_" + name: value for name, value in parsed_timings.items()})
    return metrics


def _record_progress_legacy(
    records: List[Dict[str, Any]], label: str, assigned: Dict[str, int],
    identity: Dict[str, Any],
) -> Tuple[Dict[str, int], int, List[Dict[str, Any]]]:
    counters = {name: 0 for name in CHECKPOINT_COUNTERS}
    certificate_records = []  # type: List[Dict[str, Any]]
    expected_index = assigned["start"]
    for number, record in enumerate(records, 1):
        if record.get("schema") != "oneshotsea.search-curve.v1":
            raise AuditError("{} progress line {} has the wrong schema".format(label, number))
        index = _integer(record.get("index"), "{} progress line {} index".format(label, number))
        if index != expected_index:
            raise AuditError("{} progress is not contiguous at index {}".format(label, expected_index))
        if index >= assigned["end"]:
            raise AuditError("{} progress exceeds its assigned range".format(label))
        status = record.get("status")
        if status not in TERMINAL_COUNTER:
            raise AuditError("{} progress has unknown/nonadvancing status {!r}".format(label, status))
        heuristic = _boolean(record.get("heuristic"), label + " progress heuristic")
        if heuristic or TERMINAL_COUNTER[status] == "rejected_heuristic" or (
            record.get("outcome_class") == "heuristic_rejection"
        ):
            raise AuditError("{} progress contains a heuristic skip".format(label))
        full = _boolean(record.get("full_point_count"), label + " full_point_count")
        smooth = _boolean(record.get("reached_smoothness"), label + " reached_smoothness")
        terminal = TERMINAL_COUNTER[status]
        if terminal in ("rejected_certificate_assembly", "certificates_found") and not (full and smooth):
            raise AuditError("{} terminal certificate record lacks required work".format(label))
        if status == "sound_smoothness_reject" and record.get("sound_early_abort") is not True:
            raise AuditError("{} sound rejection lacks sound_early_abort".format(label))
        counters["curves_attempted"] += 1
        counters[terminal] += 1
        counters["full_point_counts_completed"] += int(full)
        counters["candidates_reaching_smoothness"] += int(smooth)
        if status == "verified_certificate":
            if not isinstance(record.get("certificate"), dict):
                raise AuditError("{} certificate record lacks certificate data".format(label))
            certificate_records.append(record)
        elif "certificate" in record:
            raise AuditError("{} non-certificate record contains certificate data".format(label))
        expected_index += 1
        if record.get("state") != _expected_state(
            identity, counters, expected_index, expected_index == assigned["end"]
        ):
            raise AuditError("{} progress state/counters drift at index {}".format(label, index))
    if len(certificate_records) > 1:
        raise AuditError("{} progress contains multiple certificates".format(label))
    if certificate_records and records[-1] is not certificate_records[0]:
        raise AuditError("{} continued after a verified certificate".format(label))
    return counters, expected_index, certificate_records


def _record_progress(
    records: List[Dict[str, Any]], label: str, assigned: Dict[str, int],
    identity: Dict[str, Any], telemetry_enabled: bool = False,
    heuristic_enabled: bool = False, include_details: bool = False,
) -> Any:
    if not include_details:
        return _record_progress_legacy(records, label, assigned, identity)
    counters = {name: 0 for name in CHECKPOINT_COUNTERS}
    certificate_records = []  # type: List[Dict[str, Any]]
    expected_index = assigned["start"]
    transitions = []  # type: List[Dict[str, Any]]
    aggregate_metrics = {
        "generator_rejections": 0, "sea_levels": 0, "exact_sea_levels": 0,
        "atkin_sea_levels": 0, "schoof_fallback_level_count": 0,
        "candidate_attempts": 0, "candidate_search_nodes": 0,
        "assembly_calls": 0, "canonical_rejections": 0,
        "timing_generation": 0, "timing_sea": 0, "timing_smoothness": 0,
        "timing_candidate": 0, "timing_assembly": 0, "timing_verifier": 0,
        "timing_total": 0,
    }
    peak_values = []  # type: List[int]
    for number, record in enumerate(records, 1):
        metrics = _validate_curve_record(
            record, "{} progress line {}".format(label, number),
            telemetry_enabled, heuristic_enabled)
        index = _integer(record.get("index"), "{} progress line {} index".format(label, number))
        if index != expected_index:
            raise AuditError("{} progress is not contiguous at index {}".format(label, expected_index))
        if index >= assigned["end"]:
            raise AuditError("{} progress exceeds its assigned range".format(label))
        status = record.get("status")
        semantics = STATUS_SEMANTICS[status]
        before_index = expected_index
        if semantics["advances"]:
            counters["curves_attempted"] += 1
            counters[semantics["counter"]] += 1
            counters["full_point_counts_completed"] += int(record["full_point_count"])
            counters["candidates_reaching_smoothness"] += int(record["reached_smoothness"])
            expected_index += 1
        if status == "verified_certificate":
            certificate_records.append(record)
        expected = _expected_state(
            identity, counters, expected_index, expected_index == assigned["end"]
        )
        if record.get("state") != expected:
            raise AuditError("{} progress state/counters drift at index {}".format(label, index))
        transitions.append({
            "before_index": before_index, "after_index": expected_index,
            "advances": semantics["advances"], "status": status,
            "state": expected,
        })
        for name in aggregate_metrics:
            aggregate_metrics[name] += metrics[name]
        peak_values.append(metrics["peak_rss_bytes"])
    if len(certificate_records) > 1:
        raise AuditError("{} progress contains multiple certificates".format(label))
    if certificate_records and records[-1] is not certificate_records[0]:
        raise AuditError("{} continued after a verified certificate".format(label))
    aggregate_metrics["minimum_curve_total_us"] = min(
        (metrics for metrics in (
            _string_uint(record["timings_us"]["total"], label + " total timing")
            for record in records
        )), default=0)
    aggregate_metrics["maximum_curve_total_us"] = max(
        (_string_uint(record["timings_us"]["total"], label + " total timing")
         for record in records), default=0)
    aggregate_metrics["maximum_reported_peak_rss_bytes"] = max(peak_values, default=0)
    return counters, expected_index, certificate_records, transitions, aggregate_metrics


def _certificate(
    directory: Path, label: str, manifest: Dict[str, Any],
    identity: Dict[str, Any], certificate_records: List[Dict[str, Any]],
    counters: Dict[str, int],
) -> Dict[str, Any]:
    certificate_path = directory / "certificate.txt"
    metadata_path = directory / "certificate.txt.meta.json"
    if not certificate_records:
        if certificate_path.exists() or metadata_path.exists():
            raise AuditError("{} has certificate artifacts without a certificate state".format(label))
        if counters["certificates_found"] != 0:
            raise AuditError("{} counters record a missing certificate".format(label))
        return {"found": False}
    if counters["certificates_found"] != 1:
        raise AuditError("{} certificate counter is not one".format(label))
    if not certificate_path.is_file() or certificate_path.is_symlink():
        raise AuditError("{} certificate file is missing or non-regular".format(label))
    if not metadata_path.is_file() or metadata_path.is_symlink():
        raise AuditError("{} certificate metadata is missing or non-regular".format(label))
    record = certificate_records[0]
    certificate = record["certificate"]
    line = certificate.get("line")
    if not isinstance(line, str) or not line or "\n" in line or "\r" in line:
        raise AuditError("{} certificate line is malformed".format(label))
    try:
        content = certificate_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise AuditError("cannot read {} certificate: {}".format(label, error))
    if content != line + "\n":
        raise AuditError("{} certificate file and progress line differ".format(label))
    metadata = _load_json(metadata_path, label + " certificate metadata")
    expected = {
        "schema": "oneshotsea.certificate-binding.v1",
        "prime": manifest["prime"],
        "seed": manifest["seed"],
        "worker_id": str(manifest["worker_id"]),
        "worker_count": str(manifest["worker_count"]),
        "range_start": identity["range_start"],
        "range_end": identity["range_end"],
        "schedule_sha256": identity["schedule_sha256"],
        "table_manifest_sha256": identity["table_manifest_sha256"],
        "build_id": manifest["build_id"],
        "global_index": record["index"],
        "certificate_sha256": _sha256_file(certificate_path),
        "certificate_line": line,
    }
    if metadata != expected:
        raise AuditError("{} certificate metadata does not bind its state/artifact".format(label))
    return {
        "found": True,
        "index": _integer(record["index"], label + " certificate index"),
        "sha256": expected["certificate_sha256"],
        "line": line,
        "metadata_sha256": _sha256_file(metadata_path),
    }


def _canonical_verifier_transcript(
    verifier: Optional[Path], root: Path, line: str, prime: str,
) -> Dict[str, Any]:
    if verifier is None:
        raise AuditError("a certificate requires --canonical-verifier")
    _outside_root(verifier, root, "canonical verifier")
    if not verifier.is_file() or verifier.is_symlink():
        raise AuditError("canonical verifier is missing or non-regular")
    digest = _sha256_file(verifier)
    if digest != PINNED_VERIFIER_SHA256:
        raise AuditError("canonical verifier does not have the pinned digest")
    tokens = line.split(" ")
    if len(tokens) < 4 or any(not token or not re.fullmatch(r"0|[1-9][0-9]*", token) for token in tokens):
        raise AuditError("certificate is not a canonical decimal tuple")
    if tokens[0] != prime:
        raise AuditError("certificate target differs from the audited prime")
    try:
        completed = subprocess.run(
            [sys.executable, str(verifier)] + tokens,
            check=False, capture_output=True, text=True, timeout=300,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise AuditError("cannot execute canonical verifier: {}".format(error))
    stdout = completed.stdout
    stderr = completed.stderr
    if completed.returncode != 0 or stdout != "True\n" or stderr:
        raise AuditError(
            "canonical verifier rejected certificate (status {}, stdout={!r}, stderr={!r})".format(
                completed.returncode, stdout, stderr))
    return {
        "verifier_sha256": digest,
        "exit_status": completed.returncode,
        "stdout_sha256": hashlib.sha256(stdout.encode("utf-8")).hexdigest(),
        "stderr_sha256": hashlib.sha256(stderr.encode("utf-8")).hexdigest(),
        "accepted": True,
    }


def _certificate_profile(
    directory: Path, root: Path, label: str, manifest: Dict[str, Any],
    identity: Dict[str, Any], certificate_records: List[Dict[str, Any]],
    counters: Dict[str, int], verifier: Optional[Path],
) -> Dict[str, Any]:
    certificate_path = directory / "certificate.txt"
    metadata_path = directory / "certificate.txt.meta.json"
    if not certificate_records:
        if certificate_path.exists() or metadata_path.exists():
            raise AuditError("{} has certificate artifacts without certificate state".format(label))
        if counters["certificates_found"] != 0:
            raise AuditError("{} counters record a missing certificate".format(label))
        return {"found": False}
    if len(certificate_records) != 1 or counters["certificates_found"] != 1:
        raise AuditError("{} has an inconsistent certificate count".format(label))
    if not certificate_path.is_file() or certificate_path.is_symlink() or (
        not metadata_path.is_file() or metadata_path.is_symlink()
    ):
        raise AuditError("{} certificate artifacts are missing or non-regular".format(label))
    record = certificate_records[0]
    line = record["certificate"]["line"]
    if not isinstance(line, str) or not line or "\n" in line or "\r" in line:
        raise AuditError("{} certificate line is malformed".format(label))
    try:
        content = certificate_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise AuditError("cannot read {} certificate: {}".format(label, error))
    if content != line + "\n":
        raise AuditError("{} certificate file and progress line differ".format(label))
    metadata = _load_json(metadata_path, label + " certificate metadata")
    expected = {
        "schema": "oneshotsea.certificate-binding.v1",
        "prime": manifest["prime"], "seed": manifest["seed"],
        "worker_id": str(manifest["worker_id"]),
        "worker_count": str(manifest["worker_count"]),
        "range_start": identity["range_start"], "range_end": identity["range_end"],
        "schedule_sha256": identity["schedule_sha256"],
        "table_manifest_sha256": identity["table_manifest_sha256"],
        "build_id": manifest["build_id"], "global_index": record["index"],
        "certificate_sha256": _sha256_file(certificate_path),
        "certificate_line": line,
    }
    if metadata != expected:
        raise AuditError("{} certificate metadata does not bind state/artifact".format(label))
    transcript = _canonical_verifier_transcript(
        verifier, root, line, manifest["prime"])
    return {
        "found": True,
        "index": _integer(record["index"], label + " certificate index"),
        "sha256": expected["certificate_sha256"], "line": line,
        "metadata_sha256": _sha256_file(metadata_path),
        "local_canonical_verifier": transcript,
    }


def _worker_log(
    path: Path, label: str, manifest: Dict[str, Any], assigned: Dict[str, int],
    options: Dict[str, str], progress: List[Dict[str, Any]],
    expected_final_state: Dict[str, Any], status: int, certificate_found: bool,
) -> Dict[str, Any]:
    values = _load_jsonl(path, label + " worker.log")
    starts = [value for value in values if value.get("schema") == "oneshotsea.search-start.v1"]
    curves = [value for value in values if value.get("schema") == "oneshotsea.search-curve.v1"]
    summaries = [value for value in values if value.get("schema") == "oneshotsea.search-summary.v1"]
    allowed = {
        "oneshotsea.search-start.v1",
        "oneshotsea.search-curve.v1",
        "oneshotsea.search-sea-level.v1",
        "oneshotsea.search-summary.v1",
    }
    if any(value.get("schema") not in allowed for value in values):
        raise AuditError("{} worker.log contains an unknown record schema".format(label))
    if len(starts) != 1 or values[0] is not starts[0]:
        raise AuditError("{} worker.log must begin with exactly one search-start".format(label))
    start = starts[0]
    expected_start = {
        "prime": manifest["prime"],
        "seed": manifest["seed"],
        "worker_id": str(manifest["worker_id"]),
        "worker_count": str(manifest["worker_count"]),
        "range_start": str(assigned["start"]),
        "range_end": str(assigned["end"]),
        "next_index": str(assigned["start"]),
        "build_id": manifest["build_id"],
        "smooth_cache_sha256": options["--smooth-cache-sha256"],
        "heuristic_rejection": False,
    }
    for name, wanted in expected_start.items():
        if start.get(name) != wanted:
            raise AuditError("{} worker.log search-start {} drift".format(label, name))
    for name in ("schedule_sha256", "table_manifest_sha256"):
        value = start.get(name)
        if not isinstance(value, str) or SHA256.fullmatch(value) is None:
            raise AuditError("{} worker.log has invalid {}".format(label, name))
    if curves != progress:
        raise AuditError("{} worker.log curve records differ from progress.jsonl".format(label))
    if status == 0:
        if len(summaries) != 1 or values[-1] is not summaries[0]:
            raise AuditError("{} successful worker lacks one terminal summary".format(label))
        summary = summaries[0]
        if _integer(summary.get("processed"), label + " summary processed") != len(progress):
            raise AuditError("{} summary processed count differs from progress".format(label))
        if _boolean(summary.get("range_exhausted"), label + " range_exhausted") != (
            int(expected_final_state["next_index"]) == assigned["end"]
        ):
            raise AuditError("{} summary range-exhausted state differs".format(label))
        if _boolean(summary.get("verified"), label + " summary verified") != certificate_found:
            raise AuditError("{} summary certificate state differs".format(label))
        if summary.get("state") != expected_final_state:
            raise AuditError("{} summary final state differs from checkpoint".format(label))
    elif summaries:
        raise AuditError("{} timed-out worker unexpectedly has a terminal summary".format(label))
    return {
        "record_count": len(values),
        "sea_level_record_count": sum(
            value.get("schema") == "oneshotsea.search-sea-level.v1" for value in values
        ),
        "has_terminal_summary": bool(summaries),
        "schedule_sha256": start["schedule_sha256"],
        "table_manifest_sha256": start["table_manifest_sha256"],
    }


def _start_resources(policy: Dict[str, str]) -> Dict[str, Any]:
    return {
        "smooth_threads": policy["--smooth-threads"],
        "smooth_max_batch": policy["--smooth-max-batch"],
        "smooth_root_auxiliary_bytes": policy["--smooth-root-auxiliary-bytes"],
        "smooth_build_segment_span": policy["--smooth-build-segment-span"],
        "curve_threads": policy["--curve-threads"],
        "smooth_coordinators": policy["--smooth-coordinators"],
        "x1_require_point_four": policy["--x1-require-point4"] == "1",
        "skip_incomplete_curves": policy["--skip-incomplete-curves"] == "1",
        "schoof_fallback": policy["--schoof-fallback"] == "1",
        "sea_level_telemetry": policy["--sea-level-telemetry"] == "1",
        "sea_threads": policy["--sea-threads"],
        "assembly_attempts": policy["--assembly-attempts"],
        "trace_cap": policy["--trace-cap"],
        "max_certificate_candidates": policy["--max-certificate-candidates"],
        "max_candidate_search_nodes": policy["--max-candidate-search-nodes"],
    }


def _expected_start_profile(
    profile: Dict[str, Any], manifest: Dict[str, Any], assigned: Dict[str, int],
    next_index: int,
) -> Dict[str, Any]:
    policy = profile["option_policy"]
    return {
        "schema": "oneshotsea.search-start.v1", "prime": profile["prime"],
        "seed": profile["seed"], "curve_family": policy["--curve-family"],
        "worker_id": str(manifest["worker_id"]),
        "worker_count": str(manifest["worker_count"]),
        "range_start": str(assigned["start"]), "range_end": str(assigned["end"]),
        "next_index": str(next_index),
        "schedule_sha256": profile["schedule_sha256"],
        "table_manifest_sha256": profile["table_manifest_sha256"],
        "smooth_cache_sha256": profile["smooth_cache_sha256"],
        "verifier_sha256": profile["verifier_sha256"],
        "python_executable": profile["python_executable"],
        "python_sha256": profile["python_sha256"],
        "build_id": manifest["build_id"],
        "heuristic_rejection": policy["--skip-incomplete-curves"] == "1",
        "resources": _start_resources(policy),
    }


def _validate_smooth_batch(value: Any, label: str) -> Dict[str, int]:
    def histogram_counts(raw: Any, histogram_label: str) -> Dict[int, int]:
        if not isinstance(raw, list):
            raise AuditError("{} is not an array".format(histogram_label))
        counts = {}  # type: Dict[int, int]
        previous_orders = -1
        for number, item in enumerate(raw, 1):
            if not isinstance(item, dict) or set(item) != {"orders", "scan_chunks"}:
                raise AuditError(
                    "{} item {} is malformed".format(histogram_label, number))
            orders = _string_uint(
                item["orders"], histogram_label + " orders", minimum=1)
            chunks = _string_uint(
                item["scan_chunks"], histogram_label + " chunks", minimum=1)
            if orders <= previous_orders:
                raise AuditError(
                    "{} is not strictly ordered".format(histogram_label))
            previous_orders = orders
            counts[orders] = chunks
        return counts

    required = {
        "enabled", "coordinator_count", "submitted_requests",
        "completed_requests", "failed_requests", "cancelled_requests",
        "coordinator_batches", "successful_cache_scan_chunks",
        "submitted_orders", "max_queued_requests_in_any_cohort",
        "max_requests_per_batch_in_any_cohort",
        "max_orders_per_successful_scan_chunk_in_any_cohort",
        "successful_scan_chunk_size_histogram", "cohorts",
    }
    if not isinstance(value, dict) or set(value) != required:
        raise AuditError("{} smooth_batch has an unexpected field set".format(label))
    enabled = _boolean(value["enabled"], label + " smooth batch enabled")
    names = required - {"enabled", "successful_scan_chunk_size_histogram", "cohorts"}
    parsed = {
        name: _string_uint(value[name], "{} smooth batch {}".format(label, name))
        for name in names
    }
    if enabled != (parsed["coordinator_count"] != 0):
        raise AuditError("{} smooth batch enabled/count differ".format(label))
    terminal = (
        parsed["completed_requests"] + parsed["failed_requests"] +
        parsed["cancelled_requests"])
    if terminal != parsed["submitted_requests"]:
        raise AuditError("{} smooth request counters do not balance".format(label))
    histogram = histogram_counts(
        value["successful_scan_chunk_size_histogram"], label + " smooth histogram")
    if sum(histogram.values()) != parsed["successful_cache_scan_chunks"]:
        raise AuditError("{} smooth histogram does not cover scan chunks".format(label))
    cohorts = value["cohorts"]
    if not isinstance(cohorts, list) or len(cohorts) != parsed["coordinator_count"]:
        raise AuditError("{} smooth cohort count differs".format(label))
    cohort_keys = {
        "index", "submitted_requests", "completed_requests", "failed_requests",
        "cancelled_requests", "coordinator_batches",
        "successful_cache_scan_chunks", "submitted_orders", "max_queued_requests",
        "max_requests_per_batch", "max_orders_per_successful_scan_chunk",
        "successful_scan_chunk_size_histogram",
    }
    parsed_cohorts = []  # type: List[Dict[str, int]]
    cohort_histogram = {}  # type: Dict[int, int]
    for number, cohort in enumerate(cohorts):
        if not isinstance(cohort, dict) or set(cohort) != cohort_keys:
            raise AuditError("{} smooth cohort {} is malformed".format(label, number))
        parsed_cohort = {
            name: _string_uint(cohort[name], "{} cohort {} {}".format(label, number, name))
            for name in cohort_keys - {"successful_scan_chunk_size_histogram"}
        }
        parsed_cohorts.append(parsed_cohort)
        if parsed_cohort["index"] != number:
            raise AuditError("{} smooth cohort index differs".format(label))
        if parsed_cohort["submitted_requests"] != (
            parsed_cohort["completed_requests"] + parsed_cohort["failed_requests"] +
            parsed_cohort["cancelled_requests"]
        ):
            raise AuditError("{} smooth cohort counters do not balance".format(label))
        one_histogram = histogram_counts(
            cohort["successful_scan_chunk_size_histogram"],
            "{} smooth cohort {} histogram".format(label, number))
        if sum(one_histogram.values()) != parsed_cohort["successful_cache_scan_chunks"]:
            raise AuditError("{} smooth cohort histogram does not cover scan chunks".format(label))
        for orders, chunks in one_histogram.items():
            cohort_histogram[orders] = cohort_histogram.get(orders, 0) + chunks
    additive = (
        "submitted_requests", "completed_requests", "failed_requests",
        "cancelled_requests", "coordinator_batches",
        "successful_cache_scan_chunks", "submitted_orders",
    )
    for name in additive:
        if sum(cohort[name] for cohort in parsed_cohorts) != parsed[name]:
            raise AuditError("{} smooth cohort {} total differs".format(label, name))
    maxima = {
        "max_queued_requests_in_any_cohort": "max_queued_requests",
        "max_requests_per_batch_in_any_cohort": "max_requests_per_batch",
        "max_orders_per_successful_scan_chunk_in_any_cohort":
            "max_orders_per_successful_scan_chunk",
    }
    for total_name, cohort_name in maxima.items():
        if max((cohort[cohort_name] for cohort in parsed_cohorts), default=0) != parsed[total_name]:
            raise AuditError("{} smooth cohort {} maximum differs".format(label, cohort_name))
    if cohort_histogram != histogram:
        raise AuditError("{} smooth cohort histograms differ from the total".format(label))
    return parsed


def _worker_log_profile(
    path: Path, label: str, profile: Dict[str, Any], manifest: Dict[str, Any],
    assigned: Dict[str, int], progress: List[Dict[str, Any]],
    transitions: List[Dict[str, Any]], attempts: List[Dict[str, Any]],
    expected_final_state: Dict[str, Any], certificate_found: bool,
) -> Dict[str, Any]:
    values = _load_jsonl(path, label + " worker.log")
    start_positions = [
        position for position, value in enumerate(values)
        if value.get("schema") == "oneshotsea.search-start.v1"
    ]
    if len(start_positions) != len(attempts) or not start_positions or start_positions[0] != 0:
        raise AuditError("{} worker.log attempt/start count differs".format(label))
    if len(attempts) > 1 and not profile["allow_resume"]:
        raise AuditError("{} contains a disallowed resume".format(label))
    allowed = {
        "oneshotsea.search-start.v1", "oneshotsea.search-curve.v1",
        "oneshotsea.search-sea-level.v1", "oneshotsea.search-summary.v1",
    }
    if any(value.get("schema") not in allowed for value in values):
        raise AuditError("{} worker.log contains an unknown record schema".format(label))
    flat_curves = [
        value for value in values
        if value.get("schema") == "oneshotsea.search-curve.v1"
    ]
    if flat_curves != progress:
        raise AuditError("{} worker.log curve records differ from progress.jsonl".format(label))
    progress_position = 0
    cursor = assigned["start"]
    sea_records = 0
    summaries = 0
    for attempt_number, start_position in enumerate(start_positions):
        stop = start_positions[attempt_number + 1] if attempt_number + 1 < len(start_positions) else len(values)
        segment = values[start_position:stop]
        start = segment[0]
        if start != _expected_start_profile(profile, manifest, assigned, cursor):
            raise AuditError("{} attempt {} search-start differs from profile/state".format(
                label, attempt_number + 1))
        segment_curves = [
            value for value in segment
            if value.get("schema") == "oneshotsea.search-curve.v1"
        ]
        segment_summaries = [
            value for value in segment
            if value.get("schema") == "oneshotsea.search-summary.v1"
        ]
        segment_levels = [
            value for value in segment
            if value.get("schema") == "oneshotsea.search-sea-level.v1"
        ]
        if profile["option_policy"]["--sea-level-telemetry"] == "0" and segment_levels:
            raise AuditError("{} has standalone SEA telemetry while disabled".format(label))
        for number, level in enumerate(segment_levels, 1):
            _validate_sea_level(
                level, "{} attempt {} SEA record {}".format(label, attempt_number + 1, number),
                standalone=True)
            index = _integer(level["index"], label + " SEA record index")
            if index < assigned["start"] or index >= assigned["end"]:
                raise AuditError("{} SEA telemetry index is outside assignment".format(label))
        sea_records += len(segment_levels)
        advancing = 0
        found_in_segment = False
        for offset, curve in enumerate(segment_curves):
            if progress_position >= len(transitions):
                raise AuditError("{} worker.log has excess curve records".format(label))
            transition = transitions[progress_position]
            if transition["before_index"] != cursor:
                raise AuditError("{} resumed curve cursor is discontinuous".format(label))
            if not transition["advances"] and offset != len(segment_curves) - 1:
                raise AuditError("{} nonadvancing outcome is not terminal in its attempt".format(label))
            cursor = transition["after_index"]
            advancing += int(transition["advances"])
            found_in_segment = found_in_segment or curve["status"] == "verified_certificate"
            progress_position += 1
        status = attempts[attempt_number]["status"]
        if status == 0:
            if len(segment_summaries) != 1 or segment[-1] is not segment_summaries[0]:
                raise AuditError("{} successful attempt lacks one final summary".format(label))
            summary = segment_summaries[0]
            summaries += 1
            required_summary = {
                "schema", "processed", "range_exhausted", "verified",
                "smooth_batch", "state",
            }
            if set(summary) != required_summary:
                raise AuditError("{} summary has an unexpected field set".format(label))
            if _string_uint(summary["processed"], label + " summary processed") != advancing:
                raise AuditError("{} summary processed count differs".format(label))
            if _boolean(summary["range_exhausted"], label + " summary exhausted") != (
                cursor == assigned["end"]
            ):
                raise AuditError("{} summary range state differs".format(label))
            if _boolean(summary["verified"], label + " summary verified") != found_in_segment:
                raise AuditError("{} summary certificate state differs".format(label))
            wanted_state = (
                transitions[progress_position - 1]["state"]
                if segment_curves else _expected_state(
                    _progress_identity(manifest, assigned, start),
                    {name: 0 for name in CHECKPOINT_COUNTERS}, cursor,
                    cursor == assigned["end"])
            )
            # Empty resumed attempts are not emitted by the launcher; avoiding
            # an invented prior-counter reconstruction keeps this fail closed.
            if not segment_curves:
                raise AuditError("{} successful attempt processed no curve records".format(label))
            if summary["state"] != wanted_state:
                raise AuditError("{} summary state differs from durable progress".format(label))
            _validate_smooth_batch(summary["smooth_batch"], label + " summary")
        elif status == 124:
            if segment_summaries:
                raise AuditError("{} timed-out attempt has a summary".format(label))
        else:
            raise AuditError("{} has unsupported attempt status".format(label))
    if progress_position != len(progress):
        raise AuditError("{} worker.log omits progress records".format(label))
    if transitions and transitions[-1]["state"] != expected_final_state:
        raise AuditError("{} final progress state differs from checkpoint".format(label))
    return {
        "record_count": len(values), "sea_level_record_count": sea_records,
        "has_terminal_summary": bool(summaries), "attempt_count": len(attempts),
        "schedule_sha256": profile["schedule_sha256"],
        "table_manifest_sha256": profile["table_manifest_sha256"],
    }


def _worker(
    root: Path, worker_id: int, worker_count: int,
    global_range: Dict[str, int], allow_timeout_124: bool,
) -> Dict[str, Any]:
    label = "worker-{}".format(worker_id)
    directory = root / label
    manifest_path = directory / "manifest.json"
    manifest = _load_json(manifest_path, label + " manifest")
    if manifest.get("schema") != WORKER_SCHEMA:
        raise AuditError("{} has an unexpected manifest schema".format(label))
    if manifest.get("worker_id") != worker_id or manifest.get("worker_count") != worker_count:
        raise AuditError("{} manifest has the wrong worker identity".format(label))
    manifest_global = _range(manifest.get("global_range"), label + " global_range")
    if manifest_global != global_range:
        raise AuditError("{} disagrees about the global range".format(label))
    assigned = _range(manifest.get("assigned_range"), label + " assigned_range")
    wanted_assignment = _partition(
        global_range["start"], global_range["end"], worker_id, worker_count
    )
    if assigned != wanted_assignment:
        raise AuditError("{} assigned range is not the deterministic partition".format(label))
    for name, pattern in (("deployment_commit", GIT_COMMIT), ("binary_sha256", SHA256)):
        value = manifest.get(name)
        if not isinstance(value, str) or pattern.fullmatch(value) is None:
            raise AuditError("{} manifest has invalid {}".format(label, name))
    build_id = "git:{}+binary-sha256:{}".format(
        manifest["deployment_commit"], manifest["binary_sha256"]
    )
    if manifest.get("build_id") != build_id:
        raise AuditError("{} manifest build_id is not its immutable identity".format(label))
    for name in ("prime", "seed", "run_id", "run_kind"):
        if not isinstance(manifest.get(name), str) or not manifest[name]:
            raise AuditError("{} manifest has invalid {}".format(label, name))
    started_utc = manifest.get("started_utc")
    if not isinstance(started_utc, str) or UTC.fullmatch(started_utc) is None:
        raise AuditError("{} manifest has invalid started_utc".format(label))
    wall_limit = _integer(manifest.get("wall_time_limit_seconds"), label + " wall limit")
    argv, options = _argv_options(manifest.get("command_argv"), label)
    expected_options = {
        "--p": manifest["prime"],
        "--seed": manifest["seed"],
        "--range-start": str(global_range["start"]),
        "--range-end": str(global_range["end"]),
        "--worker-id": str(worker_id),
        "--worker-count": str(worker_count),
        "--max-curves": str(assigned["count"]),
        "--build-id": build_id,
    }
    for name, wanted in expected_options.items():
        if options.get(name) != wanted:
            raise AuditError("{} command option {} differs from its manifest".format(label, name))
    cache_sha = options.get("--smooth-cache-sha256", "")
    if SHA256.fullmatch(cache_sha) is None:
        raise AuditError("{} command has no authenticated smooth cache".format(label))
    for name, basename in (
        ("--checkpoint", "checkpoint.json"),
        ("--progress", "progress.jsonl"),
        ("--certificate-out", "certificate.txt"),
    ):
        value = options.get(name)
        if not isinstance(value, str) or Path(value).name != basename or Path(value).parent.name != label:
            raise AuditError("{} command option {} has the wrong worker path".format(label, name))
    command_path = directory / "command.sh"
    command_sha = manifest.get("command_sha256")
    if not isinstance(command_sha, str) or SHA256.fullmatch(command_sha) is None:
        raise AuditError("{} manifest command SHA-256 is malformed".format(label))
    if _sha256_file(command_path) != command_sha:
        raise AuditError("{} manifest command SHA-256 does not match command.sh".format(label))
    launch = _expected_launch(argv, wall_limit)
    if _command_tokens(command_path, label) != launch:
        raise AuditError("{} command.sh argv differs from manifest command_argv".format(label))
    attempt = _attempt(directory / "attempts.jsonl", label, started_utc)
    if attempt["status"] not in (0, 124):
        raise AuditError("{} attempt has unexpected exit status {}".format(label, attempt["status"]))
    if attempt["status"] == 124 and not allow_timeout_124:
        raise AuditError("{} status 124 requires --allow-timeout-124".format(label))
    usage = _resource_usage(
        directory / "resource-usage.txt", label, launch,
        attempt["start_epoch"], attempt["end_epoch"], attempt["start_utc"],
        attempt["status"], wall_limit,
    )
    log_records = _load_jsonl(directory / "worker.log", label + " worker.log preflight")
    starts = [record for record in log_records if record.get("schema") == "oneshotsea.search-start.v1"]
    if len(starts) != 1:
        raise AuditError("{} worker.log does not have exactly one search-start".format(label))
    start = starts[0]
    identity = _progress_identity(manifest, assigned, start)
    progress_path = directory / "progress.jsonl"
    progress = _load_jsonl(progress_path, label + " progress")
    computed_counters, next_index, certificate_records = _record_progress(
        progress, label, assigned, identity
    )
    checkpoint, checkpoint_counters, checkpoint_sha = _checkpoint(
        directory / "checkpoint.json", label
    )
    checkpoint_identity = {
        "prime": manifest["prime"], "seed": manifest["seed"],
        "worker_id": str(worker_id), "worker_count": str(worker_count),
        "range_start": str(assigned["start"]), "range_end": str(assigned["end"]),
        "schedule_sha256": identity["schedule_sha256"],
        "table_manifest_sha256": identity["table_manifest_sha256"],
        "build_id": build_id,
    }
    for name, wanted in checkpoint_identity.items():
        if checkpoint.get(name) != wanted:
            raise AuditError("{} checkpoint identity field {} drift".format(label, name))
    if _integer(checkpoint.get("next_index"), label + " checkpoint next_index") != next_index:
        raise AuditError("{} checkpoint frontier differs from progress".format(label))
    if checkpoint_counters != computed_counters:
        raise AuditError("{} checkpoint counters differ from progress outcomes".format(label))
    certificate = _certificate(
        directory, label, manifest, identity, certificate_records, checkpoint_counters
    )
    final_state = _expected_state(
        identity, checkpoint_counters, next_index, next_index == assigned["end"]
    )
    log = _worker_log(
        directory / "worker.log", label, manifest, assigned, options, progress,
        final_state, attempt["status"], certificate["found"],
    )
    if attempt["status"] == 0 and not certificate["found"] and next_index != assigned["end"]:
        raise AuditError("{} exited zero without a certificate or exhausted range".format(label))
    return {
        "worker_id": worker_id,
        "assigned_range": assigned,
        "checkpoint_next_index": next_index,
        "completed_count": len(progress),
        "range_exhausted": next_index == assigned["end"],
        "deployment_commit": manifest["deployment_commit"],
        "binary_sha256": manifest["binary_sha256"],
        "smooth_cache_sha256": cache_sha,
        "run_id": manifest["run_id"],
        "run_kind": manifest["run_kind"],
        "prime": manifest["prime"],
        "seed": manifest["seed"],
        "build_id": build_id,
        "command_sha256": command_sha,
        "normalized_command_sha256": _canonical_sha256(
            [Path(argv[0]).name, argv[1]] + [
                item
                for position in range(2, len(argv), 2)
                for item in (
                    argv[position],
                    (
                        Path(argv[position + 1]).name
                        if argv[position] in (
                            "--checkpoint", "--progress", "--certificate-out"
                        )
                        else (
                            "<worker>"
                            if argv[position] in ("--worker-id", "--max-curves")
                            else argv[position + 1]
                        )
                    ),
                )
            ]
        ),
        "manifest_sha256": _sha256_file(manifest_path),
        "progress_sha256": _sha256_file(progress_path),
        "checkpoint_sha256": checkpoint_sha,
        "counters": checkpoint_counters,
        "certificate": certificate,
        "attempt": attempt,
        "resource_usage": usage,
        "worker_log": log,
        "schedule_sha256": log["schedule_sha256"],
        "table_manifest_sha256": log["table_manifest_sha256"],
    }


def _worker_profile(
    root: Path, worker_id: int, profile: Dict[str, Any],
    canonical_verifier: Optional[Path],
) -> Dict[str, Any]:
    worker_count = profile["worker_count"]
    global_range = profile["global_range"]
    label = "worker-{}".format(worker_id)
    directory = root / label
    if not directory.is_dir() or directory.is_symlink():
        raise AuditError("{} is not a real worker directory".format(label))
    actual_names = {path.name for path in directory.iterdir()}
    mandatory_names = {
        "manifest.json", "command.sh", "attempts.jsonl", "resource-usage.txt",
        "worker.log",
    }
    durable_state_names = {"progress.jsonl", "checkpoint.json"}
    certificate_names = {"certificate.txt", "certificate.txt.meta.json"}
    if actual_names not in (
        mandatory_names,
        mandatory_names | durable_state_names,
        mandatory_names | durable_state_names | certificate_names,
    ):
        raise AuditError("{} has an unexpected file layout".format(label))
    if any(not path.is_file() or path.is_symlink() for path in directory.iterdir()):
        raise AuditError("{} contains a non-regular entry".format(label))
    manifest_path = directory / "manifest.json"
    manifest = _load_json(manifest_path, label + " manifest")
    manifest_fields = {
        "schema", "run_id", "run_kind", "worker_id", "worker_count",
        "global_range", "assigned_range", "seed", "prime",
        "deployment_commit", "binary_sha256", "build_id",
        "wall_time_limit_seconds", "command_sha256", "command_argv",
        "started_utc",
    }
    if set(manifest) != manifest_fields or manifest.get("schema") != WORKER_SCHEMA:
        raise AuditError("{} manifest has an unexpected schema/field set".format(label))
    expected_manifest_values = {
        "run_id": profile["run_id"], "run_kind": profile["run_kind"],
        "worker_id": worker_id, "worker_count": worker_count,
        "seed": profile["seed"], "prime": profile["prime"],
        "deployment_commit": profile["deployment_commit"],
        "binary_sha256": profile["binary_sha256"],
        "wall_time_limit_seconds": profile["wall_time_limit_seconds"],
    }
    for name, wanted in expected_manifest_values.items():
        if manifest.get(name) != wanted:
            raise AuditError("{} manifest {} differs from trusted profile".format(label, name))
    if _utc_epoch(manifest.get("started_utc"), label + " manifest started_utc") < 0:
        raise AuditError("{} manifest start predates the Unix epoch".format(label))
    if not isinstance(manifest.get("global_range"), dict) or set(manifest["global_range"]) != {
        "start", "end", "count"
    }:
        raise AuditError("{} manifest global range has extra fields".format(label))
    manifest_global = _range(manifest["global_range"], label + " global_range")
    if manifest_global != global_range:
        raise AuditError("{} manifest global range differs from trusted profile".format(label))
    assigned = _partition(
        global_range["start"], global_range["end"], worker_id, worker_count)
    if not isinstance(manifest.get("assigned_range"), dict) or set(manifest["assigned_range"]) != {
        "start", "end", "count"
    } or _range(manifest["assigned_range"], label + " assigned_range") != assigned:
        raise AuditError("{} assigned range is not the deterministic partition".format(label))
    build_id = "git:{}+binary-sha256:{}".format(
        profile["deployment_commit"], profile["binary_sha256"])
    if manifest.get("build_id") != build_id:
        raise AuditError("{} manifest build_id differs from trusted identity".format(label))
    argv, options = _argv_options(manifest.get("command_argv"), label)
    if argv[0] != profile["executable_path"]:
        raise AuditError("{} command executable differs from trusted profile".format(label))
    expected_option_names = DERIVED_OPTIONS | set(profile["option_policy"])
    if set(options) != expected_option_names:
        raise AuditError("{} command option set differs from trusted policy".format(label))
    remote_root = PurePosixPath(profile["remote_root"])
    remote_worker = remote_root / "runs" / profile["run_id"] / label
    derived = {
        "--p": profile["prime"], "--seed": profile["seed"],
        "--range-start": str(global_range["start"]),
        "--range-end": str(global_range["end"]), "--worker-id": str(worker_id),
        "--worker-count": str(worker_count),
        "--smooth-cache-sha256": profile["smooth_cache_sha256"],
        "--checkpoint": str(remote_worker / "checkpoint.json"),
        "--progress": str(remote_worker / "progress.jsonl"),
        "--certificate-out": str(remote_worker / "certificate.txt"),
        "--build-id": build_id, "--max-curves": str(assigned["count"]),
    }
    expected_options = dict(profile["option_policy"])
    expected_options.update(derived)
    for name, wanted in expected_options.items():
        if options.get(name) != wanted:
            raise AuditError("{} command option {} differs from trusted policy".format(label, name))
    command_path = directory / "command.sh"
    command_sha = manifest.get("command_sha256")
    if not isinstance(command_sha, str) or SHA256.fullmatch(command_sha) is None or (
        _sha256_file(command_path) != command_sha
    ):
        raise AuditError("{} command SHA-256 binding is invalid".format(label))
    launch = _expected_launch(argv, profile["wall_time_limit_seconds"])
    expected_script = _canonical_command_script(
        profile["working_directory"], str(remote_worker), launch)
    if _command_tokens(command_path, label, expected_script) != launch:
        raise AuditError("{} canonical wrapper argv differs".format(label))
    attempts = _attempts(directory / "attempts.jsonl", label, manifest["started_utc"])
    expected_statuses = profile["expected_attempt_statuses"][worker_id]
    if [attempt["status"] for attempt in attempts] != expected_statuses:
        raise AuditError("{} attempt statuses differ from trusted profile".format(label))
    if len(attempts) > 1 and not profile["allow_resume"]:
        raise AuditError("{} contains a disallowed resume".format(label))
    if any(attempt["status"] == 124 for attempt in attempts) and not profile["allow_timeout_124"]:
        raise AuditError("{} contains a disallowed timeout".format(label))
    usage_blocks = _resource_usage(
        directory / "resource-usage.txt", label, launch, attempts,
        profile["wall_time_limit_seconds"])
    identity = {
        "schema": "oneshotsea.search-progress.v1", "prime": profile["prime"],
        "seed": profile["seed"], "worker_id": str(worker_id),
        "worker_count": str(worker_count), "range_start": str(assigned["start"]),
        "range_end": str(assigned["end"]),
        "schedule_sha256": profile["schedule_sha256"],
        "table_manifest_sha256": profile["table_manifest_sha256"],
        "build_id": build_id,
    }
    progress_path = directory / "progress.jsonl"
    checkpoint_path = directory / "checkpoint.json"
    progress_exists = progress_path.exists()
    checkpoint_exists = checkpoint_path.exists()
    if progress_exists != checkpoint_exists:
        raise AuditError(
            "{} has only one of progress.jsonl and checkpoint.json".format(label))
    empty_timed_out_attempt = not progress_exists
    if empty_timed_out_attempt and not all(
        attempt["status"] == 124 for attempt in attempts
    ):
        raise AuditError(
            "{} lacks durable search state outside an all-timeout attempt".format(label))
    progress = (
        [] if empty_timed_out_attempt
        else _load_jsonl(progress_path, label + " progress")
    )
    (computed_counters, next_index, certificate_records, transitions,
     metrics) = _record_progress(
        progress, label, assigned, identity,
        telemetry_enabled=profile["option_policy"]["--sea-level-telemetry"] == "1",
        heuristic_enabled=profile["option_policy"]["--skip-incomplete-curves"] == "1",
        include_details=True)
    if empty_timed_out_attempt:
        checkpoint = dict(identity)
        checkpoint["next_index"] = str(assigned["start"])
        checkpoint_counters = dict(computed_counters)
        checkpoint_sha = None
    else:
        checkpoint, checkpoint_counters, checkpoint_sha = _checkpoint(
            checkpoint_path, label)
    checkpoint_identity = {
        "prime": profile["prime"], "seed": profile["seed"],
        "worker_id": str(worker_id), "worker_count": str(worker_count),
        "range_start": str(assigned["start"]), "range_end": str(assigned["end"]),
        "schedule_sha256": profile["schedule_sha256"],
        "table_manifest_sha256": profile["table_manifest_sha256"],
        "build_id": build_id,
    }
    for name, wanted in checkpoint_identity.items():
        if checkpoint.get(name) != wanted:
            raise AuditError("{} checkpoint {} differs from trusted profile".format(label, name))
    if _integer(checkpoint.get("next_index"), label + " checkpoint next_index") != next_index:
        raise AuditError("{} checkpoint frontier differs from progress".format(label))
    if checkpoint_counters != computed_counters:
        raise AuditError("{} checkpoint counters differ from progress".format(label))
    maximum_resource_rss = max(item["peak_rss_bytes"] for item in usage_blocks)
    if metrics["maximum_reported_peak_rss_bytes"] > maximum_resource_rss:
        raise AuditError("{} progress peak RSS exceeds GNU-time peak".format(label))
    certificate = _certificate_profile(
        directory, root, label, manifest, identity, certificate_records,
        checkpoint_counters, canonical_verifier)
    final_state = _expected_state(
        identity, checkpoint_counters, next_index, next_index == assigned["end"])
    log = _worker_log_profile(
        directory / "worker.log", label, profile, manifest, assigned, progress,
        transitions, attempts, final_state, certificate["found"])
    return {
        "worker_id": worker_id, "assigned_range": assigned,
        "checkpoint_next_index": next_index,
        "completed_count": checkpoint_counters["curves_attempted"],
        "diagnostic_record_count": len(progress) - checkpoint_counters["curves_attempted"],
        "range_exhausted": next_index == assigned["end"],
        "deployment_commit": profile["deployment_commit"],
        "binary_sha256": profile["binary_sha256"],
        "smooth_cache_sha256": profile["smooth_cache_sha256"],
        "run_id": profile["run_id"], "run_kind": profile["run_kind"],
        "prime": profile["prime"], "seed": profile["seed"], "build_id": build_id,
        "command_sha256": command_sha,
        "normalized_command_sha256": _canonical_sha256(argv),
        "manifest_sha256": _sha256_file(manifest_path),
        "progress_sha256": (
            None if empty_timed_out_attempt else _sha256_file(progress_path)),
        "checkpoint_sha256": checkpoint_sha,
        "resource_usage_sha256": _sha256_file(directory / "resource-usage.txt"),
        "attempts_sha256": _sha256_file(directory / "attempts.jsonl"),
        "worker_log_sha256": _sha256_file(directory / "worker.log"),
        "counters": checkpoint_counters, "certificate": certificate,
        "attempts": attempts, "resource_usage_attempts": usage_blocks,
        "resource_usage": {
            "elapsed_seconds": sum(item["elapsed_seconds"] for item in usage_blocks),
            "maximum_resident_set_kib": maximum_resource_rss // 1024,
            "peak_rss_bytes": maximum_resource_rss,
            "swaps": sum(item["swaps"] for item in usage_blocks),
            "exit_status": usage_blocks[-1]["exit_status"],
        },
        "worker_log": log, "schedule_sha256": profile["schedule_sha256"],
        "table_manifest_sha256": profile["table_manifest_sha256"],
        "metrics": metrics,
    }


def _same(workers: List[Dict[str, Any]], field: str) -> Any:
    values = [worker[field] for worker in workers]
    if any(value != values[0] for value in values[1:]):
        raise AuditError("workers disagree about {}".format(field))
    return values[0]


def _require_expected(actual: Any, expected: Any, label: str) -> None:
    if expected is not None and actual != expected:
        raise AuditError("expected {} {}, found {}".format(label, expected, actual))


def _finite_tree(value: Any, label: str) -> None:
    if isinstance(value, float) and not math.isfinite(value):
        raise AuditError("{} contains a non-finite metric".format(label))
    if isinstance(value, str) and value.lower() in {
        "nan", "+nan", "-nan", "inf", "+inf", "-inf", "infinity",
        "+infinity", "-infinity",
    }:
        raise AuditError("{} contains a non-finite metric string".format(label))
    if isinstance(value, dict):
        for name, item in value.items():
            _finite_tree(item, "{}.{}".format(label, name))
    elif isinstance(value, list):
        for number, item in enumerate(value):
            _finite_tree(item, "{}[{}]".format(label, number))


def _validate_fetch_metadata(
    root: Path, profile: Dict[str, Any], workers: List[Dict[str, Any]],
) -> Optional[Dict[str, Any]]:
    path = root / "fetch-metadata.json"
    if not path.exists():
        return None
    value = _load_json(path, "fetch metadata")
    expected_fields = {
        "schema", "fetched_at", "run_id", "pod_id", "remote_source",
        "pod_state", "estimated_current_session_compute_cost_usd",
        "estimate_note",
    }
    if set(value) != expected_fields or value.get("schema") != 1:
        raise AuditError("fetch metadata has an unexpected schema/field set")
    if value.get("run_id") != profile["run_id"] or value.get("pod_id") != root.name:
        raise AuditError("fetch metadata run/pod identity differs")
    expected_source = str(
        PurePosixPath(profile["remote_root"]) / "runs" / profile["run_id"])
    if value.get("remote_source") != expected_source:
        raise AuditError("fetch metadata remote source differs from trusted run path")
    fetched_epoch = _utc_epoch(value.get("fetched_at"), "fetch timestamp")
    final_epoch = max(worker["attempts"][-1]["end_epoch"] for worker in workers)
    if fetched_epoch < final_epoch:
        raise AuditError("fetch timestamp predates the completed attempt")
    cost = value["estimated_current_session_compute_cost_usd"]
    if cost is not None:
        _finite_number(cost, "fetch cost")
    if not isinstance(value["estimate_note"], str) or not value["estimate_note"]:
        raise AuditError("fetch estimate note is missing")
    _finite_tree(value["pod_state"], "fetch pod state")
    return {"sha256": _sha256_file(path), "pod_id": value["pod_id"],
            "fetched_at": value["fetched_at"]}


def _aggregate_metrics(workers: List[Dict[str, Any]]) -> Dict[str, int]:
    names = set(workers[0]["metrics"])
    aggregate = {}  # type: Dict[str, int]
    for name in names:
        if name == "minimum_curve_total_us":
            positive = [worker["metrics"][name] for worker in workers
                        if worker["metrics"][name] != 0]
            aggregate[name] = min(positive, default=0)
        elif name in ("maximum_curve_total_us", "maximum_reported_peak_rss_bytes"):
            aggregate[name] = max(worker["metrics"][name] for worker in workers)
        else:
            aggregate[name] = sum(worker["metrics"][name] for worker in workers)
    return aggregate


def _validate_retained_result(
    root: Path, profile: Dict[str, Any], workers: List[Dict[str, Any]],
    aggregate_counters: Dict[str, int], metrics: Dict[str, int],
) -> Optional[Dict[str, Any]]:
    path = root / "result.json"
    if not path.exists():
        return None
    value = _load_json(path, "retained result")
    _finite_tree(value, "retained result")
    expected_top = {
        "schema", "classification", "run", "configuration", "outcome",
        "aggregate_curve_work", "host_resource_record", "throughput_and_cost",
        "local_audit",
    }
    if set(value) != expected_top:
        raise AuditError("retained result has an unexpected field set")
    expected_schema = (
        "oneshotsea.runpod-p125-probe.v1"
        if profile["run_kind"] == "benchmark"
        else "oneshotsea.runpod-p125-production.v1"
    )
    if value["schema"] != expected_schema:
        raise AuditError("retained result schema differs from run kind")
    run = value["run"]
    if not isinstance(run, dict):
        raise AuditError("retained result run is not an object")
    bindings = {
        "run_id": profile["run_id"], "run_kind": profile["run_kind"],
        "prime": profile["prime"], "seed": profile["seed"],
        "deployment_commit": profile["deployment_commit"],
        "binary_sha256": profile["binary_sha256"],
        "smooth_cache_sha256": profile["smooth_cache_sha256"],
    }
    for name, wanted in bindings.items():
        if run.get(name) != wanted:
            raise AuditError("retained result run {} differs".format(name))
    if len(workers) != 1:
        raise AuditError("legacy retained result schema cannot describe multiple workers")
    worker = workers[0]
    if run.get("pod_id") != root.name or run.get("command_sha256") != worker["command_sha256"]:
        raise AuditError("retained result pod/command identity differs")
    if run.get("start_utc") != worker["attempts"][0]["start_utc"] or (
        run.get("end_utc") != worker["attempts"][-1]["end_utc"]
    ):
        raise AuditError("retained result attempt timestamps differ")
    if _integer(run.get("exit_status"), "retained result exit status") != worker["attempts"][-1]["status"]:
        raise AuditError("retained result exit status differs")
    retained_range = run.get("range")
    if not isinstance(retained_range, dict):
        raise AuditError("retained result range is not an object")
    start = profile["global_range"]["start"]
    end = profile["global_range"]["end"]
    completed = aggregate_counters["curves_attempted"]
    if profile["run_kind"] == "benchmark":
        wanted_range = {
            "start_inclusive": str(start), "end_exclusive": str(end),
            "curves": completed,
        }
    else:
        wanted_range = {
            "start_inclusive": str(start), "configured_end_exclusive": str(end),
            "last_completed_inclusive": str(worker["checkpoint_next_index"] - 1),
            "curves_completed": completed,
        }
    if retained_range != wanted_range:
        raise AuditError("retained result range/cursor differs")
    configuration = value["configuration"]
    policy = profile["option_policy"]
    expected_configuration = {
        "curve_family": policy["--curve-family"],
        "x1_require_point_four": policy["--x1-require-point4"] == "1",
        "max_level": int(policy["--max-level"]),
        "trace_cap": int(policy["--trace-cap"]),
        "schoof_fallback": policy["--schoof-fallback"] == "1",
        "skip_incomplete_curves": policy["--skip-incomplete-curves"] == "1",
        "curve_threads": int(policy["--curve-threads"]),
        "sea_threads": int(policy["--sea-threads"]),
        "smooth_threads": int(policy["--smooth-threads"]),
        "smooth_coordinators": int(policy["--smooth-coordinators"]),
        "smooth_max_batch": int(policy["--smooth-max-batch"]),
        "wall_time_limit_seconds": profile["wall_time_limit_seconds"],
    }
    if configuration != expected_configuration:
        raise AuditError("retained result configuration differs from profile")
    outcome = value["outcome"]
    expected_outcome = {
        "curves_attempted": completed,
        "sound_smoothness_rejections": aggregate_counters["rejected_sound_early_abort"],
        "heuristic_rejections": aggregate_counters["rejected_heuristic"],
        "full_point_counts_completed": aggregate_counters["full_point_counts_completed"],
        "candidates_reaching_smoothness": aggregate_counters["candidates_reaching_smoothness"],
        "candidate_attempts": metrics["candidate_attempts"],
        "certificate_assembly_calls": metrics["assembly_calls"],
        "certificates_found": aggregate_counters["certificates_found"],
        "range_exhausted": worker["range_exhausted"],
        "checkpoint_next_index": str(worker["checkpoint_next_index"]),
    }
    for name, wanted in expected_outcome.items():
        if outcome.get(name) != wanted:
            raise AuditError("retained result outcome {} differs".format(name))
    if profile["run_kind"] == "production" and outcome.get("hard_wall_limit_reached") != (
        worker["attempts"][-1]["status"] == 124
    ):
        raise AuditError("retained result hard-wall state differs")
    aggregate_work = value["aggregate_curve_work"]
    expected_work = {
        "generator_rejections": metrics["generator_rejections"],
        "sea_levels": metrics["sea_levels"],
        "exact_sea_levels": metrics["exact_sea_levels"],
        "atkin_sea_levels": metrics["atkin_sea_levels"],
        "schoof_fallback_levels": metrics["schoof_fallback_level_count"],
        "timings_us": {
            "generation": metrics["timing_generation"],
            "sea": metrics["timing_sea"],
            "smoothness": metrics["timing_smoothness"],
            "total": metrics["timing_total"],
            "minimum_curve_total": metrics["minimum_curve_total_us"],
            "maximum_curve_total": metrics["maximum_curve_total_us"],
        },
    }
    if aggregate_work != expected_work:
        raise AuditError("retained result aggregate curve work differs")
    resource = worker["resource_usage_attempts"]
    host = value["host_resource_record"]
    elapsed = sum(item["elapsed_seconds"] for item in resource)
    epoch_elapsed = sum(
        attempt["end_epoch"] - attempt["start_epoch"] for attempt in worker["attempts"])
    host_expected = {
        "elapsed_seconds": elapsed,
        "attempt_epoch_seconds": epoch_elapsed,
        "user_seconds": sum(item["user_seconds"] for item in resource),
        "system_seconds": sum(item["system_seconds"] for item in resource),
        "average_cpu_percent": int(round(sum(
            item["average_cpu_percent"] * item["elapsed_seconds"] for item in resource
        ) / elapsed)),
        "maximum_resident_set_kbytes": max(item["maximum_resident_set_kib"] for item in resource),
        "maximum_resident_set_bytes": max(item["peak_rss_bytes"] for item in resource),
        "swaps": sum(item["swaps"] for item in resource),
    }
    for name, wanted in host_expected.items():
        actual = host.get(name)
        if isinstance(wanted, float):
            if abs(_finite_number(actual, "retained host " + name) - wanted) > 0.01:
                raise AuditError("retained result host {} differs".format(name))
        elif actual != wanted:
            raise AuditError("retained result host {} differs".format(name))
    local = value["local_audit"]
    for name, wanted in {
        "manifest_sha256": worker["manifest_sha256"],
        "checkpoint_sha256": worker["checkpoint_sha256"],
        "progress_sha256": worker["progress_sha256"],
        "resource_usage_sha256": worker["resource_usage_sha256"],
    }.items():
        if local.get(name) != wanted:
            raise AuditError("retained result local-audit {} differs".format(name))
    for name, item in local.items():
        if isinstance(item, bool) and not item:
            raise AuditError("retained result local-audit {} is false".format(name))
    return {"sha256": _sha256_file(path), "schema": value["schema"]}


def _audit_profile_root(
    root: Path, profile: Dict[str, Any], profile_sha256: str,
    canonical_verifier: Optional[Path],
) -> Dict[str, Any]:
    checksum_count, checksum_sha = _verify_checksums(root)
    expected_workers = {"worker-{}".format(i) for i in range(profile["worker_count"])}
    allowed_top = {"SHA256SUMS", "fetch-metadata.json", "result.json"} | expected_workers
    actual_top = {path.name for path in root.iterdir()}
    if not expected_workers.issubset(actual_top) or not actual_top.issubset(allowed_top):
        raise AuditError("run root has an unexpected or noncanonical layout")
    workers = [
        _worker_profile(root, worker_id, profile, canonical_verifier)
        for worker_id in range(profile["worker_count"])
    ]
    completed_indices = set()  # type: Set[int]
    aggregate = {name: 0 for name in CHECKPOINT_COUNTERS}
    for worker in workers:
        for index in range(
            worker["assigned_range"]["start"], worker["checkpoint_next_index"]
        ):
            if index in completed_indices:
                raise AuditError("duplicate completed global index {}".format(index))
            completed_indices.add(index)
        for name in CHECKPOINT_COUNTERS:
            aggregate[name] += worker["counters"][name]
    metrics = _aggregate_metrics(workers)
    usage = [worker["resource_usage"] for worker in workers]
    maximum_elapsed = max(item["elapsed_seconds"] for item in usage)
    throughput = len(completed_indices) * 3600.0 / maximum_elapsed
    resources = {
        "elapsed_seconds_sum": sum(item["elapsed_seconds"] for item in usage),
        "maximum_worker_elapsed_seconds": maximum_elapsed,
        "maximum_worker_rss_bytes": max(item["peak_rss_bytes"] for item in usage),
        "sum_worker_peak_rss_bytes": sum(item["peak_rss_bytes"] for item in usage),
        "swaps_sum": sum(item["swaps"] for item in usage),
        "exit_statuses": [item["exit_status"] for item in usage],
        "attempt_exit_statuses": [
            [attempt["status"] for attempt in worker["attempts"]]
            for worker in workers
        ],
        "aggregate_curves_per_hour": throughput,
    }
    certificates = [
        dict(worker["certificate"], worker_id=worker["worker_id"])
        for worker in workers if worker["certificate"]["found"]
    ]
    all_exhausted = all(worker["range_exhausted"] for worker in workers)
    fetch = _validate_fetch_metadata(root, profile, workers)
    retained_result = _validate_retained_result(
        root, profile, workers, aggregate, metrics)
    checks = {
        "checkpoint_next_indices": [worker["checkpoint_next_index"] for worker in workers]
            == profile["expected_checkpoint_next_indices"],
        "minimum_completed_count": len(completed_indices) >= profile["minimum_completed_count"],
        "expected_counters": aggregate == profile["expected_counters"],
        "minimum_aggregate_curves_per_hour": throughput >=
            profile["minimum_aggregate_curves_per_hour"],
        "maximum_worker_rss_bytes": resources["maximum_worker_rss_bytes"] <=
            profile["maximum_worker_rss_bytes"],
        "maximum_swaps_sum": resources["swaps_sum"] <= profile["maximum_swaps_sum"],
        "required_range_exhaustion": (
            not profile["require_all_assigned_ranges_exhausted"] or all_exhausted),
    }
    gate_accepted = all(checks.values())
    return {
        "schema": SCHEMA, "accepted": gate_accepted,
        "structural_integrity": {"accepted": True},
        "declared_outcome_gate": {"accepted": gate_accepted, "checks": checks},
        "profile_sha256": profile_sha256,
        "checksum_record_count": checksum_count,
        "sha256sums_sha256": checksum_sha,
        "identity": {
            "run_id": profile["run_id"], "run_kind": profile["run_kind"],
            "prime": profile["prime"], "seed": profile["seed"],
            "deployment_commit": profile["deployment_commit"],
            "binary_sha256": profile["binary_sha256"],
            "smooth_cache_sha256": profile["smooth_cache_sha256"],
            "schedule_sha256": profile["schedule_sha256"],
            "table_manifest_sha256": profile["table_manifest_sha256"],
            "verifier_sha256": profile["verifier_sha256"],
            "python_sha256": profile["python_sha256"],
            "global_range": profile["global_range"],
            "worker_count": profile["worker_count"],
        },
        "outcome": {
            "completed_global_index_count": len(completed_indices),
            "completed_intervals": [
                {"start": worker["assigned_range"]["start"],
                 "end": worker["checkpoint_next_index"]}
                for worker in workers
                if worker["checkpoint_next_index"] > worker["assigned_range"]["start"]
            ],
            "remaining_intervals": [
                {"start": worker["checkpoint_next_index"],
                 "end": worker["assigned_range"]["end"]}
                for worker in workers if not worker["range_exhausted"]
            ],
            "all_assigned_ranges_exhausted": all_exhausted,
            "expected_timeouts_accepted": sum(
                attempt["status"] == 124 for worker in workers
                for attempt in worker["attempts"]),
            "counters": aggregate, "certificates": certificates,
        },
        "resources": resources, "metrics": metrics,
        "fetch_metadata": fetch, "retained_result": retained_result,
        "workers": workers,
    }


def _audit_legacy(
    root: Path,
    expected_commit: Optional[str] = None,
    expected_binary_sha256: Optional[str] = None,
    expected_cache_sha256: Optional[str] = None,
    expected_global_start: Optional[int] = None,
    expected_global_end: Optional[int] = None,
    expected_worker_count: Optional[int] = None,
    allow_timeout_124: bool = False,
) -> Dict[str, Any]:
    """Authenticate and summarize one fetched RunPod search root."""
    checksum_count, checksum_sha = _verify_checksums(root)
    actual_workers = set()  # type: Set[int]
    for path in root.iterdir():
        match = WORKER_DIRECTORY.fullmatch(path.name)
        if match is not None:
            if not path.is_dir() or path.is_symlink():
                raise AuditError("{} is not a real worker directory".format(path.name))
            actual_workers.add(int(match.group(1)))
        elif path.name.startswith("worker-"):
            raise AuditError("unexpected worker directory name: {}".format(path.name))
    if not actual_workers:
        raise AuditError("run root has no worker directories")
    first_manifest = _load_json(root / "worker-0" / "manifest.json", "worker-0 manifest")
    manifest_count = _integer(first_manifest.get("worker_count"), "worker-0 worker_count", minimum=1)
    worker_count = manifest_count if expected_worker_count is None else expected_worker_count
    if worker_count < 1:
        raise AuditError("expected worker count must be positive")
    if manifest_count != worker_count or actual_workers != set(range(worker_count)):
        raise AuditError(
            "worker directories are {}, expected {}".format(
                sorted(actual_workers), list(range(worker_count))
            )
        )
    global_range = _range(first_manifest.get("global_range"), "worker-0 global_range")
    _require_expected(global_range["start"], expected_global_start, "global start")
    _require_expected(global_range["end"], expected_global_end, "global end")
    workers = [
        _worker(root, worker_id, worker_count, global_range, allow_timeout_124)
        for worker_id in range(worker_count)
    ]
    commit = _same(workers, "deployment_commit")
    binary = _same(workers, "binary_sha256")
    cache = _same(workers, "smooth_cache_sha256")
    run_id = _same(workers, "run_id")
    run_kind = _same(workers, "run_kind")
    prime = _same(workers, "prime")
    seed = _same(workers, "seed")
    _same(workers, "build_id")
    _same(workers, "normalized_command_sha256")
    _same(workers, "schedule_sha256")
    _same(workers, "table_manifest_sha256")
    if expected_commit is not None and GIT_COMMIT.fullmatch(expected_commit) is None:
        raise AuditError("expected commit is not 40 lowercase hexadecimal digits")
    if expected_binary_sha256 is not None and SHA256.fullmatch(expected_binary_sha256) is None:
        raise AuditError("expected binary SHA-256 is malformed")
    if expected_cache_sha256 is not None and SHA256.fullmatch(expected_cache_sha256) is None:
        raise AuditError("expected cache SHA-256 is malformed")
    _require_expected(commit, expected_commit, "deployment commit")
    _require_expected(binary, expected_binary_sha256, "binary SHA-256")
    _require_expected(cache, expected_cache_sha256, "smooth-cache SHA-256")
    completed_indices = set()  # type: Set[int]
    for worker in workers:
        start = worker["assigned_range"]["start"]
        for index in range(start, worker["checkpoint_next_index"]):
            if index in completed_indices:
                raise AuditError("duplicate completed global index {}".format(index))
            completed_indices.add(index)
    aggregate = {name: 0 for name in CHECKPOINT_COUNTERS}
    for worker in workers:
        for name in CHECKPOINT_COUNTERS:
            aggregate[name] += worker["counters"][name]
    resources = [worker["resource_usage"] for worker in workers]
    certificates = [
        dict(worker["certificate"], worker_id=worker["worker_id"])
        for worker in workers if worker["certificate"]["found"]
    ]
    return {
        "schema": SCHEMA,
        "accepted": True,
        "checksum_record_count": checksum_count,
        "sha256sums_sha256": checksum_sha,
        "identity": {
            "run_id": run_id,
            "run_kind": run_kind,
            "prime": prime,
            "seed": seed,
            "deployment_commit": commit,
            "binary_sha256": binary,
            "smooth_cache_sha256": cache,
            "global_range": global_range,
            "worker_count": worker_count,
        },
        "outcome": {
            "completed_global_index_count": len(completed_indices),
            "all_assigned_ranges_exhausted": all(worker["range_exhausted"] for worker in workers),
            "expected_timeouts_accepted": sum(worker["attempt"]["status"] == 124 for worker in workers),
            "counters": aggregate,
            "certificates": certificates,
        },
        "resources": {
            "elapsed_seconds_sum": sum(item["elapsed_seconds"] for item in resources),
            "maximum_worker_rss_bytes": max(item["peak_rss_bytes"] for item in resources),
            "sum_worker_peak_rss_bytes": sum(item["peak_rss_bytes"] for item in resources),
            "swaps_sum": sum(item["swaps"] for item in resources),
            "exit_statuses": [item["exit_status"] for item in resources],
        },
        "workers": workers,
    }


def audit(
    root: Path,
    expected_commit: Optional[str] = None,
    expected_binary_sha256: Optional[str] = None,
    expected_cache_sha256: Optional[str] = None,
    expected_global_start: Optional[int] = None,
    expected_global_end: Optional[int] = None,
    expected_worker_count: Optional[int] = None,
    allow_timeout_124: bool = False,
    profile: Optional[Path] = None,
    canonical_verifier: Optional[Path] = None,
) -> Dict[str, Any]:
    """Audit a run using an external profile, with a legacy in-process adapter.

    The legacy keyword path exists solely for the checked-in topology auditor,
    which independently pins the remaining workload semantics.  The command-line
    interface never exposes it.
    """
    if profile is not None:
        if any(item is not None for item in (
            expected_commit, expected_binary_sha256, expected_cache_sha256,
            expected_global_start, expected_global_end, expected_worker_count,
        )) or allow_timeout_124:
            raise AuditError("profile audit cannot be mixed with legacy expectations")
        profile_value, profile_sha = _profile(Path(profile), root)
        return _audit_profile_root(
            root, profile_value, profile_sha,
            Path(canonical_verifier) if canonical_verifier is not None else None)
    if all(item is None for item in (
        expected_commit, expected_binary_sha256, expected_cache_sha256,
        expected_global_start, expected_global_end, expected_worker_count,
    )):
        raise AuditError("an external audit profile is required")
    return _audit_legacy(
        root, expected_commit=expected_commit,
        expected_binary_sha256=expected_binary_sha256,
        expected_cache_sha256=expected_cache_sha256,
        expected_global_start=expected_global_start,
        expected_global_end=expected_global_end,
        expected_worker_count=expected_worker_count,
        allow_timeout_124=allow_timeout_124)


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
    parser.add_argument("root", type=Path, help="fetched run root containing SHA256SUMS")
    parser.add_argument(
        "--profile", required=True, type=Path,
        help="trusted audit profile outside the audited root",
    )
    parser.add_argument(
        "--canonical-verifier", type=Path,
        help="required when the run contains a certificate",
    )
    parser.add_argument("--output", type=Path, help="also write recomputed JSON atomically")
    parser.add_argument(
        "--result", type=Path,
        help="require exact equality with a previously retained audit JSON",
    )
    arguments = parser.parse_args(argv)
    try:
        result = audit(
            arguments.root, profile=arguments.profile,
            canonical_verifier=arguments.canonical_verifier,
        )
        if arguments.result is not None:
            retained = _load_json(arguments.result, "retained result")
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
