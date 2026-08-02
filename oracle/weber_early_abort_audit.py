#!/usr/bin/env python3
"""Independently replay smoothness policies over a completed Weber corpus."""

from __future__ import annotations

import argparse
import hashlib
import json
import marshal
from math import isqrt
import os
from pathlib import Path
import sys
import time
import types
from typing import Any, TextIO


CORPUS_SCHEMA = "oneshotsea.weber-oracle-corpus.v2"
RECORD_SCHEMA = "oneshotsea.weber-oracle-curve.v2"
AUDIT_SCHEMA = "oneshotsea.weber-early-abort-audit.v2"
MAX_AUDIT_BITS = 32
MAX_U64 = (1 << 64) - 1
MAX_TRACE_CAP = 4096
MAX_GENERATOR_REJECTIONS = 4096
MAX_OUTPUT_BYTES = 64 * 1024 * 1024
CONFIGURATION_KEYS = {
    "bit_sizes",
    "command_timeout_seconds",
    "curves_per_size",
    "max_generator_rejections",
    "max_level",
    "max_output_bytes",
    "max_prime_attempts",
    "prime_generation_domain",
    "schoof_fallback",
    "sea_threads",
    "seed",
    "smoothness_audited",
    "start_index",
    "trace_cap",
}
TOOL_SOURCE = Path(__file__).resolve()
LOADED_MODULE_CODE = sys._getframe().f_code


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


def stable_code_constant(value: object) -> object:
    if isinstance(value, types.CodeType):
        return ("code", stable_code_payload(value))
    if isinstance(value, slice):
        return (
            "slice",
            stable_code_constant(value.start),
            stable_code_constant(value.stop),
            stable_code_constant(value.step),
        )
    if isinstance(value, tuple):
        return ("tuple", tuple(stable_code_constant(item) for item in value))
    if isinstance(value, frozenset):
        return (
            "frozenset",
            frozenset(stable_code_constant(item) for item in value),
        )
    return value


def stable_code_payload(code: types.CodeType) -> tuple[object, ...]:
    constants = tuple(stable_code_constant(value) for value in code.co_consts)
    line_table = (
        code.co_linetable if hasattr(code, "co_linetable") else code.co_lnotab
    )
    return (
        code.co_argcount,
        code.co_posonlyargcount,
        code.co_kwonlyargcount,
        code.co_nlocals,
        code.co_stacksize,
        code.co_flags,
        code.co_code,
        constants,
        code.co_names,
        code.co_varnames,
        code.co_filename,
        code.co_name,
        getattr(code, "co_qualname", code.co_name),
        code.co_firstlineno,
        line_table,
        getattr(code, "co_exceptiontable", b""),
        code.co_freevars,
        code.co_cellvars,
    )


def code_digest(code: types.CodeType) -> str:
    return hashlib.sha256(marshal.dumps(stable_code_payload(code), 2)).hexdigest()


def source_code_digest(source: bytes) -> str:
    compiled = compile(source, LOADED_MODULE_CODE.co_filename, "exec")
    return code_digest(compiled)


def loaded_module_code_digest() -> str:
    # CPython 3.14 may materialize deferred code metadata on the first complete
    # recursive inspection.  Discard that warm-up serialization, then bind the
    # stable executing code object used for comparison and reporting.
    previous: str | None = None
    for _ in range(8):
        current = code_digest(LOADED_MODULE_CODE)
        if current == previous:
            return current
        previous = current
    fail("loaded audit module code did not stabilize")


def stat_identity(value: os.stat_result) -> tuple[int, int, int, int, int]:
    return (
        value.st_dev,
        value.st_ino,
        value.st_size,
        value.st_mtime_ns,
        value.st_ctime_ns,
    )


def read_stable_bytes(path: Path, label: str) -> tuple[bytes, tuple[int, ...]]:
    with path.open("rb") as stream:
        before = stat_identity(os.fstat(stream.fileno()))
        value = stream.read()
        after = stat_identity(os.fstat(stream.fileno()))
    if before != after or stat_identity(path.stat()) != after:
        fail(f"{label} changed while it was read")
    return value, after


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
    """Return the exact bound-smooth part of an order over a <=32-bit field."""
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


