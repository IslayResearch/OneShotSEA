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
    levels = record.get("sea_level_timings")
    if not isinstance(levels, list) or not levels:
        fail("search-curve record has no SEA levels")

    current_pass = 0
    prior_ell = 0
    residue = 0
    modulus = 1
    exact_levels = 0
    passes: list[dict[str, object]] = []
    for position, raw_level in enumerate(levels):
        if not isinstance(raw_level, dict):
            fail(f"SEA level {position} is not an object")
        pass_number = decimal(raw_level.get("pass"), f"level[{position}].pass")
        ell = decimal(raw_level.get("ell"), f"level[{position}].ell")
        if pass_number != current_pass:
            if pass_number != current_pass + 1:
                fail("SEA passes are not contiguous")
            if current_pass:
                passes.append({"pass": current_pass, "modulus": str(modulus)})
            current_pass = pass_number
            prior_ell = 0
            residue = 0
            modulus = 1
        if ell <= prior_ell:
            fail(f"SEA levels are not increasing within pass {current_pass}")
        prior_ell = ell

        exact = raw_level.get("exact")
        if not isinstance(exact, bool):
            fail(f"level[{position}].exact is not Boolean")
        claimed_residue = raw_level.get("trace_residue")
        if exact:
            small = decimal(claimed_residue, f"level[{position}].trace_residue")
            if small >= ell:
                fail(f"trace residue {small} is not canonical modulo {ell}")
            if trace % ell != small:
                fail(
                    f"independent trace disagrees at ell={ell}: "
                    f"expected {trace % ell}, logged {small}"
                )
            residue = combine_crt(residue, modulus, small, ell)
            modulus *= ell
            exact_levels += 1
        elif claimed_residue is not None:
            fail(f"nonexact level ell={ell} unexpectedly claims a residue")

        logged_modulus = decimal(
            raw_level.get("exact_modulus"), f"level[{position}].exact_modulus"
        )
        logged_count = decimal(
            raw_level.get("trace_candidate_count"),
            f"level[{position}].trace_candidate_count",
        )
        if logged_modulus != modulus:
            fail(
                f"CRT modulus mismatch at ell={ell}: expected {modulus}, "
                f"logged {logged_modulus}"
            )
        expected_count = candidate_count(radius, residue, modulus)
        if logged_count != expected_count:
            fail(
                f"candidate count mismatch at ell={ell}: expected "
                f"{expected_count}, logged {logged_count}"
            )
    passes.append({"pass": current_pass, "modulus": str(modulus)})
    return {
        "schema": "oneshotsea.sea-progress-audit.v1",
        "index": str(record.get("index")),
        "prime": str(prime),
        "trace": str(trace),
        "curve_order": str(prime + 1 - trace),
        "twist_order": str(prime + 1 + trace),
        "curve_twist_sum": str(2 * prime + 2),
        "levels": len(levels),
        "exact_levels": exact_levels,
        "passes": passes,
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
