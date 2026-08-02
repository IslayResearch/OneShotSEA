#!/usr/bin/env python3
"""Independently replay smoothness policies over a completed Weber corpus."""

from __future__ import annotations

import argparse
import hashlib
import json
from math import isqrt
import os
from pathlib import Path
import sys
import time
from typing import Any, TextIO


CORPUS_SCHEMA = "oneshotsea.weber-oracle-corpus.v1"
RECORD_SCHEMA = "oneshotsea.weber-oracle-curve.v1"
AUDIT_SCHEMA = "oneshotsea.weber-early-abort-audit.v1"
MAX_AUDIT_BITS = 32


class AuditError(RuntimeError):
    """A fail-closed corpus or policy-audit error."""


def fail(message: str) -> None:
    raise AuditError(message)


def duplicate_checked_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            fail(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def load_json(text: str, label: str) -> dict[str, Any]:
    try:
        value = json.loads(text, object_pairs_hook=duplicate_checked_object)
    except (json.JSONDecodeError, AuditError) as exc:
        fail(f"{label} is invalid JSON: {exc}")
    if type(value) is not dict:
        fail(f"{label} is not a JSON object")
    return value


def canonical_json(value: object) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def exact_object(value: object, keys: set[str], label: str) -> dict[str, Any]:
    if type(value) is not dict or set(value) != keys:
        fail(f"{label} does not have the exact expected fields")
    return value


def decimal(value: object, label: str, *, signed: bool = False) -> int:
    if type(value) is not str:
        fail(f"{label} is not a decimal string")
    digits = value
    if signed and value.startswith("-"):
        digits = value[1:]
        if not digits or digits == "0":
            fail(f"{label} is not canonical")
    if not digits.isdigit() or (len(digits) > 1 and digits.startswith("0")):
        fail(f"{label} is not canonical")
    return int(value)


def unsigned_integer(value: object, label: str) -> int:
    if type(value) is not int or value < 0:
        fail(f"{label} is not a nonnegative JSON integer")
    return value


def sha256_file(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            result.update(chunk)
    return result.hexdigest()


def primes_through(limit: int) -> list[int]:
    if limit < 2:
        return []
    sieve = bytearray(b"\x01") * (limit + 1)
    sieve[:2] = b"\x00\x00"
    for value in range(2, isqrt(limit) + 1):
        if sieve[value]:
            start = value * value
            sieve[start::value] = b"\x00" * (((limit - start) // value) + 1)
    return [value for value in range(2, limit + 1) if sieve[value]]


def exact_smooth_part(value: int, bound: int, primes: list[int]) -> int:
    """Return the exact bound-smooth part of a positive <=32-bit order."""
    if value <= 0:
        fail("candidate order is nonpositive")
    remaining = value
    smooth = 1
    for prime in primes:
        if prime > bound or prime * prime > remaining:
            break
        while remaining % prime == 0:
            remaining //= prime
            smooth *= prime
    if remaining <= bound:
        # The remaining value is prime: otherwise its least factor would not
        # exceed its square root, which was exhausted above.
        smooth *= remaining
        remaining = 1
    if value % smooth:
        fail("independent smooth part does not divide its order")
    if remaining > 1:
        for prime in primes:
            if prime > bound or prime * prime > remaining:
                break
            if remaining % prime == 0:
                fail("independent smooth extraction left a bounded prime factor")
    return smooth


def represented_traces(residues: list[int], modulus: int, radius: int) -> list[int]:
    result: list[int] = []
    for residue in residues:
        first = -radius + ((residue + radius) % modulus)
        result.extend(range(first, radius + 1, modulus))
    return sorted(result)


def increment(histogram: dict[str, int], value: int) -> None:
    key = str(value)
    histogram[key] = histogram.get(key, 0) + 1


def rate(numerator: int, denominator: int) -> float:
    return numerator / denominator if denominator else 0.0


def validate_manifest(root: Path) -> tuple[dict[str, Any], Path, int, str]:
    manifest_path = root / "manifest.json"
    records_path = root / "records.ndjson"
    manifest = load_json(manifest_path.read_text(encoding="utf-8"), "manifest")
    if manifest.get("schema") != CORPUS_SCHEMA or manifest.get("status") != "complete":
        fail("input is not a completed Weber oracle corpus")
    identity = manifest.get("identity")
    if type(identity) is not dict:
        fail("manifest identity is missing")
    for key in (
        "git_worktree_clean_at_start",
        "git_worktree_clean_at_completion",
        "validated_at_completion",
    ):
        if identity.get(key) is not True:
            fail(f"manifest identity does not assert {key}")
    record_identity = exact_object(
        manifest.get("records"),
        {"path", "count", "sha256", "next_curve_index"},
        "manifest records identity",
    )
    if record_identity["path"] != records_path.name:
        fail("manifest names an unexpected record stream")
    count = unsigned_integer(record_identity["count"], "manifest record count")
    if count == 0:
        fail("corpus has no records")
    expected_sha = record_identity["sha256"]
    if (
        type(expected_sha) is not str
        or len(expected_sha) != 64
        or any(character not in "0123456789abcdef" for character in expected_sha)
    ):
        fail("manifest record digest is invalid")
    if sha256_file(records_path) != expected_sha:
        fail("record stream digest disagrees with the manifest")
    configuration = manifest.get("configuration")
    bit_sizes = configuration.get("bit_sizes") if type(configuration) is dict else None
    if (
        type(bit_sizes) is not list
        or not bit_sizes
        or any(type(bits) is not int or bits < 3 or bits > MAX_AUDIT_BITS for bits in bit_sizes)
    ):
        fail(f"early-abort audit is limited to corpus buckets through {MAX_AUDIT_BITS} bits")
    return manifest, records_path, count, expected_sha


def audit_record(
    record: dict[str, Any],
    ordinal: int,
    primes: list[int],
) -> dict[str, object]:
    if record.get("schema") != RECORD_SCHEMA or record.get("ordinal") != ordinal:
        fail(f"record {ordinal} has an invalid schema or ordinal")
    native = record.get("native")
    oracle = record.get("oracle")
    if type(native) is not dict or type(oracle) is not dict:
        fail(f"record {ordinal} is missing native/oracle objects")
    p = decimal(native.get("p"), f"record {ordinal} p")
    requested_bits = unsigned_integer(record.get("requested_bits"), f"record {ordinal} bit bucket")
    if p.bit_length() != requested_bits or requested_bits > MAX_AUDIT_BITS:
        fail(f"record {ordinal} has a mismatched or unsupported bit bucket")
    radius = isqrt(4 * p)

    early = native.get("early")
    if type(early) is not dict:
        fail(f"record {ordinal} has no early trace state")
    raw_traces = early.get("traces")
    if type(raw_traces) is not list:
        fail(f"record {ordinal} early trace set is unavailable")
    traces = [
        decimal(value, f"record {ordinal} trace {index}", signed=True)
        for index, value in enumerate(raw_traces)
    ]
    if traces != sorted(set(traces)) or any(abs(trace) > radius for trace in traces):
        fail(f"record {ordinal} trace list is noncanonical")
    trace_count = decimal(early.get("trace_count"), f"record {ordinal} trace count")
    if trace_count != len(traces) or trace_count == 0:
        fail(f"record {ordinal} trace count disagrees with its trace list")
    modulus = decimal(
        early.get("constraint_modulus"), f"record {ordinal} constraint modulus"
    )
    raw_residues = early.get("effective_residue_classes")
    if type(raw_residues) is not list:
        fail(f"record {ordinal} effective residue state is unavailable")
    residues = [
        decimal(value, f"record {ordinal} effective residue {index}")
        for index, value in enumerate(raw_residues)
    ]
    if (
        modulus < 1
        or residues != sorted(set(residues))
        or any(residue >= modulus for residue in residues)
        or represented_traces(residues, modulus, radius) != traces
    ):
        fail(f"record {ordinal} effective residues do not reproduce its trace set")

    curve_oracle = exact_object(
        oracle.get("curve"), {"order", "trace"}, f"record {ordinal} curve oracle"
    )
    twist_oracle = exact_object(
        oracle.get("twist"), {"order", "trace"}, f"record {ordinal} twist oracle"
    )
    true_trace = decimal(
        curve_oracle["trace"], f"record {ordinal} oracle trace", signed=True
    )
    curve_order = decimal(curve_oracle["order"], f"record {ordinal} curve order")
    twist_trace = decimal(
        twist_oracle["trace"], f"record {ordinal} twist trace", signed=True
    )
    twist_order = decimal(twist_oracle["order"], f"record {ordinal} twist order")
    if true_trace not in traces:
        fail(f"record {ordinal} true trace is absent from the early candidate set")
    if (
        curve_order != p + 1 - true_trace
        or twist_trace != -true_trace
        or twist_order != p + 1 + true_trace
        or curve_order + twist_order != 2 * p + 2
    ):
        fail(f"record {ordinal} oracle curve/twist identities disagree")

    smooth_bound = requested_bits**4
    q = isqrt(p)
    lower_bound = q + 1 + isqrt(4 * q)
    survivors = 0
    true_curve_smooth: int | None = None
    true_twist_smooth: int | None = None
    order_evaluations = 0
    for trace in traces:
        for twist in (False, True):
            order = p + 1 + trace if twist else p + 1 - trace
            smooth = exact_smooth_part(order, smooth_bound, primes)
            order_evaluations += 1
            if smooth > lower_bound:
                survivors += 1
            if trace == true_trace and not twist:
                true_curve_smooth = smooth
            if trace == true_trace and twist:
                true_twist_smooth = smooth
    if true_curve_smooth is None or true_twist_smooth is None:
        fail(f"record {ordinal} did not audit both true sides")
    opportunity = (
        true_curve_smooth > lower_bound or true_twist_smooth > lower_bound
    )
    fallback_levels = early.get("fallback_levels")
    levels = early.get("levels")
    if type(fallback_levels) is not list or type(levels) is not list:
        fail(f"record {ordinal} has invalid level/fallback arrays")
    return {
        "trace_count": trace_count,
        "level_count": len(levels),
        "fallback_count": len(fallback_levels),
        "order_evaluations": order_evaluations,
        "opportunity": opportunity,
        "sound_rejection": survivors == 0,
        "heuristic_rejection": bool(fallback_levels),
    }


def audit_corpus(root: Path) -> dict[str, object]:
    started = time.monotonic()
    manifest, records_path, expected_count, records_sha = validate_manifest(root)
    # At 32 bits every possible curve/twist order is below 2^32+2^17.  Trial
    # primes through its square root prove complete factorization without a
    # production smooth cache or probabilistic factorization dependency.
    primes = primes_through(isqrt((1 << MAX_AUDIT_BITS) + (1 << 17)))
    results = {
        "order_evaluations": 0,
        "smooth_opportunity_curves": 0,
        "sound_rejections": 0,
        "sound_rejections_before_unique_trace": 0,
        "sound_survivors": 0,
        "sound_false_negatives": 0,
        "heuristic_rejections": 0,
        "heuristic_false_negatives": 0,
        "fallback_curves": 0,
    }
    trace_histogram: dict[str, int] = {}
    level_histogram: dict[str, int] = {}
    fallback_histogram: dict[str, int] = {}
    count = 0
    with records_path.open("r", encoding="utf-8") as stream:
        for count, line in enumerate(stream, start=1):
            if not line.endswith("\n"):
                fail(f"record {count - 1} is not newline terminated")
            record = load_json(line, f"record {count - 1}")
            if line != canonical_json(record) + "\n":
                fail(f"record {count - 1} is not canonically encoded")
            outcome = audit_record(record, count - 1, primes)
            results["order_evaluations"] += int(outcome["order_evaluations"])
            opportunity = bool(outcome["opportunity"])
            sound_rejection = bool(outcome["sound_rejection"])
            heuristic_rejection = bool(outcome["heuristic_rejection"])
            if opportunity:
                results["smooth_opportunity_curves"] += 1
            if sound_rejection:
                results["sound_rejections"] += 1
                if int(outcome["trace_count"]) > 1:
                    results["sound_rejections_before_unique_trace"] += 1
                if opportunity:
                    results["sound_false_negatives"] += 1
            else:
                results["sound_survivors"] += 1
            if heuristic_rejection:
                results["heuristic_rejections"] += 1
                results["fallback_curves"] += 1
                if opportunity:
                    results["heuristic_false_negatives"] += 1
            increment(trace_histogram, int(outcome["trace_count"]))
            increment(level_histogram, int(outcome["level_count"]))
            increment(fallback_histogram, int(outcome["fallback_count"]))
    if count != expected_count:
        fail("record stream count disagrees with the manifest")
    if results["sound_false_negatives"]:
        fail("sound policy produced a false negative")
    elapsed = time.monotonic() - started
    opportunity_count = results["smooth_opportunity_curves"]
    heuristic_false_negatives = results["heuristic_false_negatives"]
    identity = manifest["identity"]
    report = {
        "schema": AUDIT_SCHEMA,
        "corpus": {
            "path": str(root.resolve()),
            "manifest_sha256": sha256_file(root / "manifest.json"),
            "records_sha256": records_sha,
            "record_count": expected_count,
            "git_commit": identity.get("git_commit"),
            "native_sha256": identity.get("native_sha256"),
        },
        "audit_tool": {
            "path": str(Path(__file__).resolve()),
            "sha256": sha256_file(Path(__file__).resolve()),
            "python": sys.version.split()[0],
        },
        "policy": {
            "smooth_bound": "bit_length(p)^4",
            "lower_bound": (
                "floor(sqrt(p))+1+floor(2*sqrt(floor(sqrt(p))))"
            ),
            "sound": (
                "reject only when every enumerated curve/twist order has "
                "exact smooth part <= lower_bound"
            ),
            "heuristic": (
                "skip states that require retained exact-Schoof fallback"
            ),
            "heuristic_false_negative_label": (
                "true curve/twist has exact smooth part > lower_bound"
            ),
        },
        "results": {
            **results,
            "exact_off_full_point_counts": expected_count,
            "sound_exact_on_full_point_counts": (
                expected_count - results["sound_rejections_before_unique_trace"]
            ),
            "sound_saved_full_point_counts": results[
                "sound_rejections_before_unique_trace"
            ],
            "sound_rejection_rate": rate(results["sound_rejections"], expected_count),
            "heuristic_rejection_rate": rate(
                results["heuristic_rejections"], expected_count
            ),
            "heuristic_false_negative_rate_all": rate(
                heuristic_false_negatives, expected_count
            ),
            "heuristic_false_negative_rate_opportunities": rate(
                heuristic_false_negatives, opportunity_count
            ),
            "trace_count_histogram": trace_histogram,
            "level_count_histogram": level_histogram,
            "fallback_count_histogram": fallback_histogram,
        },
        "runtime": {
            "audit_wall_seconds": elapsed,
            "trial_prime_count": len(primes),
            "maximum_supported_bits": MAX_AUDIT_BITS,
        },
    }
    return report


def write_report(report: dict[str, object], output: Path | None, stream: TextIO) -> None:
    encoded = canonical_json(report) + "\n"
    if output is not None:
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("x", encoding="utf-8") as destination:
            destination.write(encoded)
            destination.flush()
            os.fsync(destination.fileno())
    stream.write(encoded)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Audit sound and incomplete-skip policies over a Weber corpus"
    )
    parser.add_argument("corpus", type=Path)
    parser.add_argument("--output", type=Path)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        write_report(audit_corpus(args.corpus.resolve()), args.output, sys.stdout)
    except (AuditError, OSError) as exc:
        print(f"Weber early-abort audit: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