def validate_manifest(
    root: Path,
) -> tuple[
    dict[str, Any],
    Path,
    int,
    str,
    str,
    tuple[int, ...],
    dict[str, int | str | bool | list[int]],
]:
    manifest_path = root / "manifest.json"
    records_path = root / "records.ndjson"
    manifest_bytes, manifest_stat = read_stable_bytes(manifest_path, "manifest")
    try:
        manifest_text = manifest_bytes.decode("utf-8")
    except UnicodeDecodeError as exc:
        fail(f"manifest is not UTF-8: {exc}")
    manifest = load_json(manifest_text, "manifest")
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
    configuration = exact_object(
        manifest.get("configuration"), CONFIGURATION_KEYS, "manifest configuration"
    )
    bit_sizes = configuration["bit_sizes"]
    if (
        type(bit_sizes) is not list
        or not bit_sizes
        or any(type(bits) is not int or bits < 4 or bits > MAX_AUDIT_BITS for bits in bit_sizes)
    ):
        fail(f"early-abort audit is limited to corpus buckets through {MAX_AUDIT_BITS} bits")
    curves_per_size = unsigned_integer(
        configuration["curves_per_size"], "configuration curves-per-size"
    )
    command_timeout = unsigned_integer(
        configuration["command_timeout_seconds"], "configuration timeout"
    )
    max_output = unsigned_integer(
        configuration["max_output_bytes"], "configuration output cap"
    )
    max_prime_attempts = unsigned_integer(
        configuration["max_prime_attempts"], "configuration prime-attempt cap"
    )
    max_generator_rejections = unsigned_integer(
        configuration["max_generator_rejections"],
        "configuration generator-rejection cap",
    )
    max_level = unsigned_integer(configuration["max_level"], "configuration max level")
    sea_threads = unsigned_integer(
        configuration["sea_threads"], "configuration SEA threads"
    )
    trace_cap = unsigned_integer(configuration["trace_cap"], "configuration trace cap")
    seed = decimal(configuration["seed"], "configuration seed")
    start_index = decimal(configuration["start_index"], "configuration start index")
    if (
        curves_per_size == 0
        or command_timeout == 0
        or not 0 < max_output <= MAX_OUTPUT_BYTES
        or max_prime_attempts == 0
        or max_generator_rejections > MAX_GENERATOR_REJECTIONS
        or max_level < 5
        or not 0 < trace_cap <= MAX_TRACE_CAP
        or seed > MAX_U64
        or start_index > MAX_U64
        or configuration["schoof_fallback"] is not True
        or configuration["smoothness_audited"] is not False
        or configuration["prime_generation_domain"]
        != "oneshotsea.weber-oracle-corpus.v1"
    ):
        fail("manifest configuration violates the bounded audit contract")
    scheduled_count = len(bit_sizes) * curves_per_size
    if count != scheduled_count:
        fail("manifest record count disagrees with its bucket schedule")
    next_integer = start_index + count
    if next_integer > MAX_U64 + 1:
        fail("manifest curve schedule crosses the uint64 index boundary")
    expected_next = None if next_integer == MAX_U64 + 1 else str(next_integer)
    if record_identity["next_curve_index"] != expected_next:
        fail("manifest next curve index disagrees with its schedule")
    normalized_configuration: dict[str, int | str | bool | list[int]] = {
        "bit_sizes": list(bit_sizes),
        "curves_per_size": curves_per_size,
        "command_timeout_seconds": command_timeout,
        "max_output_bytes": max_output,
        "max_prime_attempts": max_prime_attempts,
        "max_generator_rejections": max_generator_rejections,
        "prime_generation_domain": str(configuration["prime_generation_domain"]),
        "max_level": max_level,
        "sea_threads": sea_threads,
        "trace_cap": trace_cap,
        "seed": str(seed),
        "start_index": str(start_index),
        "schoof_fallback": True,
        "smoothness_audited": False,
    }
    return (
        manifest,
        records_path,
        count,
        expected_sha,
        hashlib.sha256(manifest_bytes).hexdigest(),
        manifest_stat,
        normalized_configuration,
    )


