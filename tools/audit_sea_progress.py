#!/usr/bin/env python3
"""Audit retained exact SEA residues against an independent final trace."""

from __future__ import annotations

import argparse
import json
from math import gcd, isqrt
from pathlib import Path
import sys


def fail(message: str) -> "NoReturn":
    raise SystemExit(f"error: {message}")


def decimal(value: object, label: str) -> int:
    if not isinstance(value, str) or not value.isdecimal():
        fail(f"{label} must be an unsigned decimal string")
    return int(value)


def signed_decimal(value: object, label: str) -> int:
    if not isinstance(value, str):
        fail(f"{label} must be a signed decimal string")
    digits = value[1:] if value.startswith("-") else value
    if not digits.isdecimal():
        fail(f"{label} must be a signed decimal string")
    return int(value)


def candidate_count(radius: int, residue: int, modulus: int) -> int:
    lower = -radius
    upper = radius
    first = lower + (residue - lower) % modulus
    if first > upper:
        return 0
    return (upper - first) // modulus + 1


def combine_crt(residue: int, modulus: int, small: int, ell: int) -> int:
    if gcd(modulus, ell) != 1:
        fail(f"SEA level {ell} is not coprime to accumulated modulus {modulus}")
    step = ((small - residue) * pow(modulus, -1, ell)) % ell
    return (residue + modulus * step) % (modulus * ell)


def matrix_multiply(
    left: tuple[int, int, int, int],
    right: tuple[int, int, int, int],
    ell: int,
) -> tuple[int, int, int, int]:
    a, b, c, d = left
    e, f, g, h = right
    return (
        (a * e + b * g) % ell,
        (a * f + b * h) % ell,
        (c * e + d * g) % ell,
        (c * f + d * h) % ell,
    )


def projective_order(ell: int, prime: int, trace: int) -> int:
    matrix = (0, -prime % ell, 1, trace % ell)
    power = (1, 0, 0, 1)
    # The nonsplit Atkin case has order dividing ell+1. This deliberately
    # uses independent repeated multiplication rather than mirroring the
    # optimized order-reduction code in src/trace.cpp.
    for order in range(1, ell + 2):
        power = matrix_multiply(power, matrix, ell)
        if power[1] == 0 and power[2] == 0 and power[0] == power[3]:
            return order
    fail(f"Frobenius did not become scalar through ell+1 at ell={ell}")


