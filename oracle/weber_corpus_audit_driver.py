#!/usr/bin/env python3
"""Execute a snapshotted production-Weber/Magma differential corpus."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys
from typing import Any

import audit_common
from audit_common import (
    AuditError,
    MAGMA_ENVIRONMENT_KEYS,
    MAX_OUTPUT_CAP_BYTES,
    MAX_U64,
    canonical_decimal,
    canonical_json,
    deterministic_prime,
    digest,
    directory_path,
    executable_dependency_identity,
    exact_integer,
    executable_path,
    git_identity,
    host_identity,
    integer_list,
    loaded_module_code_digest,
    magma_dependency_identity,
    magma_count_curve,
    magma_runtime_identity,
    nonnegative_integer,
    positive_integer,
    probably_prime,
    run_json,
    snapshot_file,
    source_module_code_digest,
    utc_now,
    verify_file_identities,
    write_manifest,
)


ROOT = Path(
    os.environ.get(
        "ONESHOTSEA_AUDIT_REPOSITORY_ROOT", Path(__file__).resolve().parents[1]
    )
).resolve()
POINT_COUNT_SCRIPT = Path(__file__).with_name("point_count.m")
PRIME_CHECK_SCRIPT = Path(__file__).with_name("prime_check.m")
ORIGINAL_BOOTSTRAP = Path(
    os.environ.get("ONESHOTSEA_WEBER_AUDIT_ORIGINAL_BOOTSTRAP", __file__)
).resolve()
ORIGINAL_DRIVER = Path(
    os.environ.get("ONESHOTSEA_WEBER_AUDIT_ORIGINAL_DRIVER", __file__)
).resolve()
ORIGINAL_COMMON = Path(
    os.environ.get("ONESHOTSEA_WEBER_AUDIT_ORIGINAL_COMMON", audit_common.__file__)
).resolve()
ORIGINAL_POINT_COUNT = Path(
    os.environ.get(
        "ONESHOTSEA_WEBER_AUDIT_ORIGINAL_POINT_COUNT", POINT_COUNT_SCRIPT
    )
).resolve()
ORIGINAL_PRIME_CHECK = Path(
    os.environ.get(
        "ONESHOTSEA_WEBER_AUDIT_ORIGINAL_PRIME_CHECK", PRIME_CHECK_SCRIPT
    )
).resolve()

SCHEMA = "oneshotsea.weber-oracle-corpus.v1"
RECORD_SCHEMA = "oneshotsea.weber-oracle-curve.v1"
NATIVE_SCHEMA = "oneshotsea.weber-audit.v1"
FALLBACK_LEVELS = (3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37)
MAX_TWIST_PARAMETER = 1_000_000
MAX_TRACE_CAP = 4096
MAX_GENERATOR_REJECTIONS = 4096
WEBER_SEARCH_DOMAIN = 0x5745424552464356
MASK64 = (1 << 64) - 1

TOP_LEVEL_KEYS = {
    "schema",
    "p",
    "seed",
    "index",
    "max_level",
    "trace_cap",
    "sea_threads",
    "schoof_fallback",
    "smoothness_audited",
    "rejected_samples",
    "weber_f",
    "j",
    "twist_parameter",
    "curve",
    "twist",
    "trace_prior",
    "early",
    "unique_mode",
    "final",
    "final_exact_trace",
    "final_exact_only",
    "complete",
}
STATE_KEYS = {
    "status",
    "exact_modulus",
    "constraint_modulus",
    "exact_trace_candidate_count",
    "trace_candidate_count",
    "exact_residue_classes",
    "effective_residue_classes",
    "compatible_source_lifts",
    "atkin_constraints",
    "levels",
    "fallback_levels",
    "trace_count",
    "traces",
}
LEVEL_KEYS = {
    "ell",
    "classification",
    "exact",
    "trace_residue",
    "exact_modulus",
    "constraint_modulus",
    "exact_trace_candidate_count",
    "trace_candidate_count",
    "atkin_projective_order",
    "atkin_residue_count",
    "compatible_source_lifts",
    "timings",
}
TIMING_KEYS = {
    "modular_root_workers",
    "modular_root_orbits",
    "modular_root_reused_lifts",
    "modular_root_orbit_reuse",
    "source_lifts_us",
    "modular_roots_us",
    "normalized_codomain_us",
    "bmss_us",
    "eigenvalue_us",
    "lift_pairs",
    "distinct_codomains",
    "codomain_cache_hits",
    "conjugate_eigenvalue_reuse",
    "eigenvalue_attempts",
    "independent_eigenvalue_recoveries",
    "conjugate_eigenvalues_derived",
}
FALLBACK_KEYS = {
    "ell",
    "classification",
    "trace_residue",
    "exact_modulus",
    "constraint_modulus",
    "exact_trace_candidate_count",
    "trace_candidate_count",
    "elapsed_us",
}


def require_bootstrap_context() -> str:
    required = (
        "ONESHOTSEA_WEBER_AUDIT_ORIGINAL_BOOTSTRAP",
        "ONESHOTSEA_WEBER_AUDIT_ORIGINAL_DRIVER",
        "ONESHOTSEA_WEBER_AUDIT_ORIGINAL_COMMON",
        "ONESHOTSEA_WEBER_AUDIT_ORIGINAL_POINT_COUNT",
        "ONESHOTSEA_WEBER_AUDIT_ORIGINAL_PRIME_CHECK",
        "ONESHOTSEA_WEBER_AUDIT_EXECUTION_SNAPSHOT_DIR",
        "ONESHOTSEA_WEBER_AUDIT_LOADED_BOOTSTRAP_CODE_SHA256",
    )
    missing = [name for name in required if not os.environ.get(name)]
    if missing:
        raise AuditError(
            "Weber corpus driver requires the pre-import bootstrap; missing "
            + ", ".join(missing)
        )
    snapshot_directory = Path(
        os.environ["ONESHOTSEA_WEBER_AUDIT_EXECUTION_SNAPSHOT_DIR"]
    ).resolve()
    if Path(__file__).resolve().parent != snapshot_directory:
        raise AuditError("Weber corpus driver is not executing from its bootstrap snapshot")
    for filename in (
        "weber_corpus_audit_driver.py",
        "audit_common.py",
        "point_count.m",
        "prime_check.m",
    ):
        if not (snapshot_directory / filename).is_file():
            raise AuditError(f"bootstrap execution snapshot is incomplete: {filename}")
    loaded_digest = os.environ[
        "ONESHOTSEA_WEBER_AUDIT_LOADED_BOOTSTRAP_CODE_SHA256"
    ]
    if (
        len(loaded_digest) != 64
        or any(character not in "0123456789abcdef" for character in loaded_digest)
        or source_module_code_digest(
            ORIGINAL_BOOTSTRAP, "oneshotsea_weber_bootstrap_probe"
        )
        != loaded_digest
    ):
        raise AuditError("loaded Weber corpus bootstrap differs from its source bytes")
    return loaded_digest


@dataclass(frozen=True)
class ConstraintState:
    modulus: int
    residues: tuple[int, ...]


def require_object(value: object, keys: set[str], label: str) -> dict[str, Any]:
    if type(value) is not dict or set(value) != keys:
        raise AuditError(f"{label} returned an unexpected schema")
    return value


def exact_boolean(value: object, label: str) -> bool:
    if type(value) is not bool:
        raise AuditError(f"{label} is not a JSON boolean")
    return value


def unsigned_decimal(value: object, label: str, *, maximum: int | None = None) -> int:
    result = canonical_decimal(value, label)
    if maximum is not None and result > maximum:
        raise AuditError(f"{label} is out of range")
    return result


def decimal_vector(
    value: object,
    label: str,
    *,
    modulus: int | None = None,
    signed: bool = False,
) -> tuple[int, ...]:
    if type(value) is not list:
        raise AuditError(f"{label} is not a JSON array")
    result = tuple(
        canonical_decimal(item, f"{label}[{index}]", signed=signed)
        for index, item in enumerate(value)
    )
    if tuple(sorted(set(result))) != result:
        raise AuditError(f"{label} must be strictly increasing and duplicate-free")
    if modulus is not None and any(item < 0 or item >= modulus for item in result):
        raise AuditError(f"{label} contains a noncanonical residue")
    return result


def refine(state: ConstraintState, ell: int, allowed: tuple[int, ...]) -> ConstraintState:
    if ell < 2 or math.gcd(state.modulus, ell) != 1:
        raise AuditError("constraint replay encountered a non-coprime modulus")
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
        first = residue + ((-radius - residue + state.modulus - 1) // state.modulus) * state.modulus
        if first <= radius:
            total += (radius - first) // state.modulus + 1
    return total


def enumerate_traces(
    prime: int, state: ConstraintState, cap: int
) -> tuple[int, ...] | None:
    count = candidate_count(prime, state)
    if count > cap:
        return None
    radius = math.isqrt(4 * prime)
    traces: list[int] = []
    for residue in state.residues:
        first = residue + ((-radius - residue + state.modulus - 1) // state.modulus) * state.modulus
        traces.extend(range(first, radius + 1, state.modulus))
    return tuple(sorted(traces))


def matrix_multiply(
    left: tuple[int, int, int, int],
    right: tuple[int, int, int, int],
    modulus: int,
) -> tuple[int, int, int, int]:
    a, b, c, d = left
    e, f, g, h = right
    return (
        (a * e + b * g) % modulus,
        (a * f + b * h) % modulus,
        (c * e + d * g) % modulus,
        (c * f + d * h) % modulus,
    )


def matrix_power(
    base: tuple[int, int, int, int], exponent: int, modulus: int
) -> tuple[int, int, int, int]:
    result = (1, 0, 0, 1)
    while exponent:
        if exponent & 1:
            result = matrix_multiply(result, base, modulus)
        exponent >>= 1
        if exponent:
            base = matrix_multiply(base, base, modulus)
    return result


def is_scalar(matrix: tuple[int, int, int, int]) -> bool:
    a, b, c, d = matrix
    return b == 0 and c == 0 and a == d


def projective_order(ell: int, prime: int, trace: int) -> int:
    if not probably_prime(ell) or ell < 3 or ell % 2 == 0 or prime % ell == 0:
        raise AuditError("invalid projective-order input")
    residue = trace % ell
    discriminant = (residue * residue - 4 * (prime % ell)) % ell
    character = 0 if discriminant == 0 else (1 if pow(discriminant, (ell - 1) // 2, ell) == 1 else -1)
    order = ell + 1 if character < 0 else (ell - 1 if character > 0 else ell)
    frobenius = (0, (-prime) % ell, 1, residue)
    if not is_scalar(matrix_power(frobenius, order, ell)):
        raise AuditError("projective-order bound failed during replay")
    divisor = 2
    remaining = order
    prime_divisors: list[int] = []
    while divisor * divisor <= remaining:
        if remaining % divisor == 0:
            prime_divisors.append(divisor)
            while remaining % divisor == 0:
                remaining //= divisor
        divisor += 1
    if remaining > 1:
        prime_divisors.append(remaining)
    for divisor in prime_divisors:
        while order % divisor == 0 and is_scalar(
            matrix_power(frobenius, order // divisor, ell)
        ):
            order //= divisor
    return order


def atkin_residues(ell: int, prime: int, order: int) -> tuple[int, ...]:
    if order < 2 or (ell + 1) % order:
        raise AuditError("invalid certified Atkin projective order")
    result: list[int] = []
    for trace in range(ell):
        discriminant = (trace * trace - 4 * (prime % ell)) % ell
        if discriminant and pow(discriminant, (ell - 1) // 2, ell) == ell - 1:
            if projective_order(ell, prime, trace) == order:
                result.append(trace)
    if not result:
        raise AuditError("certified Atkin order produced an empty residue set")
    return tuple(result)


def polynomial_trim(polynomial: list[int]) -> list[int]:
    while len(polynomial) > 1 and polynomial[-1] == 0:
        polynomial.pop()
    return polynomial


def polynomial_remainder(dividend: list[int], divisor: list[int], prime: int) -> list[int]:
    result = polynomial_trim([coefficient % prime for coefficient in dividend])
    divisor = polynomial_trim([coefficient % prime for coefficient in divisor])
    inverse = pow(divisor[-1], -1, prime)
    while len(result) >= len(divisor) and result != [0]:
        factor = result[-1] * inverse % prime
        shift = len(result) - len(divisor)
        for index, coefficient in enumerate(divisor):
            result[index + shift] = (result[index + shift] - factor * coefficient) % prime
        polynomial_trim(result)
    return result


def polynomial_multiply_mod(
    left: list[int], right: list[int], modulus: list[int], prime: int
) -> list[int]:
    product = [0] * (len(left) + len(right) - 1)
    for left_index, left_value in enumerate(left):
        for right_index, right_value in enumerate(right):
            product[left_index + right_index] = (
                product[left_index + right_index] + left_value * right_value
            ) % prime
    return polynomial_remainder(product, modulus, prime)


def polynomial_x_power(exponent: int, modulus: list[int], prime: int) -> list[int]:
    result = [1]
    base = [0, 1]
    while exponent:
        if exponent & 1:
            result = polynomial_multiply_mod(result, base, modulus, prime)
        exponent >>= 1
        if exponent:
            base = polynomial_multiply_mod(base, base, modulus, prime)
    return result


def polynomial_gcd(left: list[int], right: list[int], prime: int) -> list[int]:
    left = polynomial_trim([item % prime for item in left])
    right = polynomial_trim([item % prime for item in right])
    while right != [0]:
        left, right = right, polynomial_remainder(left, right, prime)
    inverse = pow(left[-1], -1, prime)
    return polynomial_trim([(item * inverse) % prime for item in left])


def rational_two_torsion_roots(prime: int, a: int, b: int) -> int:
    cubic = [b % prime, a % prime, 0, 1]
    x_to_p = polynomial_x_power(prime, cubic, prime)
    size = max(len(x_to_p), 2)
    difference = x_to_p + [0] * (size - len(x_to_p))
    difference[1] = (difference[1] - 1) % prime
    return len(polynomial_gcd(cubic, polynomial_trim(difference), prime)) - 1


def splitmix64(value: int) -> int:
    value = (value + 0x9E3779B97F4A7C15) & MASK64
    value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & MASK64
    return (value ^ (value >> 31)) & MASK64


def deterministic_residue(prime: int, seed: int, index: int, domain: int) -> int:
    limbs = max(1, (prime.bit_length() + 63) // 64)
    state = splitmix64(seed ^ splitmix64(index) ^ splitmix64(domain))
    value = 0
    for limb in range(limbs + 1):
        state = splitmix64(state ^ limb ^ domain)
        value = (value << 64) + state
    return value % prime


def divide_by_linear(polynomial: list[int], root: int, prime: int) -> list[int]:
    remainder = polynomial_trim([item % prime for item in polynomial])
    quotient = [0] * (len(remainder) - 1)
    for degree in range(len(remainder) - 1, 0, -1):
        coefficient = remainder[degree]
        quotient[degree - 1] = coefficient
        remainder[degree - 1] = (
            remainder[degree - 1] + coefficient * root
        ) % prime
    if remainder[0] != 0:
        raise AuditError("linear-factor removal failed during Montgomery replay")
    return polynomial_trim(quotient)


def polynomial_evaluate(polynomial: list[int], value: int, prime: int) -> int:
    result = 0
    for coefficient in reversed(polynomial):
        result = (result * value + coefficient) % prime
    return result


def has_montgomery_model_from_j(prime: int, j: int) -> bool:
    inverse_256 = pow(256, -1, prime)
    cubic = [
        (4 * j - 6912) * inverse_256 % prime,
        (6912 - j) * inverse_256 % prime,
        -9 % prime,
        1,
    ]
    x_to_p = polynomial_x_power(prime, cubic, prime)
    size = max(len(x_to_p), 2)
    difference = x_to_p + [0] * (size - len(x_to_p))
    difference[1] = (difference[1] - 1) % prime
    rational_roots = polynomial_gcd(cubic, polynomial_trim(difference), prime)
    if polynomial_evaluate(rational_roots, 4, prime) == 0:
        rational_roots = divide_by_linear(rational_roots, 4, prime)
    if len(rational_roots) <= 1:
        return False
    if polynomial_evaluate(rational_roots, 0, prime) == 0:
        return True
    square_test = polynomial_x_power(
        (prime - 1) // 2, rational_roots, prime
    )
    if not square_test:
        square_test = [0]
    square_test[0] = (square_test[0] - 1) % prime
    square_roots = polynomial_gcd(
        rational_roots, polynomial_trim(square_test), prime
    )
    return len(square_roots) > 1


def validate_deterministic_weber_sample(
    prime: int,
    seed: int,
    index: int,
    rejected_samples: int,
    emitted_f: int,
    emitted_j: int,
    maximum_rejections: int,
) -> None:
    if rejected_samples > maximum_rejections:
        raise AuditError(
            f"native generator exceeded the audit rejection cap of {maximum_rejections}"
        )
    for attempt in range(rejected_samples + 1):
        domain = splitmix64(WEBER_SEARCH_DOMAIN ^ attempt)
        candidate_f = deterministic_residue(prime, seed, index, domain)
        admitted = False
        candidate_j: int | None = None
        if candidate_f:
            power = pow(candidate_f, 24, prime)
            candidate_j = pow(power - 16, 3, prime) * pow(power, -1, prime) % prime
            admitted = (
                candidate_j not in {0, 1728 % prime}
                and has_montgomery_model_from_j(prime, candidate_j)
            )
        if attempt < rejected_samples and admitted:
            raise AuditError("native generator skipped an earlier admissible Weber sample")
        if attempt == rejected_samples and (
            not admitted or candidate_f != emitted_f or candidate_j != emitted_j
        ):
            raise AuditError("native generator output disagrees with deterministic replay")


def parse_curve(value: object, label: str, prime: int) -> dict[str, int]:
    curve = require_object(value, {"a", "b"}, label)
    result = {
        "a": unsigned_decimal(curve["a"], f"{label} a"),
        "b": unsigned_decimal(curve["b"], f"{label} b"),
    }
    if any(coefficient >= prime for coefficient in result.values()):
        raise AuditError(f"{label} returned noncanonical coefficients")
    if (4 * pow(result["a"], 3, prime) + 27 * pow(result["b"], 2, prime)) % prime == 0:
        raise AuditError(f"{label} is singular")
    return result


def curve_j(prime: int, curve: dict[str, int]) -> int:
    four_a3 = 4 * pow(curve["a"], 3, prime) % prime
    discriminant = (four_a3 + 27 * pow(curve["b"], 2, prime)) % prime
    return 1728 * four_a3 * pow(discriminant, -1, prime) % prime


def parse_atkin_constraints(
    value: object, label: str, prime: int, oracle_trace: int
) -> tuple[tuple[int, int, tuple[int, ...]], ...]:
    if type(value) is not list:
        raise AuditError(f"{label} is not a JSON array")
    result: list[tuple[int, int, tuple[int, ...]]] = []
    seen: set[int] = set()
    for index, item in enumerate(value):
        constraint = require_object(
            item, {"ell", "projective_order", "trace_residues"}, f"{label}[{index}]"
        )
        ell = exact_integer(constraint["ell"], f"{label}[{index}] ell")
        order = exact_integer(
            constraint["projective_order"], f"{label}[{index}] projective order"
        )
        residues_value = constraint["trace_residues"]
        if type(residues_value) is not list or any(type(item) is not int for item in residues_value):
            raise AuditError(f"{label}[{index}] trace residues are invalid")
        residues = tuple(residues_value)
        if ell in seen or tuple(sorted(set(residues))) != residues:
            raise AuditError(f"{label} contains duplicate or noncanonical constraints")
        if order != projective_order(ell, prime, oracle_trace):
            raise AuditError(f"{label}[{index}] projective order disagrees with Magma trace")
        if residues != atkin_residues(ell, prime, order):
            raise AuditError(f"{label}[{index}] does not contain the complete Atkin residue set")
        if oracle_trace % ell not in residues:
            raise AuditError(f"{label}[{index}] eliminates the Magma trace")
        seen.add(ell)
        result.append((ell, order, residues))
    return tuple(result)


def rebuild_effective(
    exact: ConstraintState,
    atkin: tuple[tuple[int, int, tuple[int, ...]], ...],
) -> ConstraintState:
    effective = exact
    for ell, _order, residues in atkin:
        if exact.modulus % ell:
            effective = refine(effective, ell, residues)
        elif any(residue % ell not in residues for residue in exact.residues):
            raise AuditError("exact fallback residue contradicts retained Atkin evidence")
    return effective


def validate_timings(value: object, label: str) -> None:
    timings = require_object(value, TIMING_KEYS, label)
    for key, item in timings.items():
        if key in {"modular_root_orbit_reuse", "conjugate_eigenvalue_reuse"}:
            exact_boolean(item, f"{label} {key}")
        else:
            unsigned_decimal(item, f"{label} {key}", maximum=MAX_U64)


def normalized_atkin(
    constraints: tuple[tuple[int, int, tuple[int, ...]], ...]
) -> list[dict[str, object]]:
    return [
        {"ell": ell, "projective_order": order, "trace_residues": list(residues)}
        for ell, order, residues in constraints
    ]


def validate_state(
    value: object,
    label: str,
    *,
    prime: int,
    oracle_trace: int,
    weber_f: int,
    initial: ConstraintState,
    available_levels: set[int],
    max_level: int,
    trace_cap: int,
    final: bool,
    production_early: bool = False,
) -> tuple[dict[str, Any], ConstraintState, ConstraintState]:
    state = require_object(value, STATE_KEYS, label)
    exact = initial
    effective = initial
    retained_atkin: tuple[tuple[int, int, tuple[int, ...]], ...] = ()
    normalized_levels: list[dict[str, Any]] = []
    levels_value = state["levels"]
    if type(levels_value) is not list:
        raise AuditError(f"{label} levels are not a JSON array")
    expected_table_levels = sorted(
        ell
        for ell in available_levels
        if ell <= max_level and prime % ell and initial.modulus % ell
    )
    previous_ell = 0
    for ordinal, item in enumerate(levels_value):
        level_label = f"{label} level {ordinal}"
        level = require_object(item, LEVEL_KEYS, level_label)
        ell = exact_integer(level["ell"], f"{level_label} ell")
        if (
            ell <= previous_ell
            or ell > max_level
            or ell not in available_levels
            or prime % ell == 0
        ):
            raise AuditError(f"{level_label} names an invalid or out-of-order table level")
        if production_early and (
            ordinal >= len(expected_table_levels)
            or ell != expected_table_levels[ordinal]
        ):
            raise AuditError(f"{level_label} is not the next production table level")
        completion_state = exact if trace_cap == 1 else effective
        if production_early and candidate_count(prime, completion_state) <= trace_cap:
            raise AuditError(f"{level_label} ran after the production trace cap was met")
        previous_ell = ell
        classification = level["classification"]
        if type(classification) is not str or classification not in {
            "exact_elkies",
            "certified_atkin",
            "unconstrained",
        }:
            raise AuditError(f"{level_label} has an invalid classification")
        is_exact = exact_boolean(level["exact"], f"{level_label} exact")
        trace_residue_value = level["trace_residue"]
        order_value = level["atkin_projective_order"]
        atkin_count = unsigned_decimal(
            level["atkin_residue_count"], f"{level_label} Atkin residue count"
        )
        oracle_discriminant = (
            (oracle_trace % ell) * (oracle_trace % ell) - 4 * (prime % ell)
        ) % ell
        discriminant_square = (
            oracle_discriminant == 0
            or pow(oracle_discriminant, (ell - 1) // 2, ell) == 1
        )
        expected_classification = (
            "exact_elkies"
            if discriminant_square
            else ("certified_atkin" if ell in {5, 7} else "unconstrained")
        )
        if classification != expected_classification:
            raise AuditError(
                f"{level_label} classification disagrees with the Magma trace"
            )
        if classification == "exact_elkies":
            if not is_exact or type(trace_residue_value) is not int or order_value is not None or atkin_count != 0:
                raise AuditError(f"{level_label} has inconsistent exact-Elkies metadata")
            if trace_residue_value != oracle_trace % ell or not 0 <= trace_residue_value < ell:
                raise AuditError(f"{level_label} exact residue disagrees with Magma")
            discriminant = (
                trace_residue_value * trace_residue_value - 4 * (prime % ell)
            ) % ell
            if discriminant and pow(discriminant, (ell - 1) // 2, ell) != 1:
                raise AuditError(
                    f"{level_label} exact-Elkies label has nonsquare Frobenius discriminant"
                )
            exact = refine(exact, ell, (trace_residue_value,))
            effective = refine(effective, ell, (trace_residue_value,))
        elif classification == "certified_atkin":
            if is_exact or trace_residue_value is not None or type(order_value) is not int:
                raise AuditError(f"{level_label} has inconsistent certified-Atkin metadata")
            order = projective_order(ell, prime, oracle_trace)
            residues = atkin_residues(ell, prime, order)
            if order_value != order or atkin_count != len(residues):
                raise AuditError(f"{level_label} certified-Atkin metadata disagrees with replay")
            effective = refine(effective, ell, residues)
            retained_atkin += ((ell, order, residues),)
        else:
            if is_exact or trace_residue_value is not None or order_value is not None or atkin_count != 0:
                raise AuditError(f"{level_label} has inconsistent unconstrained metadata")
        if unsigned_decimal(level["exact_modulus"], f"{level_label} exact modulus") != exact.modulus:
            raise AuditError(f"{level_label} exact modulus disagrees with replay")
        if unsigned_decimal(level["constraint_modulus"], f"{level_label} effective modulus") != effective.modulus:
            raise AuditError(f"{level_label} effective modulus disagrees with replay")
        if unsigned_decimal(level["exact_trace_candidate_count"], f"{level_label} exact count") != candidate_count(prime, exact):
            raise AuditError(f"{level_label} exact candidate count disagrees with replay")
        if unsigned_decimal(level["trace_candidate_count"], f"{level_label} effective count") != candidate_count(prime, effective):
            raise AuditError(f"{level_label} effective candidate count disagrees with replay")
        if unsigned_decimal(level["compatible_source_lifts"], f"{level_label} source-lift count") != 1:
            raise AuditError(f"{level_label} did not retain the bound Weber source lift")
        validate_timings(level["timings"], f"{level_label} timings")
        normalized_levels.append(
            {
                "ell": ell,
                "classification": classification,
                "exact": is_exact,
                "trace_residue": trace_residue_value,
                "exact_modulus": str(exact.modulus),
                "constraint_modulus": str(effective.modulus),
                "exact_trace_candidate_count": str(candidate_count(prime, exact)),
                "trace_candidate_count": str(candidate_count(prime, effective)),
                "atkin_projective_order": order_value,
                "atkin_residue_count": str(atkin_count),
                "compatible_source_lifts": "1",
            }
        )

    normalized_fallback: list[dict[str, Any]] = []
    fallback_value = state["fallback_levels"]
    if type(fallback_value) is not list:
        raise AuditError(f"{label} fallback levels are not a JSON array")
    if production_early and fallback_value:
        if len(levels_value) != len(expected_table_levels):
            raise AuditError(f"{label} entered fallback before exhausting Weber tables")
        completion_state = exact if trace_cap == 1 else effective
        if candidate_count(prime, completion_state) <= trace_cap:
            raise AuditError(f"{label} entered fallback after the production trace cap was met")
    fallback_cursor = 0
    for ordinal, item in enumerate(fallback_value):
        fallback_label = f"{label} fallback level {ordinal}"
        level = require_object(item, FALLBACK_KEYS, fallback_label)
        while fallback_cursor < len(FALLBACK_LEVELS) and exact.modulus % FALLBACK_LEVELS[fallback_cursor] == 0:
            fallback_cursor += 1
        if fallback_cursor == len(FALLBACK_LEVELS):
            raise AuditError(f"{fallback_label} extends the fixed fallback schedule")
        ell = exact_integer(level["ell"], f"{fallback_label} ell")
        if (
            ell != FALLBACK_LEVELS[fallback_cursor]
            or type(level["classification"]) is not str
            or level["classification"] != "exact_schoof"
        ):
            raise AuditError(f"{fallback_label} violates the fixed fallback schedule")
        completion_state = exact if trace_cap == 1 else effective
        if production_early and candidate_count(prime, completion_state) <= trace_cap:
            raise AuditError(f"{fallback_label} ran after the production trace cap was met")
        residue = exact_integer(level["trace_residue"], f"{fallback_label} residue")
        if residue != oracle_trace % ell:
            raise AuditError(f"{fallback_label} residue disagrees with Magma")
        exact = refine(exact, ell, (residue,))
        effective = rebuild_effective(exact, retained_atkin)
        if unsigned_decimal(level["exact_modulus"], f"{fallback_label} exact modulus") != exact.modulus:
            raise AuditError(f"{fallback_label} exact modulus disagrees with replay")
        if unsigned_decimal(level["constraint_modulus"], f"{fallback_label} effective modulus") != effective.modulus:
            raise AuditError(f"{fallback_label} effective modulus disagrees with replay")
        if unsigned_decimal(level["exact_trace_candidate_count"], f"{fallback_label} exact count") != candidate_count(prime, exact):
            raise AuditError(f"{fallback_label} exact count disagrees with replay")
        if unsigned_decimal(level["trace_candidate_count"], f"{fallback_label} effective count") != candidate_count(prime, effective):
            raise AuditError(f"{fallback_label} effective count disagrees with replay")
        unsigned_decimal(level["elapsed_us"], f"{fallback_label} elapsed time", maximum=MAX_U64)
        normalized_fallback.append(
            {
                "ell": ell,
                "classification": "exact_schoof",
                "trace_residue": residue,
                "exact_modulus": str(exact.modulus),
                "constraint_modulus": str(effective.modulus),
                "exact_trace_candidate_count": str(candidate_count(prime, exact)),
                "trace_candidate_count": str(candidate_count(prime, effective)),
            }
        )
        fallback_cursor += 1

    if production_early:
        completion_state = exact if trace_cap == 1 else effective
        if candidate_count(prime, completion_state) > trace_cap:
            if len(levels_value) != len(expected_table_levels):
                raise AuditError(f"{label} stopped before exhausting Weber tables")
            next_fallback = next(
                (ell for ell in FALLBACK_LEVELS if exact.modulus % ell), None
            )
            if next_fallback is not None:
                raise AuditError(f"{label} stopped before exhausting exact fallback")

    exact_modulus = unsigned_decimal(state["exact_modulus"], f"{label} exact modulus")
    effective_modulus = unsigned_decimal(state["constraint_modulus"], f"{label} effective modulus")
    exact_residues = decimal_vector(
        state["exact_residue_classes"], f"{label} exact residues", modulus=exact_modulus
    )
    effective_residues = decimal_vector(
        state["effective_residue_classes"],
        f"{label} effective residues",
        modulus=effective_modulus,
    )
    if (exact_modulus, exact_residues) != (exact.modulus, exact.residues):
        raise AuditError(f"{label} exact state disagrees with level replay")
    if (effective_modulus, effective_residues) != (effective.modulus, effective.residues):
        raise AuditError(f"{label} effective state disagrees with level replay")
    exact_count = candidate_count(prime, exact)
    effective_count = candidate_count(prime, effective)
    if unsigned_decimal(state["exact_trace_candidate_count"], f"{label} exact count") != exact_count:
        raise AuditError(f"{label} exact candidate count disagrees with replay")
    if unsigned_decimal(state["trace_candidate_count"], f"{label} effective count") != effective_count:
        raise AuditError(f"{label} effective candidate count disagrees with replay")
    source_lifts = decimal_vector(
        state["compatible_source_lifts"], f"{label} compatible source lifts", modulus=prime
    )
    if source_lifts != (weber_f,):
        raise AuditError(f"{label} did not retain exactly the bound Weber source lift")
    emitted_atkin = parse_atkin_constraints(
        state["atkin_constraints"], f"{label} Atkin constraints", prime, oracle_trace
    )
    if emitted_atkin != retained_atkin:
        raise AuditError(f"{label} retained Atkin constraints disagree with replay")
    if oracle_trace % exact.modulus not in exact.residues or oracle_trace % effective.modulus not in effective.residues:
        raise AuditError(f"{label} constraints eliminate the Magma trace")

    expected_traces = enumerate_traces(
        prime,
        exact if final or trace_cap == 1 else effective,
        1 if final else trace_cap,
    )
    traces_value = state["traces"]
    trace_count_value = state["trace_count"]
    if traces_value is None:
        if trace_count_value is not None or expected_traces is not None:
            raise AuditError(f"{label} has inconsistent or incomplete trace enumeration")
        traces: tuple[int, ...] | None = None
    else:
        traces = decimal_vector(traces_value, f"{label} traces", signed=True)
        if expected_traces != traces:
            raise AuditError(f"{label} trace enumeration disagrees with replay")
        if unsigned_decimal(trace_count_value, f"{label} trace count") != len(traces):
            raise AuditError(f"{label} trace count disagrees with its trace list")
        if oracle_trace not in traces:
            raise AuditError(f"{label} trace enumeration omits the Magma trace")

    status = state["status"]
    expected_status = "complete" if final and traces is not None else (
        "trace_set_enumerated" if traces is not None else "level_limit"
    )
    if type(status) is not str or status != expected_status:
        raise AuditError(f"{label} status disagrees with its replayed state")
    normalized = {
        "status": status,
        "exact_modulus": str(exact.modulus),
        "constraint_modulus": str(effective.modulus),
        "exact_trace_candidate_count": str(exact_count),
        "trace_candidate_count": str(effective_count),
        "exact_residue_classes": [str(item) for item in exact.residues],
        "effective_residue_classes": [str(item) for item in effective.residues],
        "compatible_source_lifts": [str(weber_f)],
        "atkin_constraints": normalized_atkin(retained_atkin),
        "levels": normalized_levels,
        "fallback_levels": normalized_fallback,
        "trace_count": None if traces is None else str(len(traces)),
        "traces": None if traces is None else [str(item) for item in traces],
    }
    return normalized, exact, effective


def validate_native_record(
    value: object,
    *,
    prime: int,
    seed: int,
    index: int,
    max_level: int,
    trace_cap: int,
    sea_threads: int,
    available_levels: set[int],
    curve_oracle: dict[str, int],
    twist_oracle: dict[str, int],
    max_generator_rejections: int = MAX_GENERATOR_REJECTIONS,
) -> dict[str, Any]:
    record = require_object(value, TOP_LEVEL_KEYS, "native Weber audit")
    if record["schema"] != NATIVE_SCHEMA:
        raise AuditError("native Weber audit returned an unexpected schema version")
    echoed = (
        unsigned_decimal(record["p"], "native p"),
        unsigned_decimal(record["seed"], "native seed", maximum=MAX_U64),
        unsigned_decimal(record["index"], "native index", maximum=MAX_U64),
        unsigned_decimal(record["max_level"], "native max level", maximum=MAX_U64),
        unsigned_decimal(record["trace_cap"], "native trace cap"),
        unsigned_decimal(record["sea_threads"], "native SEA threads"),
    )
    if echoed != (prime, seed, index, max_level, trace_cap, sea_threads):
        raise AuditError("native Weber audit returned mismatched inputs")
    if not exact_boolean(record["schoof_fallback"], "native fallback flag"):
        raise AuditError("native Weber audit did not enable exact Schoof fallback")
    if exact_boolean(record["smoothness_audited"], "native smoothness-audited flag"):
        raise AuditError("native Weber audit unexpectedly claims smoothness coverage")
    rejected = unsigned_decimal(
        record["rejected_samples"], "native rejected samples", maximum=MAX_U64
    )
    weber_f = unsigned_decimal(record["weber_f"], "native Weber f")
    j = unsigned_decimal(record["j"], "native j")
    twist_parameter = unsigned_decimal(
        record["twist_parameter"], "native twist parameter"
    )
    if not (0 < weber_f < prime and 0 <= j < prime and 2 <= twist_parameter < prime):
        raise AuditError("native Weber curve metadata is noncanonical")
    if j in {0, 1728 % prime}:
        raise AuditError("native Weber curve used an exceptional j-invariant")
    f24 = pow(weber_f, 24, prime)
    if pow(f24, -1, prime) * pow((f24 - 16) % prime, 3, prime) % prime != j:
        raise AuditError("native Weber source lift does not map to the emitted j-invariant")
    validate_deterministic_weber_sample(
        prime,
        seed,
        index,
        rejected,
        weber_f,
        j,
        max_generator_rejections,
    )
    curve = parse_curve(record["curve"], "native curve", prime)
    twist = parse_curve(record["twist"], "native twist", prime)
    k = j * pow((1728 - j) % prime, -1, prime) % prime
    if curve != {"a": 3 * k % prime, "b": 2 * k % prime}:
        raise AuditError("native curve is not the canonical production model for its j")
    if twist_parameter > MAX_TWIST_PARAMETER:
        raise AuditError("native twist parameter exceeds the bounded independent audit limit")
    least_nonsquare = next(
        (
            candidate
            for candidate in range(2, twist_parameter + 1)
            if pow(candidate, (prime - 1) // 2, prime) == prime - 1
        ),
        None,
    )
    if least_nonsquare != twist_parameter:
        raise AuditError("native twist parameter is not the least quadratic nonsquare")
    expected_twist = {
        "a": curve["a"] * pow(twist_parameter, 2, prime) % prime,
        "b": curve["b"] * pow(twist_parameter, 3, prime) % prime,
    }
    if twist != expected_twist:
        raise AuditError("native twist coefficients do not match the declared parameter")
    if curve_j(prime, curve) != j or curve_j(prime, twist) != j:
        raise AuditError("native curve or twist has an inconsistent j-invariant")

    curve_trace = curve_oracle["trace"]
    twist_trace = twist_oracle["trace"]
    if curve_oracle["order"] + twist_oracle["order"] != 2 * prime + 2:
        raise AuditError("Magma curve/twist orders do not sum to 2p+2")
    if twist_trace != -curve_trace:
        raise AuditError("Magma curve/twist traces are not negatives")
    prior_value = record["trace_prior"]
    root_count = rational_two_torsion_roots(prime, curve["a"], curve["b"])
    if prior_value is None:
        if root_count == 3:
            raise AuditError("native Weber audit omitted the full-rational-2-torsion prior")
        initial = ConstraintState(1, (0,))
        normalized_prior = None
    else:
        prior = require_object(prior_value, {"modulus", "residue"}, "native trace prior")
        modulus = unsigned_decimal(prior["modulus"], "native trace-prior modulus")
        residue = unsigned_decimal(prior["residue"], "native trace-prior residue")
        expected_residue = (prime + 1) % 4
        if root_count != 3 or (modulus, residue) != (4, expected_residue):
            raise AuditError("native trace prior lacks the required full rational 2-torsion provenance")
        if curve_trace % modulus != residue:
            raise AuditError("native trace prior disagrees with the Magma trace")
        initial = ConstraintState(modulus, (residue,))
        normalized_prior = {"modulus": str(modulus), "residue": str(residue)}

    early, early_exact, _early_effective = validate_state(
        record["early"],
        "native early state",
        prime=prime,
        oracle_trace=curve_trace,
        weber_f=weber_f,
        initial=initial,
        available_levels=available_levels,
        max_level=max_level,
        trace_cap=trace_cap,
        final=False,
        production_early=True,
    )
    final, final_exact, _final_effective = validate_state(
        record["final"],
        "native final state",
        prime=prime,
        oracle_trace=curve_trace,
        weber_f=weber_f,
        initial=initial,
        available_levels=available_levels,
        max_level=max_level,
        trace_cap=trace_cap,
        final=True,
    )
    mode = record["unique_mode"]
    if type(mode) is not str or mode not in {
        "already_exact_singleton",
        "retained_schoof_fallback",
    }:
        raise AuditError("native Weber audit returned an invalid unique mode")
    if mode == "already_exact_singleton":
        if int(early["exact_trace_candidate_count"]) != 1:
            raise AuditError("already-exact mode lacks an early exact singleton")
        comparable_early = dict(early)
        comparable_final = dict(final)
        comparable_early.pop("status")
        comparable_final.pop("status")
        if comparable_early != comparable_final:
            raise AuditError("already-exact native states are not identical")
    if mode == "retained_schoof_fallback":
        if final["levels"] != early["levels"] or final["fallback_levels"][: len(early["fallback_levels"])] != early["fallback_levels"]:
            raise AuditError("retained fallback did not preserve the early production state")
        appended_fallback = final["fallback_levels"][len(early["fallback_levels"]):]
        if int(early["exact_trace_candidate_count"]) <= 1 or not appended_fallback:
            raise AuditError("retained fallback lacks a necessary cap-one continuation")
        if any(
            int(level["exact_trace_candidate_count"]) <= 1
            for level in appended_fallback[:-1]
        ) or int(appended_fallback[-1]["exact_trace_candidate_count"]) != 1:
            raise AuditError("retained fallback did not stop at the first exact singleton")
    complete = exact_boolean(record["complete"], "native complete flag")
    final_exact_only = exact_boolean(
        record["final_exact_only"], "native final-exact-only flag"
    )
    final_trace = canonical_decimal(
        record["final_exact_trace"], "native final exact trace", signed=True
    )
    if (
        not complete
        or not final_exact_only
        or candidate_count(prime, final_exact) != 1
        or final["traces"] != [str(curve_trace)]
        or final_trace != curve_trace
    ):
        raise AuditError("native final state is not the exact Magma-trace singleton")
    if early_exact.modulus > final_exact.modulus or final_exact.modulus % early_exact.modulus:
        raise AuditError("native final exact state does not refine the early state")
    return {
        "schema": NATIVE_SCHEMA,
        "p": str(prime),
        "seed": str(seed),
        "index": str(index),
        "max_level": str(max_level),
        "trace_cap": str(trace_cap),
        "sea_threads": str(sea_threads),
        "schoof_fallback": True,
        "smoothness_audited": False,
        "rejected_samples": str(rejected),
        "weber_f": str(weber_f),
        "j": str(j),
        "twist_parameter": str(twist_parameter),
        "curve": {key: str(value) for key, value in curve.items()},
        "twist": {key: str(value) for key, value in twist.items()},
        "trace_prior": normalized_prior,
        "early": early,
        "unique_mode": mode,
        "final": final,
        "final_exact_trace": str(final_trace),
        "final_exact_only": True,
        "complete": True,
    }


def table_files(root: Path) -> list[Path]:
    files: list[Path] = []
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise AuditError(f"table tree contains a symbolic link: {path}")
        if path.is_file():
            files.append(path)
        elif not path.is_dir():
            raise AuditError(f"table tree contains an unsupported entry: {path}")
    if not files:
        raise AuditError(f"table tree is empty: {root}")
    return files


def snapshot_tables(
    source_weber: Path,
    destination_parent: Path,
    tracked_inputs: dict[str, tuple[Path, str]],
) -> tuple[Path, dict[str, Any], set[int]]:
    source_parent = source_weber.parent
    source_j = source_parent / "j"
    if not source_j.is_dir():
        raise AuditError(f"classical-j table directory is missing: {source_j}")
    destination_parent.mkdir()
    identities: list[dict[str, Any]] = []
    for name, source_root in (("weber_f", source_weber), ("j", source_j)):
        destination_root = destination_parent / name
        destination_root.mkdir()
        for source in table_files(source_root):
            relative = source.relative_to(source_root)
            destination = destination_root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            snapshot_file(source, destination)
            source_hash = digest(source)
            destination_hash = digest(destination)
            tracked_inputs[f"original {name} table {relative}"] = (source, source_hash)
            tracked_inputs[f"snapshot {name} table {relative}"] = (
                destination,
                destination_hash,
            )
            identities.append(
                {
                    "path": f"{name}/{relative.as_posix()}",
                    "sha256": destination_hash,
                    "bytes": destination.stat().st_size,
                }
            )
    table_digest = hashlib.sha256(canonical_json(identities).encode()).hexdigest()
    available: set[int] = set()
    for path in (destination_parent / "weber_f").glob("phi_*.txt"):
        match = re.fullmatch(r"phi_([0-9]+)\.txt", path.name)
        if match:
            ell = int(match.group(1), 10)
            if ell >= 5 and ell % 2 and probably_prime(ell) and ell % 3:
                available.add(ell)
    return destination_parent / "weber_f", {
        "sha256": table_digest,
        "files": identities,
    }, available


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Stream a deterministic production-Weber/Magma differential corpus"
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--native", default=str(ROOT / "build" / "oracle_weber_audit")
    )
    parser.add_argument(
        "--table-dir", default=str(ROOT / "data" / "modpoly" / "weber_f")
    )
    parser.add_argument("--magma-runtime", required=True)
    parser.add_argument("--magma-root", required=True)
    parser.add_argument("--seed", type=nonnegative_integer, required=True)
    parser.add_argument("--bit-sizes", default="16,24,32,48,64,96,128,192,256")
    parser.add_argument("--curves-per-size", type=positive_integer, default=1)
    parser.add_argument("--start-index", type=nonnegative_integer, default=0)
    parser.add_argument("--max-level", type=positive_integer, default=193)
    parser.add_argument("--trace-cap", type=positive_integer, default=16)
    parser.add_argument("--sea-threads", type=nonnegative_integer, default=1)
    parser.add_argument("--command-timeout-seconds", type=positive_integer, default=3600)
    parser.add_argument("--max-output-bytes", type=positive_integer, default=8 * 1024 * 1024)
    parser.add_argument("--max-prime-attempts", type=positive_integer, default=1_000_000)
    parser.add_argument(
        "--max-generator-rejections",
        type=positive_integer,
        default=MAX_GENERATOR_REJECTIONS,
    )
    args = parser.parse_args(argv)
    args.bit_sizes = integer_list(args.bit_sizes, minimum=4, label="bit-sizes")
    total_curves = len(args.bit_sizes) * args.curves_per_size
    if args.seed > MAX_U64 or args.start_index > MAX_U64:
        parser.error("seed and start-index must not exceed UINT64_MAX")
    if total_curves > MAX_U64 - args.start_index + 1:
        parser.error("the curve-index range exceeds UINT64_MAX")
    if args.max_level < 5 or args.max_level > (1 << 32) - 1:
        parser.error("max-level must be between 5 and UINT32_MAX")
    if args.trace_cap > MAX_TRACE_CAP:
        parser.error(f"trace-cap may not exceed {MAX_TRACE_CAP}")
    if args.sea_threads > MAX_U64:
        parser.error("sea-threads must not exceed UINT64_MAX")
    if args.max_generator_rejections > MAX_GENERATOR_REJECTIONS:
        parser.error(
            f"max-generator-rejections may not exceed {MAX_GENERATOR_REJECTIONS}"
        )
    if args.max_output_bytes > MAX_OUTPUT_CAP_BYTES:
        parser.error("max-output-bytes may not exceed 67108864")
    if any(bits > 4096 for bits in args.bit_sizes):
        parser.error("bit-sizes may not exceed 4096")
    return args


def audit(args: argparse.Namespace, invocation: list[str]) -> dict[str, Any]:
    source = git_identity()
    output = args.output_dir.expanduser().resolve()
    output.mkdir(parents=True, exist_ok=False)
    manifest_path = output / "manifest.json"
    records_path = output / "records.ndjson"
    base: dict[str, Any] = {
        "schema": SCHEMA,
        "status": "running",
        "started_utc": utc_now(),
        "invocation_argv": invocation,
        "configuration": {
            "seed": str(args.seed),
            "bit_sizes": list(args.bit_sizes),
            "curves_per_size": args.curves_per_size,
            "start_index": str(args.start_index),
            "max_level": args.max_level,
            "trace_cap": args.trace_cap,
            "sea_threads": args.sea_threads,
            "schoof_fallback": True,
            "smoothness_audited": False,
            "command_timeout_seconds": args.command_timeout_seconds,
            "max_output_bytes": args.max_output_bytes,
            "max_prime_attempts": args.max_prime_attempts,
            "max_generator_rejections": args.max_generator_rejections,
        },
    }
    write_manifest(manifest_path, base)
    tracked_inputs: dict[str, tuple[Path, str]] = {}
    native_dependencies_start: dict[str, Any] | None = None
    magma_dependencies_start: dict[str, Any] | None = None
    native_for_identity: Path | None = None
    magma_for_identity: Path | None = None
    magma_root_for_identity: Path | None = None
    try:
        native_source = executable_path(args.native, "native Weber audit")
        table_source = directory_path(args.table_dir, "Weber table")
        magma = executable_path(args.magma_runtime, "Magma runtime")
        magma_root = directory_path(args.magma_root, "Magma root")
        magma_system_spec = magma_root / "package" / "spec"
        if not magma_system_spec.is_file():
            raise AuditError(f"Magma system spec is missing: {magma_system_spec}")
        driver_source = Path(__file__).resolve()
        common_source = Path(audit_common.__file__).resolve()
        source_inputs = {
            "native source": native_source,
            "Magma runtime executable": magma,
            "Magma system spec": magma_system_spec,
            "original Weber corpus bootstrap": ORIGINAL_BOOTSTRAP,
            "original Weber corpus driver": ORIGINAL_DRIVER,
            "original audit common": ORIGINAL_COMMON,
            "original point-count script": ORIGINAL_POINT_COUNT,
            "original prime-check script": ORIGINAL_PRIME_CHECK,
            "executing Weber corpus driver": driver_source,
            "executing audit common": common_source,
            "executing point-count script": POINT_COUNT_SCRIPT,
            "executing prime-check script": PRIME_CHECK_SCRIPT,
        }
        tracked_inputs = {
            label: (path, digest(path)) for label, path in source_inputs.items()
        }
        for original, executing, label in (
            (ORIGINAL_DRIVER, driver_source, "Weber corpus driver"),
            (ORIGINAL_COMMON, common_source, "audit common"),
            (ORIGINAL_POINT_COUNT, POINT_COUNT_SCRIPT, "point-count script"),
            (ORIGINAL_PRIME_CHECK, PRIME_CHECK_SCRIPT, "prime-check script"),
        ):
            if digest(original) != digest(executing):
                raise AuditError(f"executing {label} differs from its source snapshot")
        inputs_directory = output / "inputs"
        inputs_directory.mkdir()
        native = snapshot_file(native_source, inputs_directory / "oracle_weber_audit")
        point_count_script = snapshot_file(
            POINT_COUNT_SCRIPT, inputs_directory / "point_count.m"
        )
        prime_check_script = snapshot_file(
            PRIME_CHECK_SCRIPT, inputs_directory / "prime_check.m"
        )
        bootstrap_snapshot = snapshot_file(
            ORIGINAL_BOOTSTRAP, inputs_directory / "weber_corpus_audit.py"
        )
        driver_snapshot = snapshot_file(
            driver_source, inputs_directory / "weber_corpus_audit_driver.py"
        )
        common_snapshot = snapshot_file(
            common_source, inputs_directory / "audit_common.py"
        )
        for label, path in {
            "native snapshot": native,
            "point-count script snapshot": point_count_script,
            "prime-check script snapshot": prime_check_script,
            "Weber corpus bootstrap snapshot": bootstrap_snapshot,
            "Weber corpus driver snapshot": driver_snapshot,
            "audit common snapshot": common_snapshot,
        }.items():
            tracked_inputs[label] = (path, digest(path))
        table_snapshot, table_identity, available_levels = snapshot_tables(
            table_source, inputs_directory / "modpoly", tracked_inputs
        )
        native_for_identity = native
        magma_for_identity = magma
        magma_root_for_identity = magma_root
        native_dependencies_start = executable_dependency_identity(native)
        magma_dependencies_start = magma_dependency_identity(magma_root, magma)
        magma_version_identity = magma_runtime_identity(
            magma,
            magma_root,
            timeout_seconds=args.command_timeout_seconds,
            max_output_bytes=args.max_output_bytes,
        )
        base["identity"] = {
            "git_commit": source["commit"],
            "git_worktree_clean": source["worktree_clean"],
            "git_worktree_clean_at_start": source["worktree_clean"],
            "native_source_path": str(native_source),
            "native_path": str(native),
            "native_sha256": digest(native),
            "native_dynamic_dependencies": native_dependencies_start,
            "table_source_path": str(table_source),
            "table_snapshot_path": str(table_snapshot),
            "table_set": table_identity,
            "magma_root": str(magma_root),
            "magma_runtime_path": str(magma),
            "magma_runtime_sha256": digest(magma),
            "magma_system_spec_sha256": digest(magma_system_spec),
            "magma_runtime": magma_version_identity,
            "magma_dependencies": magma_dependencies_start,
            "magma_environment_policy": {
                "cleared": list(MAGMA_ENVIRONMENT_KEYS),
                "mode": "direct-runtime-controlled-root",
                "startup_file": os.devnull,
                "numeric_library_threads": "1",
            },
            "loaded_corpus_code_sha256": loaded_module_code_digest(sys.modules[__name__]),
            "loaded_bootstrap_code_sha256": os.environ[
                "ONESHOTSEA_WEBER_AUDIT_LOADED_BOOTSTRAP_CODE_SHA256"
            ],
            "loaded_audit_common_code_sha256": loaded_module_code_digest(audit_common),
            "point_count_script_sha256": digest(point_count_script),
            "prime_check_script_sha256": digest(prime_check_script),
            "corpus_bootstrap_sha256": digest(bootstrap_snapshot),
            "corpus_driver_sha256": digest(driver_snapshot),
            "audit_common_original_sha256": digest(ORIGINAL_COMMON),
            "audit_common_executing_sha256": digest(common_source),
            "audit_common_artifact_sha256": digest(common_snapshot),
            "nondeterministic_native_timing_fields_archived": False,
            "snapshots_directory": inputs_directory.name,
            "host": host_identity(),
        }
        write_manifest(manifest_path, base)
        count = 0
        curve_cursor: int | None = args.start_index
        with records_path.open("x", encoding="utf-8") as stream:
            for bits in args.bit_sizes:
                for bucket_ordinal in range(args.curves_per_size):
                    if curve_cursor is None:
                        raise AuditError(
                            "curve-index space exhausted before the requested corpus completed"
                        )
                    prime, prime_attempts = deterministic_prime(
                        magma,
                        magma_root,
                        prime_check_script,
                        args.seed,
                        bits,
                        bucket_ordinal,
                        domain=SCHEMA,
                        timeout_seconds=args.command_timeout_seconds,
                        max_output_bytes=args.max_output_bytes,
                        max_attempts=args.max_prime_attempts,
                    )
                    native_record = run_json(
                        [
                            str(native),
                            "--p",
                            str(prime),
                            "--seed",
                            str(args.seed),
                            "--range-start",
                            str(curve_cursor),
                            "--count",
                            "1",
                            "--max-level",
                            str(args.max_level),
                            "--trace-cap",
                            str(args.trace_cap),
                            "--sea-threads",
                            str(args.sea_threads),
                            "--table-dir",
                            str(table_snapshot),
                            "--schoof-fallback",
                            "1",
                        ],
                        "native production Weber audit",
                        timeout_seconds=args.command_timeout_seconds,
                        max_output_bytes=args.max_output_bytes,
                    )
                    curve_value = require_object(
                        native_record.get("curve"), {"a", "b"}, "native curve"
                    )
                    twist_value = require_object(
                        native_record.get("twist"), {"a", "b"}, "native twist"
                    )
                    curve_a = unsigned_decimal(curve_value["a"], "native curve a")
                    curve_b = unsigned_decimal(curve_value["b"], "native curve b")
                    twist_a = unsigned_decimal(twist_value["a"], "native twist a")
                    twist_b = unsigned_decimal(twist_value["b"], "native twist b")
                    curve_oracle = magma_count_curve(
                        magma,
                        magma_root,
                        point_count_script,
                        prime,
                        curve_a,
                        curve_b,
                        timeout_seconds=args.command_timeout_seconds,
                        max_output_bytes=args.max_output_bytes,
                    )
                    twist_oracle = magma_count_curve(
                        magma,
                        magma_root,
                        point_count_script,
                        prime,
                        twist_a,
                        twist_b,
                        timeout_seconds=args.command_timeout_seconds,
                        max_output_bytes=args.max_output_bytes,
                    )
                    normalized_native = validate_native_record(
                        native_record,
                        prime=prime,
                        seed=args.seed,
                        index=curve_cursor,
                        max_level=args.max_level,
                        trace_cap=args.trace_cap,
                        sea_threads=args.sea_threads,
                        available_levels=available_levels,
                        curve_oracle=curve_oracle,
                        twist_oracle=twist_oracle,
                        max_generator_rejections=args.max_generator_rejections,
                    )
                    record = {
                        "schema": RECORD_SCHEMA,
                        "ordinal": count,
                        "bucket_ordinal": bucket_ordinal,
                        "requested_bits": bits,
                        "prime_generation_attempts": prime_attempts,
                        "native": normalized_native,
                        "oracle": {
                            "curve": {
                                "order": str(curve_oracle["order"]),
                                "trace": str(curve_oracle["trace"]),
                            },
                            "twist": {
                                "order": str(twist_oracle["order"]),
                                "trace": str(twist_oracle["trace"]),
                            },
                        },
                    }
                    stream.write(canonical_json(record) + "\n")
                    stream.flush()
                    os.fsync(stream.fileno())
                    count += 1
                    curve_cursor = None if curve_cursor == MAX_U64 else curve_cursor + 1
        verify_file_identities(tracked_inputs)
        if executable_dependency_identity(native) != native_dependencies_start:
            raise AuditError("native executable dependency identity changed during the corpus run")
        if magma_dependency_identity(magma_root, magma) != magma_dependencies_start:
            raise AuditError("Magma dependency identity changed during the corpus run")
        if magma_runtime_identity(
            magma,
            magma_root,
            timeout_seconds=args.command_timeout_seconds,
            max_output_bytes=args.max_output_bytes,
        ) != magma_version_identity:
            raise AuditError("Magma runtime identity changed during the corpus run")
        final_source = git_identity()
        if final_source["commit"] != source["commit"]:
            raise AuditError("Git commit changed during the corpus run")
        base["identity"]["git_worktree_clean_at_completion"] = final_source[
            "worktree_clean"
        ]
        base["identity"]["validated_at_completion"] = True
        base.update(
            {
                "status": "complete",
                "completed_utc": utc_now(),
                "records": {
                    "path": records_path.name,
                    "count": count,
                    "sha256": digest(records_path),
                    "next_curve_index": (
                        None if curve_cursor is None else str(curve_cursor)
                    ),
                },
            }
        )
        write_manifest(manifest_path, base)
        return base
    except BaseException as exc:
        base.update(
            {
                "status": "interrupted" if isinstance(exc, KeyboardInterrupt) else "failed",
                "completed_utc": utc_now(),
                "error": f"{type(exc).__name__}: {exc}",
            }
        )
        if records_path.exists():
            with records_path.open(encoding="utf-8") as partial_stream:
                partial_lines = sum(1 for _ in partial_stream)
            base["partial_records"] = {
                "path": records_path.name,
                "sha256": digest(records_path),
                "lines": partial_lines,
            }
        if tracked_inputs:
            try:
                verify_file_identities(tracked_inputs)
            except BaseException as identity_exc:
                base["identity_verification_error"] = (
                    f"{type(identity_exc).__name__}: {identity_exc}"
                )
        for label, start, probe in (
            (
                "native dependency",
                native_dependencies_start,
                lambda: executable_dependency_identity(native_for_identity),
            ),
            (
                "Magma dependency",
                magma_dependencies_start,
                lambda: magma_dependency_identity(
                    magma_root_for_identity, magma_for_identity
                ),
            ),
        ):
            if start is None:
                continue
            try:
                if probe() != start:
                    base[f"{label.replace(' ', '_')}_verification_error"] = (
                        f"{label} identity changed during the corpus run"
                    )
            except BaseException as identity_exc:
                base[f"{label.replace(' ', '_')}_verification_error"] = (
                    f"{type(identity_exc).__name__}: {identity_exc}"
                )
        write_manifest(manifest_path, base)
        raise


def main(argv: list[str] | None = None) -> int:
    actual_argv = sys.argv[1:] if argv is None else argv
    try:
        require_bootstrap_context()
        args = parse_args(actual_argv)
        result = audit(args, [str(ORIGINAL_BOOTSTRAP), *actual_argv])
    except (AuditError, RuntimeError, OSError, ValueError) as exc:
        print(f"Weber oracle corpus: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Weber oracle corpus: interrupted", file=sys.stderr)
        return 130
    print(canonical_json(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