def audit_record(
    record: dict[str, Any],
    ordinal: int,
    primes: list[int],
    *,
    configuration: dict[str, int | str | bool | list[int]],
    expected_bits: int,
    expected_bucket_ordinal: int,
    expected_index: int,
) -> dict[str, object]:
    if record.get("schema") != RECORD_SCHEMA or record.get("ordinal") != ordinal:
        fail(f"record {ordinal} has an invalid schema or ordinal")
    if (
        unsigned_integer(
            record.get("bucket_ordinal"), f"record {ordinal} bucket ordinal"
        )
        != expected_bucket_ordinal
        or unsigned_integer(
            record.get("prime_generation_attempts"),
            f"record {ordinal} prime-generation attempts",
        )
        not in range(1, int(configuration["max_prime_attempts"]) + 1)
    ):
        fail(f"record {ordinal} disagrees with its configured bucket schedule")
    native = record.get("native")
    heuristic = record.get("heuristic_fallback_off")
    oracle = record.get("oracle")
    if type(native) is not dict or type(heuristic) is not dict or type(oracle) is not dict:
        fail(f"record {ordinal} is missing native/heuristic/oracle objects")
    if (
        native.get("schema") != "oneshotsea.weber-audit.v1"
        or heuristic.get("schema") != "oneshotsea.weber-audit.v1"
    ):
        fail(f"record {ordinal} has an unexpected native schema")
    p = decimal(native.get("p"), f"record {ordinal} p")
    requested_bits = unsigned_integer(record.get("requested_bits"), f"record {ordinal} bit bucket")
    if (
        p.bit_length() != requested_bits
        or requested_bits != expected_bits
        or requested_bits > MAX_AUDIT_BITS
        or decimal(native.get("seed"), f"record {ordinal} native seed")
        != int(configuration["seed"])
        or decimal(native.get("index"), f"record {ordinal} native index")
        != expected_index
        or decimal(native.get("max_level"), f"record {ordinal} native max level")
        != int(configuration["max_level"])
        or decimal(native.get("trace_cap"), f"record {ordinal} native trace cap")
        != int(configuration["trace_cap"])
        or decimal(native.get("sea_threads"), f"record {ordinal} native SEA threads")
        != int(configuration["sea_threads"])
        or native.get("schoof_fallback") is not True
        or native.get("smoothness_audited") is not False
        or native.get("complete") is not True
        or native.get("final_exact_only") is not True
        or decimal(
            native.get("rejected_samples"), f"record {ordinal} generator rejections"
        )
        > int(configuration["max_generator_rejections"])
    ):
        fail(f"record {ordinal} has a mismatched or unsupported bit bucket")
    for key in (
        "p",
        "seed",
        "index",
        "max_level",
        "trace_cap",
        "sea_threads",
        "smoothness_audited",
        "rejected_samples",
        "weber_f",
        "j",
        "twist_parameter",
        "curve",
        "twist",
        "trace_prior",
    ):
        if heuristic.get(key) != native.get(key):
            fail(f"record {ordinal} fallback-off counterfactual changed {key}")
    if heuristic.get("schoof_fallback") is not False:
        fail(f"record {ordinal} counterfactual did not disable fallback")
    if heuristic.get("unique_mode") not in {
        "already_exact_singleton",
        "fresh_cap_one",
    }:
        fail(f"record {ordinal} counterfactual has an invalid unique mode")
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
    if (
        trace_count != len(traces)
        or trace_count == 0
        or trace_count > int(configuration["trace_cap"])
    ):
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
    native_final = native.get("final")
    if (
        type(native_final) is not dict
        or decimal(
            native.get("final_exact_trace"),
            f"record {ordinal} native final trace",
            signed=True,
        )
        != true_trace
        or native_final.get("status") != "complete"
        or native_final.get("trace_count") != "1"
        or native_final.get("traces") != [str(true_trace)]
    ):
        fail(f"record {ordinal} native completion disagrees with the oracle")

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
    heuristic_early = heuristic.get("early")
    if type(heuristic_early) is not dict:
        fail(f"record {ordinal} counterfactual early state is missing")
    used_early_fallback = bool(fallback_levels)
    if used_early_fallback:
        if (
            heuristic_early.get("status") != "level_limit"
            or heuristic_early.get("fallback_levels") != []
            or heuristic_early.get("trace_count") is not None
            or heuristic_early.get("traces") is not None
        ):
            fail(f"record {ordinal} fallback-off first pass is not incomplete")
    elif heuristic_early != early:
        fail(f"record {ordinal} fallback-off first pass changed scientific state")
    counterfactual_complete = heuristic.get("complete")
    if type(counterfactual_complete) is not bool:
        fail(f"record {ordinal} counterfactual completion flag is invalid")
    heuristic_final = heuristic.get("final")
    if type(heuristic_final) is not dict:
        fail(f"record {ordinal} counterfactual final state is missing")
    if counterfactual_complete:
        if (
            heuristic.get("final_exact_only") is not True
            or decimal(
                heuristic.get("final_exact_trace"),
                f"record {ordinal} counterfactual final trace",
                signed=True,
            )
            != true_trace
            or heuristic_final.get("status") != "complete"
            or heuristic_final.get("trace_count") != "1"
            or heuristic_final.get("traces") != [str(true_trace)]
        ):
            fail(f"record {ordinal} completed counterfactual disagrees with the oracle")
    elif (
        heuristic.get("final_exact_only") is not False
        or heuristic.get("final_exact_trace") is not None
        or heuristic_final.get("status") not in {"level_limit", "no_rational_weber_lift"}
        or heuristic_final.get("trace_count") is not None
        or heuristic_final.get("traces") is not None
        or heuristic.get("unique_mode") != "fresh_cap_one"
    ):
        fail(f"record {ordinal} incomplete counterfactual claims completion")
    if used_early_fallback:
        heuristic_stage: str | None = "first_pass"
    elif survivors == 0 or counterfactual_complete:
        heuristic_stage = None
    else:
        heuristic_stage = "second_pass"
    return {
        "trace_count": trace_count,
        "level_count": len(levels),
        "fallback_count": len(fallback_levels),
        "order_evaluations": order_evaluations,
        "opportunity": opportunity,
        "sound_rejection": survivors == 0,
        "heuristic_rejection": heuristic_stage is not None,
        "heuristic_rejection_stage": heuristic_stage,
    }