def atkin_residues(ell: int, prime: int, order: int) -> list[int]:
    if order < 2 or (ell + 1) % order:
        fail(f"invalid Atkin projective order {order} at ell={ell}")
    values: list[int] = []
    for trace in range(ell):
        discriminant = (trace * trace - 4 * prime) % ell
        if (
            discriminant
            and pow(discriminant, (ell - 1) // 2, ell) == ell - 1
            and projective_order(ell, prime, trace) == order
        ):
            values.append(trace)
    if not values:
        fail(f"Atkin order {order} produced no residues at ell={ell}")
    return values


def residue_set_candidate_count(
    radius: int, residues: list[int], modulus: int
) -> int:
    return sum(candidate_count(radius, residue, modulus) for residue in residues)


def audit_record(record: dict[str, object], trace: int) -> dict[str, object]:
    if record.get("schema") != "oneshotsea.search-curve.v1":
        fail("selected record is not a search-curve record")
    state = record.get("state")
    if not isinstance(state, dict):
        fail("search-curve record has no state object")
    prime = decimal(state.get("prime"), "state.prime")
    radius = isqrt(4 * prime)
    if not -radius <= trace <= radius:
        fail("independent trace is outside the Hasse interval")
    raw_prior = record.get("trace_prior")
    if raw_prior is None:
        prior_modulus = 1
        prior_residue = 0
    else:
        if not isinstance(raw_prior, dict) or set(raw_prior) != {
            "modulus",
            "residue",
        }:
            fail("trace_prior must be null or an exact modulus/residue object")
        prior_modulus = decimal(raw_prior.get("modulus"), "trace_prior.modulus")
        prior_residue = decimal(raw_prior.get("residue"), "trace_prior.residue")
        if prior_modulus < 2 or prior_residue >= prior_modulus:
            fail("trace_prior is not canonical")
        if gcd(prime, prior_modulus) != 1:
            fail("trace_prior modulus is not coprime to the field characteristic")
        if trace % prior_modulus != prior_residue:
            fail("independent trace disagrees with trace_prior")
    levels = record.get("sea_level_timings")
    direct_levels = record.get("classical_direct_levels", [])
    if not isinstance(levels, list):
        fail("search-curve sea_level_timings is not a list")
    if not isinstance(direct_levels, list):
        fail("search-curve classical_direct_levels is not a list")
    if not levels and not direct_levels:
        fail("search-curve record has no SEA levels")

    current_pass = 0
    prior_ell = 0
    residue = prior_residue
    modulus = prior_modulus
    effective_residues = [prior_residue]
    effective_modulus = prior_modulus
    exact_levels = 0
    atkin_levels = 0
    passes: list[dict[str, object]] = []
    direct_passes: list[dict[str, object]] = []

    def replay_level_stream(
        stream: list[object], stream_name: str, reset_between_passes: bool
    ) -> None:
        nonlocal current_pass, prior_ell, residue, modulus
        nonlocal effective_residues, effective_modulus
        nonlocal exact_levels, atkin_levels

        current_pass = 0
        prior_ell = 0
        for position, raw_level in enumerate(stream):
            label = f"{stream_name}[{position}]"
            if not isinstance(raw_level, dict):
                fail(f"{label} is not an object")
            pass_number = decimal(raw_level.get("pass"), f"{label}.pass")
            ell = decimal(raw_level.get("ell"), f"{label}.ell")
            if pass_number != current_pass:
                if pass_number != current_pass + 1:
                    fail(f"{stream_name} passes are not contiguous")
                if current_pass:
                    target = passes if reset_between_passes else direct_passes
                    target.append(
                        {"pass": current_pass, "modulus": str(modulus)}
                    )
                current_pass = pass_number
                prior_ell = 0
                if reset_between_passes:
                    residue = prior_residue
                    modulus = prior_modulus
                    effective_residues = [prior_residue]
                    effective_modulus = prior_modulus
            if ell <= prior_ell:
                fail(
                    f"{stream_name} levels are not increasing within pass "
                    f"{current_pass}"
                )
            prior_ell = ell

            exact = raw_level.get("exact")
            if not isinstance(exact, bool):
                fail(f"{label}.exact is not Boolean")
            claimed_residue = raw_level.get("trace_residue")
            if exact:
                small = decimal(claimed_residue, f"{label}.trace_residue")
                if small >= ell:
                    fail(f"trace residue {small} is not canonical modulo {ell}")
                if trace % ell != small:
                    fail(
                        f"independent trace disagrees at ell={ell}: "
                        f"expected {trace % ell}, logged {small}"
                    )
                residue = combine_crt(residue, modulus, small, ell)
                modulus *= ell
                effective_residues = [
                    combine_crt(value, effective_modulus, small, ell)
                    for value in effective_residues
                ]
                effective_modulus *= ell
                exact_levels += 1
            elif claimed_residue is not None:
                fail(f"nonexact level ell={ell} unexpectedly claims a residue")

            claimed_atkin_order = raw_level.get("atkin_projective_order")
            if claimed_atkin_order is not None:
                if exact:
                    fail(f"exact level ell={ell} also claims Atkin evidence")
                order = decimal(
                    claimed_atkin_order, f"{label}.atkin_projective_order"
                )
                allowed = atkin_residues(ell, prime, order)
                logged_residue_count = decimal(
                    raw_level.get("atkin_residue_count"),
                    f"{label}.atkin_residue_count",
                )
                if logged_residue_count != len(allowed):
                    fail(
                        f"Atkin residue-count mismatch at ell={ell}: expected "
                        f"{len(allowed)}, logged {logged_residue_count}"
                    )
                if trace % ell not in allowed:
                    fail(
                        f"independent trace violates Atkin constraint at ell={ell}"
                    )
                effective_residues = [
                    combine_crt(value, effective_modulus, small, ell)
                    for value in effective_residues
                    for small in allowed
                ]
                effective_residues = sorted(set(effective_residues))
                effective_modulus *= ell
                atkin_levels += 1

            if stream_name == "classical_direct_levels":
                discriminant = signed_decimal(
                    raw_level.get("order_discriminant"),
                    f"{label}.order_discriminant",
                )
                class_number = decimal(
                    raw_level.get("class_number"), f"{label}.class_number"
                )
                auxiliary_primes = decimal(
                    raw_level.get("auxiliary_prime_count"),
                    f"{label}.auxiliary_prime_count",
                )
                kernels = decimal(
                    raw_level.get("elkies_kernel_count"),
                    f"{label}.elkies_kernel_count",
                )
                decimal(raw_level.get("elapsed_us"), f"{label}.elapsed_us")
                if discriminant >= 0 or class_number == 0 or auxiliary_primes == 0:
                    fail(f"{label} has invalid CM/CRT evidence")
                if (exact and kernels == 0) or (not exact and kernels != 0):
                    fail(f"{label} has inconsistent Elkies kernel evidence")

            logged_modulus = decimal(
                raw_level.get("exact_modulus"), f"{label}.exact_modulus"
            )
            if logged_modulus != modulus:
                fail(
                    f"CRT modulus mismatch at ell={ell}: expected {modulus}, "
                    f"logged {logged_modulus}"
                )
            expected_count = candidate_count(radius, residue, modulus)
            logged_exact_count_value = raw_level.get(
                "exact_trace_candidate_count"
            )
            # Backward compatibility for retained pre-Atkin v1 records.
            logged_exact_count = decimal(
                raw_level.get("trace_candidate_count")
                if logged_exact_count_value is None
                else logged_exact_count_value,
                f"{label}.exact_trace_candidate_count",
            )
            if logged_exact_count != expected_count:
                fail(
                    f"exact candidate-count mismatch at ell={ell}: expected "
                    f"{expected_count}, logged {logged_exact_count}"
                )
            logged_constraint_modulus_value = raw_level.get(
                "constraint_modulus"
            )
            if logged_constraint_modulus_value is not None:
                logged_constraint_modulus = decimal(
                    logged_constraint_modulus_value,
                    f"{label}.constraint_modulus",
                )
                if logged_constraint_modulus != effective_modulus:
                    fail(
                        f"effective modulus mismatch at ell={ell}: expected "
                        f"{effective_modulus}, logged {logged_constraint_modulus}"
                    )
                effective_count = residue_set_candidate_count(
                    radius, effective_residues, effective_modulus
                )
                logged_effective_count = decimal(
                    raw_level.get("trace_candidate_count"),
                    f"{label}.trace_candidate_count",
                )
                if logged_effective_count != effective_count:
                    fail(
                        f"effective candidate-count mismatch at ell={ell}: "
                        f"expected {effective_count}, logged {logged_effective_count}"
                    )
        if current_pass:
            target = passes if reset_between_passes else direct_passes
            target.append({"pass": current_pass, "modulus": str(modulus)})

    replay_level_stream(levels, "sea_level_timings", True)
    replay_level_stream(direct_levels, "classical_direct_levels", False)
    return {
        "schema": "oneshotsea.sea-progress-audit.v1",
        "index": str(record.get("index")),
        "prime": str(prime),
        "trace": str(trace),
        "trace_prior": (
            None
            if raw_prior is None
            else {"modulus": str(prior_modulus), "residue": str(prior_residue)}
        ),
        "curve_order": str(prime + 1 - trace),
        "twist_order": str(prime + 1 + trace),
        "curve_twist_sum": str(2 * prime + 2),
        "levels": len(levels) + len(direct_levels),
        "weber_levels": len(levels),
        "classical_direct_levels": len(direct_levels),
        "exact_levels": exact_levels,
        "atkin_levels": atkin_levels,
        "passes": passes,
        "classical_direct_passes": direct_passes,
        "verified": True,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--progress", type=Path, required=True)
    parser.add_argument("--trace", type=int, required=True)
    parser.add_argument("--index", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    matches: list[dict[str, object]] = []
    try:
        with args.progress.open(encoding="utf-8") as stream:
            for line_number, line in enumerate(stream, 1):
                if not line.strip():
                    continue
                value = json.loads(line)
                if (
                    isinstance(value, dict)
                    and value.get("schema") == "oneshotsea.search-curve.v1"
                    and str(value.get("index")) == args.index
                ):
                    matches.append(value)
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read progress log: {exc}")
    if len(matches) != 1:
        fail(f"expected exactly one curve record for index {args.index}")
    print(json.dumps(audit_record(matches[0], args.trace), sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
