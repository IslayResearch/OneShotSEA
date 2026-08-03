#!/usr/bin/env python3
"""Audit cross-run search coverage and reject undeclared interval overlap."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
from typing import Any, Dict, List, Optional, Sequence, Tuple


ROOT = Path(__file__).resolve().parents[1]
LEDGER_SCHEMA = "oneshotsea.search-coverage-ledger.v1"
RESULT_SCHEMA = "oneshotsea.search-coverage-audit.v1"
SHA256 = re.compile(r"[0-9a-f]{64}")
IDENTITY_FIELDS = (
    "prime", "seed", "deployment_commit", "binary_sha256",
    "smooth_cache_sha256", "table_manifest_sha256",
)


class AuditError(ValueError):
    """Coverage evidence is malformed, unauthenticated, or inconsistent."""


def _reject_constant(value: str) -> Any:
    raise AuditError("non-finite JSON constant: {}".format(value))


def _object(pairs: Sequence[Tuple[str, Any]]) -> Dict[str, Any]:
    result: Dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise AuditError("duplicate JSON key: {!r}".format(key))
        result[key] = value
    return result


def _load_json(path: Path, label: str) -> Dict[str, Any]:
    if not path.is_file() or path.is_symlink():
        raise AuditError("{} is missing, non-regular, or a symlink".format(label))
    try:
        with path.open(encoding="utf-8") as stream:
            value = json.load(
                stream, object_pairs_hook=_object, parse_constant=_reject_constant,
            )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise AuditError("invalid {}: {}".format(label, error))
    if not isinstance(value, dict):
        raise AuditError("{} is not an object".format(label))
    return value


def _integer(value: Any, label: str) -> int:
    if isinstance(value, bool):
        raise AuditError("{} is not an integer".format(label))
    try:
        parsed = int(value)
    except (TypeError, ValueError):
        raise AuditError("{} is not an integer".format(label))
    if not isinstance(value, int) and str(parsed) != str(value):
        raise AuditError("{} is not a canonical integer".format(label))
    if parsed < 0:
        raise AuditError("{} is negative".format(label))
    return parsed


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _safe_source_path(raw: Any) -> Path:
    if not isinstance(raw, str) or not raw or raw.startswith("/"):
        raise AuditError("coverage source path is not a nonempty repository-relative path")
    relative = Path(raw)
    if ".." in relative.parts or relative.as_posix() != raw:
        raise AuditError("coverage source path is not canonical")
    path = ROOT / relative
    try:
        path.resolve().relative_to(ROOT.resolve())
    except ValueError:
        raise AuditError("coverage source escapes the repository")
    return path


def _interval(value: Any, label: str) -> Tuple[int, int]:
    if not isinstance(value, dict) or set(value) not in (
        {"start", "end"}, {"start", "end", "count"},
    ):
        raise AuditError("{} is not a canonical interval".format(label))
    start = _integer(value["start"], label + " start")
    end = _integer(value["end"], label + " end")
    if end <= start:
        raise AuditError("{} is empty or reversed".format(label))
    if "count" in value and _integer(value["count"], label + " count") != end - start:
        raise AuditError("{} count differs from its bounds".format(label))
    return start, end


def _merge(intervals: Sequence[Tuple[int, int]]) -> List[Tuple[int, int]]:
    merged: List[Tuple[int, int]] = []
    for start, end in sorted(intervals):
        if not merged or start > merged[-1][1]:
            merged.append((start, end))
        else:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
    return merged


def _assert_disjoint(intervals: Sequence[Tuple[int, int]], label: str) -> None:
    ordered = sorted(intervals)
    for previous, current in zip(ordered, ordered[1:]):
        if current[0] < previous[1]:
            raise AuditError("{} contains internal overlap".format(label))


def _identity(value: Dict[str, Any], label: str) -> Dict[str, str]:
    result: Dict[str, str] = {}
    for field in IDENTITY_FIELDS:
        item = value.get(field)
        if not isinstance(item, str) or not item:
            raise AuditError("{} has invalid identity field {}".format(label, field))
        result[field] = item
    for field in (
        "binary_sha256", "smooth_cache_sha256", "table_manifest_sha256",
    ):
        if SHA256.fullmatch(result[field]) is None:
            raise AuditError("{} identity field {} is not SHA-256".format(label, field))
    if re.fullmatch(r"[0-9a-f]{40}", result["deployment_commit"]) is None:
        raise AuditError("{} deployment commit is malformed".format(label))
    _integer(result["prime"], label + " prime")
    _integer(result["seed"], label + " seed")
    return result


def _schedule(value: Dict[str, Any], label: str) -> str:
    schedule = value.get("schedule_sha256")
    if not isinstance(schedule, str) or SHA256.fullmatch(schedule) is None:
        raise AuditError("{} has an invalid schedule SHA-256".format(label))
    return schedule


def _extract_topology(value: Dict[str, Any], label: str) -> Tuple[Dict[str, str], str, List[Tuple[int, int]], int]:
    if value.get("schema") != "oneshotsea.p125-topology-audit.v1" or value.get("accepted") is not True:
        raise AuditError("{} is not an accepted topology audit".format(label))
    gates = value.get("gates")
    if not isinstance(gates, dict) or not gates or not all(item is True for item in gates.values()):
        raise AuditError("{} topology gates are not all accepted".format(label))
    pairs = value.get("pairs")
    if not isinstance(pairs, dict) or not pairs:
        raise AuditError("{} topology audit has no range pairs".format(label))
    intervals: List[Tuple[int, int]] = []
    for name in sorted(pairs):
        pair = pairs[name]
        if not isinstance(pair, dict):
            raise AuditError("{} topology pair {} is malformed".format(label, name))
        pair_gates = pair.get("gates")
        if not isinstance(pair_gates, dict) or pair_gates.get("no_certificates_or_heuristics") is not True:
            raise AuditError("{} topology pair {} lacks a no-hit sound gate".format(label, name))
        intervals.append(_interval(pair.get("range"), "{} topology pair {}".format(label, name)))
    _assert_disjoint(intervals, label)
    raw_identity = value.get("immutable_identity", {})
    return _identity(raw_identity, label), _schedule(raw_identity, label), intervals, 0


def _extract_run_audit(value: Dict[str, Any], label: str) -> Tuple[Dict[str, str], str, List[Tuple[int, int]], int]:
    if value.get("schema") != "oneshotsea.runpod-search-audit.v2" or value.get("accepted") is not True:
        raise AuditError("{} is not an accepted RunPod search audit".format(label))
    if value.get("structural_integrity", {}).get("accepted") is not True or \
            value.get("declared_outcome_gate", {}).get("accepted") is not True:
        raise AuditError("{} RunPod audit gates are not accepted".format(label))
    outcome = value.get("outcome")
    if not isinstance(outcome, dict):
        raise AuditError("{} RunPod audit has no outcome".format(label))
    counters = outcome.get("counters")
    if not isinstance(counters, dict) or _integer(
        counters.get("rejected_heuristic"), label + " heuristic count"
    ) != 0:
        raise AuditError("{} contains heuristic coverage".format(label))
    raw_intervals = outcome.get("completed_intervals")
    if not isinstance(raw_intervals, list):
        raise AuditError("{} completed intervals are malformed".format(label))
    intervals = [_interval(item, "{} completed interval".format(label))
                 for item in raw_intervals]
    _assert_disjoint(intervals, label)
    certificates = outcome.get("certificates")
    if not isinstance(certificates, list):
        raise AuditError("{} certificate list is malformed".format(label))
    raw_identity = value.get("identity", {})
    return (
        _identity(raw_identity, label), _schedule(raw_identity, label),
        intervals, len(certificates),
    )


def _intersection_count(interval: Tuple[int, int], covered: Sequence[Tuple[int, int]]) -> int:
    return sum(max(0, min(interval[1], end) - max(interval[0], start))
               for start, end in covered)


def audit(ledger_path: Path) -> Dict[str, Any]:
    ledger = _load_json(ledger_path, "coverage ledger")
    required = {
        "schema", "identity", "contiguous_start", "expected_first_gap",
        "sources", "intentional_overlaps",
    }
    if set(ledger) != required or ledger.get("schema") != LEDGER_SCHEMA:
        raise AuditError("coverage ledger has an unexpected schema or field set")
    wanted_identity = _identity(ledger["identity"], "coverage ledger")
    contiguous_start = _integer(ledger["contiguous_start"], "contiguous start")
    expected_first_gap = _integer(ledger["expected_first_gap"], "expected first gap")
    raw_sources = ledger["sources"]
    if not isinstance(raw_sources, list) or not raw_sources:
        raise AuditError("coverage ledger has no sources")

    covered: List[Tuple[int, int]] = []
    extracted: List[Dict[str, Any]] = []
    labels = set()
    for number, source in enumerate(raw_sources, 1):
        if not isinstance(source, dict) or set(source) != {
            "label", "kind", "path", "sha256", "schedule_sha256",
        }:
            raise AuditError("coverage source {} has an unexpected field set".format(number))
        label = source["label"]
        if not isinstance(label, str) or not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", label) or label in labels:
            raise AuditError("coverage source label is malformed or duplicated")
        labels.add(label)
        expected_sha = source["sha256"]
        if not isinstance(expected_sha, str) or SHA256.fullmatch(expected_sha) is None:
            raise AuditError("coverage source {} SHA-256 is malformed".format(label))
        expected_schedule = source["schedule_sha256"]
        if not isinstance(expected_schedule, str) or SHA256.fullmatch(expected_schedule) is None:
            raise AuditError(
                "coverage source {} schedule SHA-256 is malformed".format(label))
        path = _safe_source_path(source["path"])
        actual_sha = _sha256(path)
        if actual_sha != expected_sha:
            raise AuditError("coverage source {} SHA-256 mismatch".format(label))
        value = _load_json(path, "coverage source {}".format(label))
        kind = source["kind"]
        if kind == "topology_audit":
            identity, schedule, intervals, certificate_count = _extract_topology(value, label)
        elif kind == "runpod_search_audit":
            identity, schedule, intervals, certificate_count = _extract_run_audit(value, label)
        else:
            raise AuditError("coverage source {} has unsupported kind".format(label))
        if identity != wanted_identity:
            raise AuditError("coverage source {} has a different search identity".format(label))
        if schedule != expected_schedule:
            raise AuditError(
                "coverage source {} has a different declared schedule".format(label))
        assigned_count = sum(end - start for start, end in intervals)
        duplicate_count = sum(_intersection_count(interval, covered) for interval in intervals)
        if duplicate_count > assigned_count:
            raise AuditError("coverage source {} duplicate accounting overflow".format(label))
        covered = _merge(covered + intervals)
        extracted.append({
            "label": label, "kind": kind, "path": source["path"],
            "sha256": actual_sha, "schedule_sha256": schedule,
            "intervals": [{"start": start, "end": end} for start, end in intervals],
            "assigned_count": assigned_count,
            "fresh_count": assigned_count - duplicate_count,
            "duplicate_count": duplicate_count,
            "certificate_count": certificate_count,
            "_raw_intervals": intervals,
        })

    actual_overlaps: List[Tuple[str, str, int, int]] = []
    for left_index, left in enumerate(extracted):
        for right in extracted[left_index + 1:]:
            intersections = []
            for left_interval in left["_raw_intervals"]:
                for right_interval in right["_raw_intervals"]:
                    start = max(left_interval[0], right_interval[0])
                    end = min(left_interval[1], right_interval[1])
                    if start < end:
                        intersections.append((start, end))
            for start, end in _merge(intersections):
                first, second = sorted((left["label"], right["label"]))
                actual_overlaps.append((first, second, start, end))
    actual_overlaps.sort()

    declared_overlaps: List[Tuple[str, str, int, int]] = []
    overlap_output: List[Dict[str, Any]] = []
    raw_overlaps = ledger["intentional_overlaps"]
    if not isinstance(raw_overlaps, list):
        raise AuditError("intentional overlaps are not an array")
    for item in raw_overlaps:
        if not isinstance(item, dict) or set(item) != {"sources", "start", "end", "reason"}:
            raise AuditError("intentional overlap has an unexpected field set")
        pair = item["sources"]
        if not isinstance(pair, list) or len(pair) != 2 or pair != sorted(pair) or \
                pair[0] == pair[1] or any(name not in labels for name in pair):
            raise AuditError("intentional overlap source pair is malformed")
        start, end = _interval({"start": item["start"], "end": item["end"]},
                               "intentional overlap")
        if not isinstance(item["reason"], str) or not item["reason"].strip():
            raise AuditError("intentional overlap has no reason")
        declared_overlaps.append((pair[0], pair[1], start, end))
        overlap_output.append({
            "sources": pair, "start": start, "end": end,
            "count": end - start, "reason": item["reason"],
        })
    if sorted(declared_overlaps) != actual_overlaps:
        raise AuditError(
            "declared overlap set differs from authenticated coverage: declared={} actual={}".format(
                sorted(declared_overlaps), actual_overlaps
            )
        )

    first_gap = contiguous_start
    for start, end in covered:
        if end <= first_gap:
            continue
        if start > first_gap:
            break
        first_gap = max(first_gap, end)
    if first_gap != expected_first_gap:
        raise AuditError("authenticated first gap differs from the ledger")
    total_assigned = sum(item["assigned_count"] for item in extracted)
    unique_count = sum(end - start for start, end in covered)
    for item in extracted:
        del item["_raw_intervals"]
    return {
        "schema": RESULT_SCHEMA, "accepted": True,
        "ledger_sha256": _sha256(ledger_path), "identity": wanted_identity,
        "schedule_sha256s": sorted({
            item["schedule_sha256"] for item in extracted
        }),
        "contiguous_start": contiguous_start, "first_gap": first_gap,
        "unique_intervals": [{"start": start, "end": end} for start, end in covered],
        "total_assigned_count": total_assigned,
        "unique_completed_count": unique_count,
        "duplicate_assignment_count": total_assigned - unique_count,
        "sources": extracted, "intentional_overlaps": overlap_output,
        "certificate_count": sum(item["certificate_count"] for item in extracted),
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
    parser.add_argument("ledger", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--result", type=Path)
    arguments = parser.parse_args(argv)
    try:
        value = audit(arguments.ledger)
        if arguments.result is not None and _load_json(
            arguments.result, "retained coverage result"
        ) != value:
            raise AuditError("retained coverage result differs from recomputation")
        content = _encoded(value)
        if arguments.output is not None:
            _write_atomic(arguments.output, content)
        sys.stdout.write(content)
        return 0
    except (AuditError, OSError) as error:
        sys.stdout.write(_encoded({
            "schema": RESULT_SCHEMA, "accepted": False, "error": str(error),
        }))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