def audit_corpus(root: Path) -> dict[str, object]:
    started = time.monotonic()
    tool_source, tool_stat = read_stable_bytes(TOOL_SOURCE, "audit tool source")
    tool_source_sha = hashlib.sha256(tool_source).hexdigest()
    expected_code_sha = source_code_digest(tool_source)
    loaded_code_sha = loaded_module_code_digest()
    if expected_code_sha != loaded_code_sha:
        fail("audit tool source does not match its complete loaded module code")
    (
        manifest,
        records_path,
        expected_count,
        expected_records_sha,
        manifest_sha,
        manifest_stat,
        configuration,
    ) = validate_manifest(root)
    # At 32 bits every possible curve/twist order is below 2^32+2^17, while
    # 65537^2 = 2^32+2^17+1. Trial primes through the square root of that strict
    # bound therefore prove complete factorization without a production smooth
    # cache or probabilistic factorization dependency.
    primes = primes_through(isqrt((1 << MAX_AUDIT_BITS) + (1 << 17)))
    results = {
        "order_evaluations": 0,
        "smooth_opportunity_curves": 0,
        "sound_rejections": 0,
        "sound_rejections_before_unique_trace": 0,
        "sound_survivors": 0,
        "sound_false_negatives": 0,
        "heuristic_rejections": 0,
        "heuristic_first_pass_rejections": 0,
        "heuristic_second_pass_rejections": 0,
        "heuristic_false_negatives": 0,
        "early_fallback_curves": 0,
    }
    trace_histogram: dict[str, int] = {}
    level_histogram: dict[str, int] = {}
    fallback_histogram: dict[str, int] = {}
    count = 0
    records_hasher = hashlib.sha256()
    with records_path.open("rb") as stream:
        records_stat = stat_identity(os.fstat(stream.fileno()))
        for count, raw_line in enumerate(stream, start=1):
            records_hasher.update(raw_line)
            if not raw_line.endswith(b"\n"):
                fail(f"record {count - 1} is not newline terminated")
            try:
                line = raw_line.decode("utf-8")
            except UnicodeDecodeError as exc:
                fail(f"record {count - 1} is not UTF-8: {exc}")
            record = load_json(line, f"record {count - 1}")
            if line != canonical_json(record) + "\n":
                fail(f"record {count - 1} is not canonically encoded")
            bucket_index = (count - 1) // int(configuration["curves_per_size"])
            expected_bits = configuration["bit_sizes"][bucket_index]
            expected_bucket_ordinal = (count - 1) % int(
                configuration["curves_per_size"]
            )
            expected_index = int(configuration["start_index"]) + count - 1
            outcome = audit_record(
                record,
                count - 1,
                primes,
                configuration=configuration,
                expected_bits=expected_bits,
                expected_bucket_ordinal=expected_bucket_ordinal,
                expected_index=expected_index,
            )
            results["order_evaluations"] += int(outcome["order_evaluations"])
            opportunity = bool(outcome["opportunity"])
            sound_rejection = bool(outcome["sound_rejection"])
            heuristic_rejection = bool(outcome["heuristic_rejection"])
            heuristic_stage = outcome["heuristic_rejection_stage"]
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
                if heuristic_stage == "first_pass":
                    results["heuristic_first_pass_rejections"] += 1
                    results["early_fallback_curves"] += 1
                elif heuristic_stage == "second_pass":
                    results["heuristic_second_pass_rejections"] += 1
                else:
                    fail(f"record {count - 1} has an invalid heuristic stage")
                if opportunity:
                    results["heuristic_false_negatives"] += 1
            increment(trace_histogram, int(outcome["trace_count"]))
            increment(level_histogram, int(outcome["level_count"]))
            increment(fallback_histogram, int(outcome["fallback_count"]))
        records_stat_after = stat_identity(os.fstat(stream.fileno()))
    actual_records_sha = records_hasher.hexdigest()
    if (
        records_stat_after != records_stat
        or stat_identity(records_path.stat()) != records_stat
    ):
        fail("record stream changed while it was audited")
    if actual_records_sha != expected_records_sha:
        fail("audited record stream digest disagrees with the manifest")
    if count != expected_count:
        fail("record stream count disagrees with the manifest")
    if results["sound_false_negatives"]:
        fail("sound policy produced a false negative")
    completion_manifest, completion_manifest_stat = read_stable_bytes(
        root / "manifest.json", "manifest completion identity"
    )
    completion_tool, completion_tool_stat = read_stable_bytes(
        TOOL_SOURCE, "audit tool completion source"
    )
    if (
        hashlib.sha256(completion_manifest).hexdigest() != manifest_sha
        or completion_manifest_stat != manifest_stat
    ):
        fail("manifest identity changed during the audit")
    if (
        hashlib.sha256(completion_tool).hexdigest() != tool_source_sha
        or completion_tool_stat != tool_stat
        or source_code_digest(completion_tool) != loaded_code_sha
    ):
        fail("audit tool source identity changed during the audit")
    if (
        sha256_file(records_path) != actual_records_sha
        or stat_identity(records_path.stat()) != records_stat
    ):
        fail("record stream completion identity changed during the audit")
    elapsed = time.monotonic() - started
    opportunity_count = results["smooth_opportunity_curves"]
    heuristic_false_negatives = results["heuristic_false_negatives"]
    identity = manifest["identity"]
    report = {
        "schema": AUDIT_SCHEMA,
        "corpus": {
            "path": str(root.resolve()),
            "manifest_sha256": manifest_sha,
            "records_sha256": actual_records_sha,
            "record_count": expected_count,
            "git_commit": identity.get("git_commit"),
            "native_sha256": identity.get("native_sha256"),
        },
        "audit_tool": {
            "path": str(TOOL_SOURCE),
            "sha256": tool_source_sha,
            "loaded_module_code_sha256": loaded_code_sha,
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
            "heuristic_configuration": {
                "schoof_fallback": False,
                "skip_incomplete_curves": True,
            },
            "heuristic_stages": {
                "first_pass": (
                    "skip when the configured trace cap is not reached after all Weber tables"
                ),
                "second_pass": (
                    "after a sound smoothness survivor, skip when a fresh exact cap-one Weber pass is incomplete"
                ),
            },
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
