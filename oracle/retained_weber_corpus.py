#!/usr/bin/env python3
"""Audit a compact retained production-Weber/Magma corpus.

The live corpus driver performs the expensive checks while Magma and the native
point counter are running.  This module provides the bounded clean-checkout
gate for a retained corpus: it authenticates the capture manifest and record
stream, then independently replays every mathematical claim that remains in
the normalized records against the captured Magma trace.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import gzip
import hashlib
import io
import json
import math
import os
from pathlib import Path
import stat
import sys
from typing import Any

from audit_common import AuditError, probably_prime
import weber_corpus_audit_driver as live_driver


RESULT_SCHEMA = "oneshotsea.retained-weber-oracle-result.v1"
CORPUS_SCHEMA = "oneshotsea.weber-oracle-corpus.v2"
RECORD_SCHEMA = "oneshotsea.weber-oracle-curve.v2"
MAX_RETAINED_BYTES = 128 * 1024 * 1024
MAX_RETAINED_RECORDS = 100_000
MAX_JSON_BYTES = 1024 * 1024
MAX_RECORD_BYTES = 1024 * 1024
REPOSITORY_ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class ConstraintState:
    modulus: int
    residues: tuple[int, ...]


class RetainedCorpusError(RuntimeError):
    """Raised when retained oracle evidence is incomplete or inconsistent."""


def fail(message: str) -> None:
    raise RetainedCorpusError(message)


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            fail(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_json_bytes(data: bytes, label: str) -> dict[str, Any]:
    try:
        value = json.loads(data, object_pairs_hook=reject_duplicate_keys)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        fail(f"invalid {label} JSON: {error}")
    if type(value) is not dict:
        fail(f"{label} is not a JSON object")
    return value


def read_bounded_file(path: Path, label: str, maximum: int) -> bytes:
    flags = os.O_RDONLY | os.O_NONBLOCK
    flags |= getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags)
    except OSError as error:
        fail(f"cannot open regular {label}: {path}: {error}")
    try:
        try:
            attributes = os.fstat(descriptor)
        except OSError as error:
            fail(f"cannot inspect {label}: {path}: {error}")
        if not stat.S_ISREG(attributes.st_mode):
            fail(f"missing or non-regular {label}: {path}")
        if attributes.st_size < 0 or attributes.st_size > maximum:
            fail(f"{label} exceeds the {maximum}-byte audit limit")
        data = bytearray()
        try:
            while len(data) <= maximum:
                block = os.read(
                    descriptor,
                    min(1024 * 1024, maximum + 1 - len(data)),
                )
                if not block:
                    break
                data.extend(block)
        except OSError as error:
            fail(f"cannot read {label}: {path}: {error}")
        if len(data) > maximum:
            fail(f"{label} exceeds the {maximum}-byte audit limit")
        if len(data) != attributes.st_size:
            fail(f"{label} changed while it was read")
        return bytes(data)
    finally:
        os.close(descriptor)


def load_json(path: Path, label: str) -> dict[str, Any]:
    return load_json_bytes(read_bounded_file(path, label, MAX_JSON_BYTES), label)


def object_value(value: object, label: str) -> dict[str, Any]:
    if type(value) is not dict:
        fail(f"{label} is not a JSON object")
    return value


def list_value(value: object, label: str) -> list[Any]:
    if type(value) is not list:
        fail(f"{label} is not a JSON array")
    return value


def decimal(value: object, label: str, *, signed: bool = False) -> int:
    if type(value) is not str or not value:
        fail(f"{label} is not a canonical decimal string")
    if value == "0":
        return 0
    digits = value
    if signed and value.startswith("-"):
        digits = value[1:]
    if not digits or not digits.isascii() or not digits.isdigit() or digits[0] == "0":
        fail(f"{label} is not a canonical decimal string")
    if value.startswith("-") and not signed:
        fail(f"{label} must be unsigned")
    return int(value, 10)


def exact_int(value: object, label: str) -> int:
    if type(value) is not int:
        fail(f"{label} is not an integer")
    return value


def decimal_vector(
    value: object,
    label: str,
    *,
    signed: bool = False,
    modulus: int | None = None,
) -> tuple[int, ...]:
    result = tuple(
        decimal(item, f"{label}[{index}]", signed=signed)
        for index, item in enumerate(list_value(value, label))
    )
    if result != tuple(sorted(set(result))):
        fail(f"{label} is not sorted and duplicate-free")
    if modulus is not None and any(item < 0 or item >= modulus for item in result):
        fail(f"{label} contains a noncanonical residue")
    return result


def refine(state: ConstraintState, ell: int, allowed: tuple[int, ...]) -> ConstraintState:
    if ell < 2 or math.gcd(state.modulus, ell) != 1:
        fail("constraint replay encountered a non-coprime modulus")
    canonical = tuple(sorted(set(item % ell for item in allowed)))
    inverse = pow(state.modulus % ell, -1, ell)
    new_modulus = state.modulus * ell
    merged = {
        (old + state.modulus * (((small - old) * inverse) % ell)) % new_modulus
        for old in state.residues
        for small in canonical
    }
    return ConstraintState(new_modulus, tuple(sorted(merged)))


def candidate_count(prime: int, state: ConstraintState) -> int:
    radius = math.isqrt(4 * prime)
    total = 0
    for residue in state.residues:
        first = residue + (
            (-radius - residue + state.modulus - 1) // state.modulus
        ) * state.modulus
        if first <= radius:
            total += (radius - first) // state.modulus + 1
    return total


def enumerate_traces(
    prime: int, state: ConstraintState, cap: int
) -> tuple[int, ...] | None:
    if candidate_count(prime, state) > cap:
        return None
    radius = math.isqrt(4 * prime)
    traces: list[int] = []
    for residue in state.residues:
        first = residue + (
            (-radius - residue + state.modulus - 1) // state.modulus
        ) * state.modulus
        traces.extend(range(first, radius + 1, state.modulus))
    return tuple(sorted(traces))


def live_replay(function: Any, *arguments: Any) -> Any:
    try:
        return function(*arguments)
    except AuditError as error:
        fail(str(error))


def read_gzip_records(
    compressed: bytes, expected_count: int, expected_bytes: int
) -> tuple[list[dict[str, Any]], str, int]:
    records: list[dict[str, Any]] = []
    digest = hashlib.sha256()
    total = 0
    pending = bytearray()
    try:
        with gzip.GzipFile(fileobj=io.BytesIO(compressed), mode="rb") as stream:
            while True:
                chunk = stream.read(64 * 1024)
                if not chunk:
                    break
                total += len(chunk)
                if total > expected_bytes or total > MAX_RETAINED_BYTES:
                    fail("decompressed records exceed their declared bounds")
                digest.update(chunk)
                pending.extend(chunk)
                while True:
                    newline = pending.find(b"\n")
                    if newline < 0:
                        break
                    if newline + 1 > MAX_RECORD_BYTES:
                        fail("a decompressed record exceeds the per-record audit limit")
                    if len(records) >= expected_count:
                        fail("decompressed records exceed their declared count")
                    line = bytes(pending[: newline + 1])
                    del pending[: newline + 1]
                    records.append(load_json_bytes(line, f"record {len(records)}"))
                if len(pending) > MAX_RECORD_BYTES:
                    fail("a decompressed record exceeds the per-record audit limit")
    except (OSError, EOFError) as error:
        fail(f"cannot decompress retained records: {error}")
    if pending:
        fail("decompressed record stream lacks a final newline")
    if total != expected_bytes or len(records) != expected_count:
        fail("decompressed record size/count differs from the retained result")
    return records, digest.hexdigest(), total


def validate_retained_inputs(
    root: Path,
    result: dict[str, Any],
    identity: dict[str, Any],
    table_set: dict[str, Any],
) -> None:
    retained = object_value(result.get("retained_inputs"), "retained inputs")
    expected_hashes = {
        "oracle_weber_audit": identity.get("native_sha256"),
        "weber_corpus_audit.py": identity.get("corpus_bootstrap_sha256"),
        "weber_corpus_audit_driver.py": identity.get("corpus_driver_sha256"),
        "audit_common.py": identity.get("audit_common_artifact_sha256"),
        "point_count.m": identity.get("point_count_script_sha256"),
        "prime_check.m": identity.get("prime_check_script_sha256"),
    }
    if set(retained) != set(expected_hashes):
        fail("retained input inventory is incomplete or has unexpected files")
    input_root = root / "raw" / "inputs"
    for name, manifest_hash in expected_hashes.items():
        metadata = object_value(retained.get(name), f"retained input {name}")
        path = input_root / name
        size = exact_int(metadata.get("bytes"), f"retained input {name} bytes")
        expected_hash = metadata.get("sha256")
        if size < 0 or size > MAX_RETAINED_BYTES:
            fail(f"retained input identity differs from the capture manifest: {name}")
        payload = read_bounded_file(
            path, f"retained input {name}", MAX_RETAINED_BYTES
        )
        if (
            len(payload) != size
            or expected_hash != manifest_hash
            or hashlib.sha256(payload).hexdigest() != expected_hash
        ):
            fail(f"retained input identity differs from the capture manifest: {name}")

    entries = list_value(table_set.get("files"), "manifest table files")
    expected_paths: set[Path] = set()
    table_root = REPOSITORY_ROOT / "data" / "modpoly"
    for ordinal, raw in enumerate(entries):
        entry = object_value(raw, f"manifest table file {ordinal}")
        relative_text = entry.get("path")
        if type(relative_text) is not str:
            fail(f"manifest table file {ordinal} has an invalid path")
        relative = Path(relative_text)
        if (
            relative.is_absolute()
            or ".." in relative.parts
            or not relative.parts
            or relative.parts[0] not in {"weber_f", "j"}
        ):
            fail(f"manifest table file {ordinal} escapes the table roots")
        path = table_root / relative
        if path in expected_paths:
            fail(f"manifest table inventory repeats {relative_text}")
        expected_paths.add(path)
        expected_bytes = exact_int(
            entry.get("bytes"), f"manifest table file {ordinal} bytes"
        )
        if expected_bytes < 0 or expected_bytes > MAX_RETAINED_BYTES:
            fail(f"checked-in table differs from the capture input: {relative_text}")
        payload = read_bounded_file(
            path, f"manifest table file {ordinal}", MAX_RETAINED_BYTES
        )
        if (
            len(payload) != expected_bytes
            or hashlib.sha256(payload).hexdigest() != entry.get("sha256")
        ):
            fail(f"checked-in table differs from the capture input: {relative_text}")
    actual_paths = {
        path
        for directory in (table_root / "weber_f", table_root / "j")
        for path in directory.rglob("*")
        if path.is_file()
    }
    if actual_paths != expected_paths:
        fail("checked-in table inventory differs from the capture manifest")


def validate_state(
    value: object,
    *,
    prime: int,
    oracle_trace: int,
    weber_f: int,
    initial: ConstraintState,
    trace_cap: int,
    label: str,
    final: bool,
) -> tuple[dict[str, int], ConstraintState, ConstraintState]:
    state = object_value(value, label)
    exact = initial
    effective = initial
    retained_atkin: list[tuple[int, int, tuple[int, ...]]] = []
    exact_claims = 0
    atkin_claims = 0
    unconstrained = 0
    previous_ell = 0
    for ordinal, raw in enumerate(list_value(state.get("levels"), f"{label} levels")):
        level = object_value(raw, f"{label} level {ordinal}")
        ell = exact_int(level.get("ell"), f"{label} level {ordinal} ell")
        if ell <= previous_ell or ell < 5 or ell % 2 == 0 or not probably_prime(ell):
            fail(f"{label} has an invalid or out-of-order level {ell}")
        previous_ell = ell
        classification = level.get("classification")
        is_exact = level.get("exact")
        if type(is_exact) is not bool:
            fail(f"{label} level {ell} has a non-Boolean exact flag")
        discriminant = (
            (oracle_trace % ell) * (oracle_trace % ell) - 4 * (prime % ell)
        ) % ell
        discriminant_square = (
            discriminant == 0 or pow(discriminant, (ell - 1) // 2, ell) == 1
        )
        supersingular_exception = (
            oracle_trace == 0 and not discriminant_square and ell in {5, 7}
        )
        allowed_classifications = (
            {"exact_elkies", "unconstrained"}
            if discriminant_square
            else (
                {"certified_atkin", "unconstrained"}
                if supersingular_exception
                else ({"certified_atkin"} if ell in {5, 7} else {"unconstrained"})
            )
        )
        if classification not in allowed_classifications:
            fail(f"{label} level {ell} classification disagrees with Magma")
        if classification == "exact_elkies":
            residue = exact_int(
                level.get("trace_residue"), f"{label} level {ell} residue"
            )
            if not is_exact or residue != oracle_trace % ell or not discriminant_square:
                fail(f"{label} exact Elkies level {ell} disagrees with Magma")
            if level.get("atkin_projective_order") is not None:
                fail(f"{label} exact Elkies level {ell} also claims Atkin data")
            if decimal(
                level.get("atkin_residue_count"),
                f"{label} level {ell} Atkin residue count",
            ) != 0:
                fail(f"{label} exact Elkies level {ell} claims Atkin residues")
            exact = refine(exact, ell, (residue,))
            effective = refine(effective, ell, (residue,))
            exact_claims += 1
        elif classification == "certified_atkin":
            order = exact_int(
                level.get("atkin_projective_order"),
                f"{label} level {ell} Atkin order",
            )
            expected_order = live_replay(
                live_driver.projective_order, ell, prime, oracle_trace
            )
            residues = live_replay(
                live_driver.atkin_residues, ell, prime, expected_order
            )
            if (
                is_exact
                or level.get("trace_residue") is not None
                or discriminant_square
                or order != expected_order
                or decimal(
                    level.get("atkin_residue_count"),
                    f"{label} level {ell} Atkin residue count",
                )
                != len(residues)
            ):
                fail(f"{label} certified Atkin level {ell} disagrees with Magma")
            effective = refine(effective, ell, residues)
            retained_atkin.append((ell, order, residues))
            atkin_claims += 1
        elif classification == "unconstrained":
            if (
                is_exact
                or level.get("trace_residue") is not None
                or level.get("atkin_projective_order") is not None
                or decimal(
                    level.get("atkin_residue_count"),
                    f"{label} level {ell} Atkin residue count",
                )
                != 0
            ):
                fail(f"{label} unconstrained level {ell} claims trace evidence")
            unconstrained += 1
        else:
            fail(f"{label} level {ell} has an unknown classification")

        if decimal(level.get("exact_modulus"), f"{label} level {ell} exact modulus") != exact.modulus:
            fail(f"{label} level {ell} exact modulus disagrees with CRT replay")
        if decimal(
            level.get("constraint_modulus"),
            f"{label} level {ell} constraint modulus",
        ) != effective.modulus:
            fail(f"{label} level {ell} effective modulus disagrees with CRT replay")
        if decimal(
            level.get("exact_trace_candidate_count"),
            f"{label} level {ell} exact candidate count",
        ) != candidate_count(prime, exact):
            fail(f"{label} level {ell} exact candidate count disagrees with replay")
        if decimal(
            level.get("trace_candidate_count"),
            f"{label} level {ell} effective candidate count",
        ) != candidate_count(prime, effective):
            fail(f"{label} level {ell} effective candidate count disagrees with replay")
        if decimal(
            level.get("compatible_source_lifts"),
            f"{label} level {ell} compatible source lifts",
        ) != 1:
            fail(f"{label} level {ell} lost or duplicated its bound Weber source")

    fallback_claims = 0
    fallback_cursor = 0
    for ordinal, raw in enumerate(
        list_value(state.get("fallback_levels"), f"{label} fallback levels")
    ):
        level = object_value(raw, f"{label} fallback level {ordinal}")
        ell = exact_int(level.get("ell"), f"{label} fallback level {ordinal} ell")
        residue = exact_int(
            level.get("trace_residue"), f"{label} fallback level {ell} residue"
        )
        while (
            fallback_cursor < len(live_driver.FALLBACK_LEVELS)
            and exact.modulus % live_driver.FALLBACK_LEVELS[fallback_cursor] == 0
        ):
            fallback_cursor += 1
        if (
            level.get("classification") != "exact_schoof"
            or fallback_cursor == len(live_driver.FALLBACK_LEVELS)
            or ell != live_driver.FALLBACK_LEVELS[fallback_cursor]
            or residue != oracle_trace % ell
        ):
            fail(f"{label} exact Schoof fallback level {ell} disagrees with Magma")
        exact = refine(exact, ell, (residue,))
        effective = exact
        for atkin_ell, _order, residues in retained_atkin:
            if effective.modulus % atkin_ell:
                effective = refine(effective, atkin_ell, residues)
            elif any(item % atkin_ell not in residues for item in effective.residues):
                fail(f"{label} fallback contradicts retained Atkin level {atkin_ell}")
        if decimal(
            level.get("exact_modulus"), f"{label} fallback level {ell} exact modulus"
        ) != exact.modulus:
            fail(f"{label} fallback level {ell} exact modulus disagrees with replay")
        if decimal(
            level.get("constraint_modulus"),
            f"{label} fallback level {ell} constraint modulus",
        ) != effective.modulus:
            fail(f"{label} fallback level {ell} effective modulus disagrees with replay")
        if decimal(
            level.get("exact_trace_candidate_count"),
            f"{label} fallback level {ell} exact candidate count",
        ) != candidate_count(prime, exact):
            fail(f"{label} fallback level {ell} exact candidate count disagrees with replay")
        if decimal(
            level.get("trace_candidate_count"),
            f"{label} fallback level {ell} effective candidate count",
        ) != candidate_count(prime, effective):
            fail(f"{label} fallback level {ell} effective candidate count disagrees with replay")
        fallback_claims += 1
        fallback_cursor += 1

    exact_modulus = decimal(state.get("exact_modulus"), f"{label} exact modulus")
    constraint_modulus = decimal(
        state.get("constraint_modulus"), f"{label} constraint modulus"
    )
    exact_classes = decimal_vector(
        state.get("exact_residue_classes"),
        f"{label} exact residue classes",
        modulus=exact_modulus,
    )
    effective_classes = decimal_vector(
        state.get("effective_residue_classes"),
        f"{label} effective residue classes",
        modulus=constraint_modulus,
    )
    if (exact_modulus, exact_classes) != (exact.modulus, exact.residues):
        fail(f"{label} final exact CRT state disagrees with replay")
    if (constraint_modulus, effective_classes) != (
        effective.modulus,
        effective.residues,
    ):
        fail(f"{label} final effective CRT state disagrees with replay")
    if decimal(
        state.get("exact_trace_candidate_count"), f"{label} exact candidate count"
    ) != candidate_count(prime, exact):
        fail(f"{label} exact candidate count disagrees with replay")
    if decimal(
        state.get("trace_candidate_count"), f"{label} effective candidate count"
    ) != candidate_count(prime, effective):
        fail(f"{label} effective candidate count disagrees with replay")
    if decimal_vector(
        state.get("compatible_source_lifts"),
        f"{label} compatible source lifts",
        modulus=prime,
    ) != (weber_f,):
        fail(f"{label} does not retain exactly the deterministic Weber source")

    emitted_atkin: list[tuple[int, int, tuple[int, ...]]] = []
    for ordinal, raw in enumerate(
        list_value(state.get("atkin_constraints"), f"{label} Atkin constraints")
    ):
        constraint = object_value(raw, f"{label} Atkin constraint {ordinal}")
        emitted_atkin.append(
            (
                exact_int(constraint.get("ell"), f"{label} Atkin ell"),
                exact_int(
                    constraint.get("projective_order"), f"{label} Atkin order"
                ),
                tuple(
                    exact_int(item, f"{label} Atkin residue")
                    for item in list_value(
                        constraint.get("trace_residues"),
                        f"{label} Atkin residues",
                    )
                ),
            )
        )
    if emitted_atkin != retained_atkin:
        fail(f"{label} retained Atkin list disagrees with level replay")

    traces_value = state.get("traces")
    status = state.get("status")
    expected_traces = enumerate_traces(
        prime, effective, 1 if final else trace_cap
    )
    if traces_value is None:
        if state.get("trace_count") is not None or expected_traces is not None:
            fail(f"{label} trace enumeration disagrees with CRT replay")
    else:
        traces = decimal_vector(traces_value, f"{label} traces", signed=True)
        if traces != expected_traces:
            fail(f"{label} trace enumeration disagrees with CRT replay")
        if decimal(state.get("trace_count"), f"{label} trace count") != len(traces):
            fail(f"{label} trace count disagrees with its trace list")
        if oracle_trace not in traces:
            fail(f"{label} enumerated traces omit the Magma trace")
    expected_status = (
        "complete"
        if final and expected_traces is not None
        else ("trace_set_enumerated" if expected_traces is not None else "level_limit")
    )
    if status != expected_status:
        fail(f"{label} status disagrees with CRT replay")
    return {
        "exact_elkies": exact_claims,
        "certified_atkin": atkin_claims,
        "unconstrained": unconstrained,
        "exact_schoof": fallback_claims,
    }, exact, effective


def validate_native(
    value: object,
    *,
    prime: int,
    oracle_trace: int,
    expected_seed: int,
    expected_index: int,
    expected_max_level: int,
    expected_trace_cap: int,
    expected_sea_threads: int,
    expected_fallback: bool,
    max_generator_rejections: int,
    label: str,
    require_complete: bool,
) -> dict[str, int]:
    native = object_value(value, label)
    if native.get("schema") != "oneshotsea.weber-audit.v1":
        fail(f"{label} has an unexpected schema")
    echoed = (
        decimal(native.get("p"), f"{label} prime"),
        decimal(native.get("seed"), f"{label} seed"),
        decimal(native.get("index"), f"{label} index"),
        decimal(native.get("max_level"), f"{label} max level"),
        decimal(native.get("trace_cap"), f"{label} trace cap"),
        decimal(native.get("sea_threads"), f"{label} SEA threads"),
    )
    if echoed != (
        prime,
        expected_seed,
        expected_index,
        expected_max_level,
        expected_trace_cap,
        expected_sea_threads,
    ):
        fail(f"{label} input identity differs from the corpus configuration")
    if native.get("schoof_fallback") is not expected_fallback:
        fail(f"{label} fallback policy differs from the corpus configuration")
    if native.get("smoothness_audited") is not False:
        fail(f"{label} unexpectedly claims smoothness coverage")

    rejected = decimal(native.get("rejected_samples"), f"{label} rejected samples")
    weber_f = decimal(native.get("weber_f"), f"{label} Weber source")
    j = decimal(native.get("j"), f"{label} j-invariant")
    twist_parameter = decimal(
        native.get("twist_parameter"), f"{label} twist parameter"
    )
    if not (
        rejected <= max_generator_rejections
        and 0 < weber_f < prime
        and 0 <= j < prime
        and j not in {0, 1728 % prime}
        and 2 <= twist_parameter < prime
        and twist_parameter <= live_driver.MAX_TWIST_PARAMETER
    ):
        fail(f"{label} curve/source metadata is noncanonical or unbounded")
    f24 = pow(weber_f, 24, prime)
    if pow(f24, -1, prime) * pow((f24 - 16) % prime, 3, prime) % prime != j:
        fail(f"{label} Weber source does not map to its j-invariant")
    live_replay(
        live_driver.validate_deterministic_weber_sample,
        prime,
        expected_seed,
        expected_index,
        rejected,
        weber_f,
        j,
        max_generator_rejections,
    )

    def parse_curve(raw: object, curve_label: str) -> dict[str, int]:
        curve = object_value(raw, curve_label)
        if set(curve) != {"a", "b"}:
            fail(f"{curve_label} has an unexpected schema")
        parsed = {
            "a": decimal(curve.get("a"), f"{curve_label} a"),
            "b": decimal(curve.get("b"), f"{curve_label} b"),
        }
        if any(coefficient >= prime for coefficient in parsed.values()):
            fail(f"{curve_label} has a noncanonical coefficient")
        if (4 * pow(parsed["a"], 3, prime) + 27 * pow(parsed["b"], 2, prime)) % prime == 0:
            fail(f"{curve_label} is singular")
        return parsed

    curve = parse_curve(native.get("curve"), f"{label} curve")
    twist = parse_curve(native.get("twist"), f"{label} twist")
    k = j * pow((1728 - j) % prime, -1, prime) % prime
    if curve != {"a": 3 * k % prime, "b": 2 * k % prime}:
        fail(f"{label} curve is not the canonical production model for j")
    least_nonsquare = next(
        (
            candidate
            for candidate in range(2, twist_parameter + 1)
            if pow(candidate, (prime - 1) // 2, prime) == prime - 1
        ),
        None,
    )
    if least_nonsquare != twist_parameter:
        fail(f"{label} twist parameter is not the least quadratic nonsquare")
    expected_twist = {
        "a": curve["a"] * pow(twist_parameter, 2, prime) % prime,
        "b": curve["b"] * pow(twist_parameter, 3, prime) % prime,
    }
    if twist != expected_twist:
        fail(f"{label} twist does not match its curve and parameter")
    if (
        live_replay(live_driver.curve_j, prime, curve) != j
        or live_replay(live_driver.curve_j, prime, twist) != j
    ):
        fail(f"{label} curve/twist j identity disagrees")

    root_count = live_replay(
        live_driver.rational_two_torsion_roots,
        prime,
        curve["a"],
        curve["b"],
    )
    prior = native.get("trace_prior")
    if prior is None:
        if root_count == 3:
            fail(f"{label} omitted its full-rational-2-torsion prior")
        initial = ConstraintState(1, (0,))
    else:
        prior_object = object_value(prior, f"{label} trace prior")
        modulus = decimal(prior_object.get("modulus"), f"{label} prior modulus")
        residue = decimal(prior_object.get("residue"), f"{label} prior residue")
        if (
            root_count != 3
            or (modulus, residue) != (4, (prime + 1) % 4)
            or oracle_trace % modulus != residue
        ):
            fail(f"{label} trace prior lacks the required geometric provenance")
        initial = ConstraintState(modulus, (residue,))

    _early_counts, early_exact, early_effective = validate_state(
        native.get("early"),
        prime=prime,
        oracle_trace=oracle_trace,
        weber_f=weber_f,
        initial=initial,
        trace_cap=expected_trace_cap,
        label=f"{label} early state",
        final=False,
    )
    complete = native.get("complete")
    if type(complete) is not bool:
        fail(f"{label} complete flag is not Boolean")
    final_counts, final_exact, final_effective = validate_state(
        native.get("final"),
        prime=prime,
        oracle_trace=oracle_trace,
        weber_f=weber_f,
        initial=initial,
        trace_cap=1,
        label=f"{label} final state",
        final=True,
    )
    mode = native.get("unique_mode")
    allowed_modes = (
        {
            "already_exact_singleton",
            "already_certified_singleton",
            "retained_schoof_fallback",
        }
        if expected_fallback
        else {
            "already_exact_singleton",
            "already_certified_singleton",
            "fresh_cap_one",
        }
    )
    if mode not in allowed_modes:
        fail(f"{label} has an invalid exact-completion mode")
    early_state = object_value(native.get("early"), f"{label} early state")
    final_state = object_value(native.get("final"), f"{label} final state")
    if mode == "already_exact_singleton":
        if candidate_count(prime, early_exact) != 1:
            fail(f"{label} already-exact mode lacks an early singleton")
        early_copy = dict(early_state)
        final_copy = dict(final_state)
        early_copy.pop("status", None)
        final_copy.pop("status", None)
        if early_copy != final_copy:
            fail(f"{label} already-exact states differ")
    elif mode == "already_certified_singleton":
        if (
            candidate_count(prime, early_exact) <= 1
            or candidate_count(prime, early_effective) != 1
        ):
            fail(f"{label} already-certified mode lacks a distinct singleton")
        early_copy = dict(early_state)
        final_copy = dict(final_state)
        early_copy.pop("status", None)
        final_copy.pop("status", None)
        if early_copy != final_copy:
            fail(f"{label} already-certified states differ")
    elif mode == "retained_schoof_fallback":
        early_levels = list_value(early_state.get("levels"), f"{label} early levels")
        final_levels = list_value(final_state.get("levels"), f"{label} final levels")
        early_fallback = list_value(
            early_state.get("fallback_levels"), f"{label} early fallback"
        )
        final_fallback = list_value(
            final_state.get("fallback_levels"), f"{label} final fallback"
        )
        if final_levels != early_levels or final_fallback[: len(early_fallback)] != early_fallback:
            fail(f"{label} retained fallback did not preserve the early state")
        appended = final_fallback[len(early_fallback) :]
        if candidate_count(prime, early_effective) <= 1 or not appended:
            fail(f"{label} retained fallback lacks a necessary continuation")
    elif list_value(
        early_state.get("fallback_levels"), f"{label} early fallback"
    ) or list_value(final_state.get("fallback_levels"), f"{label} final fallback"):
        fail(f"{label} fallback-off completion contains fallback levels")

    final_trace = native.get("final_exact_trace")
    scientifically_complete = (
        complete
        and candidate_count(prime, final_effective) == 1
        and final_state.get("traces") == [str(oracle_trace)]
        and final_trace is not None
        and decimal(final_trace, f"{label} final trace", signed=True) == oracle_trace
    )
    replay_exact_only = candidate_count(prime, final_exact) == 1
    if native.get("final_exact_only") is not (complete and replay_exact_only):
        fail(f"{label} final-exact-only flag disagrees with replay")
    if require_complete and not scientifically_complete:
        fail(f"{label} did not complete the required point count")
    if not require_complete:
        if complete != scientifically_complete:
            fail(f"{label} counterfactual completion flags disagree with replay")
        if not complete and final_trace is not None:
            fail(f"{label} incomplete counterfactual claims a final trace")
    if final_exact.modulus % early_exact.modulus:
        fail(f"{label} final exact state does not refine its early state")
    return final_counts


def validate_record(
    record: dict[str, Any], ordinal: int, configuration: dict[str, Any]
) -> dict[str, int]:
    if record.get("schema") != RECORD_SCHEMA or record.get("ordinal") != ordinal:
        fail(f"record {ordinal} has an unexpected schema or ordinal")
    if record.get("bucket_ordinal") != 0:
        fail(f"record {ordinal} has an unexpected bucket ordinal")
    prime_attempts = exact_int(
        record.get("prime_generation_attempts"),
        f"record {ordinal} prime-generation attempts",
    )
    if prime_attempts <= 0 or prime_attempts > configuration.get("max_prime_attempts"):
        fail(f"record {ordinal} prime-generation attempts are out of bounds")
    bits = exact_int(record.get("requested_bits"), f"record {ordinal} bit size")
    native = object_value(record.get("native"), f"record {ordinal} native result")
    prime = decimal(native.get("p"), f"record {ordinal} prime")
    if (
        prime.bit_length() != bits
        or prime < 5
        or prime % 2 == 0
        or not probably_prime(prime)
    ):
        fail(f"record {ordinal} modulus is not a prime of the requested size")
    oracle = object_value(record.get("oracle"), f"record {ordinal} oracle")
    curve_oracle = object_value(oracle.get("curve"), f"record {ordinal} curve oracle")
    twist_oracle = object_value(oracle.get("twist"), f"record {ordinal} twist oracle")
    trace = decimal(curve_oracle.get("trace"), f"record {ordinal} curve trace", signed=True)
    order = decimal(curve_oracle.get("order"), f"record {ordinal} curve order")
    twist_trace = decimal(
        twist_oracle.get("trace"), f"record {ordinal} twist trace", signed=True
    )
    twist_order = decimal(twist_oracle.get("order"), f"record {ordinal} twist order")
    if (
        order != prime + 1 - trace
        or twist_trace != -trace
        or twist_order != prime + 1 + trace
        or order + twist_order != 2 * prime + 2
        or abs(trace) > math.isqrt(4 * prime)
    ):
        fail(f"record {ordinal} Magma curve/twist identities disagree")
    expected_seed = decimal(configuration.get("seed"), "configuration seed")
    expected_index = decimal(
        configuration.get("start_index"), "configuration start index"
    ) + ordinal
    native_counts = validate_native(
        native,
        prime=prime,
        oracle_trace=trace,
        expected_seed=expected_seed,
        expected_index=expected_index,
        expected_max_level=configuration.get("max_level"),
        expected_trace_cap=configuration.get("trace_cap"),
        expected_sea_threads=configuration.get("sea_threads"),
        expected_fallback=True,
        max_generator_rejections=configuration.get("max_generator_rejections"),
        label=f"record {ordinal} native result",
        require_complete=True,
    )
    heuristic = object_value(
        record.get("heuristic_fallback_off"),
        f"record {ordinal} fallback-off result",
    )
    if heuristic.get("schoof_fallback") is not False:
        fail(f"record {ordinal} fallback-off result enabled fallback")
    if (
        heuristic.get("p") != native.get("p")
        or heuristic.get("seed") != native.get("seed")
        or heuristic.get("index") != native.get("index")
        or heuristic.get("curve") != native.get("curve")
        or heuristic.get("twist") != native.get("twist")
    ):
        fail(f"record {ordinal} fallback-off replay changed its inputs")
    validate_native(
        heuristic,
        prime=prime,
        oracle_trace=trace,
        expected_seed=expected_seed,
        expected_index=expected_index,
        expected_max_level=configuration.get("max_level"),
        expected_trace_cap=configuration.get("trace_cap"),
        expected_sea_threads=configuration.get("sea_threads"),
        expected_fallback=False,
        max_generator_rejections=configuration.get("max_generator_rejections"),
        label=f"record {ordinal} fallback-off result",
        require_complete=False,
    )
    return {
        "requested_bits": bits,
        "complete_native_counts": 1,
        "exact_elkies": native_counts["exact_elkies"],
        "certified_atkin": native_counts["certified_atkin"],
        "unconstrained": native_counts["unconstrained"],
        "exact_schoof": native_counts["exact_schoof"],
    }


def audit_artifact(root: Path) -> dict[str, Any]:
    root = root.resolve()
    result = load_json(root / "result.json", "retained result")
    if result.get("schema") != RESULT_SCHEMA:
        fail("retained result has an unexpected schema")
    raw = object_value(result.get("raw"), "retained raw identity")
    manifest_path = root / "raw" / "manifest.json"
    records_path = root / "raw" / "records.ndjson.gz"
    manifest_payload = read_bounded_file(
        manifest_path, "capture manifest", MAX_JSON_BYTES
    )
    manifest = load_json_bytes(manifest_payload, "capture manifest")
    if manifest.get("schema") != CORPUS_SCHEMA or manifest.get("status") != "complete":
        fail("capture manifest is not a completed Weber corpus")
    expected_manifest_bytes = exact_int(
        raw.get("manifest_bytes"), "capture manifest byte count"
    )
    if (
        len(manifest_payload) != expected_manifest_bytes
        or hashlib.sha256(manifest_payload).hexdigest()
        != raw.get("manifest_sha256")
    ):
        fail("capture manifest identity differs from retained result")
    expected_records_bytes = exact_int(raw.get("records_bytes"), "record byte count")
    expected_record_count = exact_int(raw.get("record_count"), "record count")
    expected_records_gzip_bytes = exact_int(
        raw.get("records_gzip_bytes"), "compressed record byte count"
    )
    if (
        expected_records_bytes < 0
        or expected_records_bytes > MAX_RETAINED_BYTES
        or expected_record_count < 0
        or expected_record_count > MAX_RETAINED_RECORDS
        or expected_records_gzip_bytes < 0
        or expected_records_gzip_bytes > MAX_RETAINED_BYTES
    ):
        fail("retained record bounds exceed the audit limits")
    compressed_records = read_bounded_file(
        records_path, "compressed record stream", MAX_RETAINED_BYTES
    )
    if (
        len(compressed_records) != expected_records_gzip_bytes
        or hashlib.sha256(compressed_records).hexdigest()
        != raw.get("records_gzip_sha256")
    ):
        fail("compressed record identity differs from retained result")

    records, decompressed_hash, decompressed_bytes = read_gzip_records(
        compressed_records, expected_record_count, expected_records_bytes
    )
    manifest_records = object_value(manifest.get("records"), "manifest records")
    if (
        decompressed_hash != raw.get("records_sha256")
        or decompressed_hash != manifest_records.get("sha256")
        or decompressed_bytes != expected_records_bytes
        or len(records) != expected_record_count
        or len(records) != manifest_records.get("count")
    ):
        fail("decompressed record identity differs from manifest/result")

    configuration = object_value(manifest.get("configuration"), "manifest configuration")
    coverage = object_value(result.get("coverage"), "retained coverage")
    capture = object_value(result.get("capture"), "retained capture")
    identity = object_value(manifest.get("identity"), "manifest identity")
    table_set = object_value(identity.get("table_set"), "manifest table set")
    magma_runtime = object_value(
        identity.get("magma_runtime"), "manifest Magma runtime"
    )
    if (
        capture.get("git_commit") != identity.get("git_commit")
        or capture.get("git_worktree_clean") is not True
        or identity.get("git_worktree_clean") is not True
        or identity.get("git_worktree_clean_at_start") is not True
        or identity.get("git_worktree_clean_at_completion") is not True
        or capture.get("provenance_mode")
        != "clean-commit-with-retained-capture-inputs"
        or capture.get("native_sha256") != identity.get("native_sha256")
        or capture.get("magma_runtime_sha256")
        != identity.get("magma_runtime_sha256")
        or capture.get("magma_version") != magma_runtime.get("version")
        or capture.get("table_set_sha256") != table_set.get("sha256")
    ):
        fail("retained capture summary differs from the source-bound manifest")
    validate_retained_inputs(root, result, identity, table_set)
    expected_bits = list_value(coverage.get("bit_sizes"), "coverage bit sizes")
    if any(type(bits) is not int or bits < 4 or bits > 4096 for bits in expected_bits):
        fail("coverage bit sizes are invalid")
    decimal(configuration.get("seed"), "configuration seed")
    decimal(configuration.get("start_index"), "configuration start index")
    for name in (
        "curves_per_size",
        "max_level",
        "trace_cap",
        "sea_threads",
        "max_prime_attempts",
        "max_generator_rejections",
    ):
        value = exact_int(configuration.get(name), f"configuration {name}")
        if value < 0:
            fail(f"configuration {name} is negative")
    if (
        configuration.get("bit_sizes") != expected_bits
        or configuration.get("curves_per_size") != coverage.get("curves_per_size")
        or coverage.get("curves_per_size") != 1
        or coverage.get("complete_point_count_required") is not True
        or configuration.get("max_level") != 401
        or configuration.get("trace_cap") != 16
        or configuration.get("schoof_fallback") is not True
    ):
        fail("manifest configuration differs from retained coverage contract")

    aggregate = {
        "complete_native_counts": 0,
        "exact_elkies": 0,
        "certified_atkin": 0,
        "unconstrained": 0,
        "exact_schoof": 0,
    }
    observed_bits: list[int] = []
    for ordinal, record in enumerate(records):
        counts = validate_record(record, ordinal, configuration)
        observed_bits.append(counts.pop("requested_bits"))
        for name, count in counts.items():
            aggregate[name] += count
    if observed_bits != expected_bits:
        fail("retained record bit-size coverage is incomplete or out of order")
    if aggregate != coverage.get("claim_counts"):
        fail("retained mathematical claim counts differ from the result summary")
    if aggregate["complete_native_counts"] != len(expected_bits):
        fail("not every requested bit size has a complete native/Magma count")
    return {
        "record_count": len(records),
        "bit_sizes": observed_bits,
        "claim_counts": aggregate,
        "records_sha256": decompressed_hash,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Authenticate and replay a retained Weber/Magma corpus"
    )
    parser.add_argument("artifact", type=Path)
    args = parser.parse_args(argv)
    try:
        summary = audit_artifact(args.artifact)
    except (OSError, RetainedCorpusError) as error:
        print(f"retained Weber corpus audit: {error}", file=sys.stderr)
        return 1
    print(json.dumps(summary, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
