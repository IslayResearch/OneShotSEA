#!/usr/bin/env python3
"""Generate small classical j modular polynomials without a CAS.

For a prime ``ell``, the classical polynomial is characterized by

    Phi_ell(j(q), j(q**ell)) = 0,

symmetry, degree ``ell + 1`` in each variable, and monicity.  All remaining
monomials have bidegree at most ``(ell, ell)``.  We compute the exact series

    j(q) = q**-1 * E4(q)**3 * product((1-q**n)**-24, n >= 1)

over the integers.  Ordering the symmetric unknowns ``X**i Y**j + X**j Y**i``
with ``i <= j`` by descending pole order ``i + ell*j`` makes the Laurent-series
system triangular with unit diagonal.  Thus every coefficient is obtained by
integer subtraction, with no rational linear algebra, numerical approximation,
precomputed table, or computer algebra system.

The initial supported levels are 2, 3, 5, and 7.  The implementation verifies the
resulting q-series identity beyond all coefficients used by the triangular
solve before returning a polynomial.
"""

from __future__ import annotations

import argparse
from math import comb
from pathlib import Path
import sys
from typing import Iterable


SUPPORTED_LEVELS = (2, 3, 5, 7)


def _multiply(lhs: list[int], rhs: list[int], degree: int) -> list[int]:
    result = [0] * (degree + 1)
    for left_degree, left in enumerate(lhs):
        if left == 0 or left_degree > degree:
            continue
        maximum_right = min(len(rhs) - 1, degree - left_degree)
        for right_degree in range(maximum_right + 1):
            right = rhs[right_degree]
            if right:
                result[left_degree + right_degree] += left * right
    return result


def _powers(series: list[int], maximum_power: int, degree: int) -> list[list[int]]:
    values = [[1] + [0] * degree]
    for _ in range(maximum_power):
        values.append(_multiply(values[-1], series, degree))
    return values


def _sigma_three(n: int) -> int:
    return sum(divisor**3 for divisor in range(1, n + 1) if n % divisor == 0)


def j_unit_series(degree: int) -> list[int]:
    """Return coefficients of q*j(q) through ``q**degree`` exactly."""

    if degree < 0:
        raise ValueError("series degree must be nonnegative")
    e4 = [1] + [240 * _sigma_three(n) for n in range(1, degree + 1)]
    e4_cubed = _multiply(_multiply(e4, e4, degree), e4, degree)

    inverse_delta_unit = [1] + [0] * degree
    for n in range(1, degree + 1):
        factor = [0] * (degree + 1)
        for k in range(degree // n + 1):
            factor[k * n] = comb(23 + k, 23)
        inverse_delta_unit = _multiply(inverse_delta_unit, factor, degree)
    result = _multiply(e4_cubed, inverse_delta_unit, degree)
    expected_prefix = [1, 744, 196884, 21493760]
    if result[: min(len(result), len(expected_prefix))] != expected_prefix[: len(result)]:
        raise ArithmeticError("internal j-series self-check failed")
    return result


def _add_shifted(target: list[int], source: list[int], shift: int, scale: int,
                 minimum_exponent: int, maximum_exponent: int) -> None:
    for ordinary_degree, coefficient in enumerate(source):
        exponent = ordinary_degree - shift
        if exponent > maximum_exponent:
            break
        if exponent >= minimum_exponent and coefficient:
            target[exponent - minimum_exponent] += scale * coefficient


def _pair_series(i: int, j: int, ell: int, powers_q: list[list[int]],
                 powers_q_ell: list[list[int]], series_degree: int,
                 minimum_exponent: int, maximum_exponent: int) -> list[int]:
    result = [0] * (maximum_exponent - minimum_exponent + 1)
    first = _multiply(powers_q[i], powers_q_ell[j], series_degree)
    _add_shifted(result, first, i + ell * j, 1,
                 minimum_exponent, maximum_exponent)
    if i != j:
        second = _multiply(powers_q[j], powers_q_ell[i], series_degree)
        _add_shifted(result, second, j + ell * i, 1,
                     minimum_exponent, maximum_exponent)
    return result


def generate_coefficients(ell: int, verification_order: int | None = None) -> dict[tuple[int, int], int]:
    """Return all nonzero coefficients of ``Phi_ell(X,Y)``.

    Coefficient keys are ``(x_degree, y_degree)``.  The returned mapping includes
    both orientations of every non-diagonal symmetric term.
    """

    if ell not in SUPPORTED_LEVELS:
        raise ValueError(f"supported levels are {SUPPORTED_LEVELS}, got {ell}")
    degree = ell + 1
    maximum_pole = ell * degree
    if verification_order is None:
        verification_order = maximum_pole + 8
    if verification_order < 1:
        raise ValueError("verification order must be positive")
    series_degree = maximum_pole + verification_order

    unit = j_unit_series(series_degree)
    unit_at_ell = [0] * (series_degree + 1)
    for index in range(series_degree // ell + 1):
        unit_at_ell[index * ell] = unit[index]
    powers_q = _powers(unit, degree, series_degree)
    powers_q_ell = _powers(unit_at_ell, degree, series_degree)

    minimum_exponent = -maximum_pole
    residual = [0] * (maximum_pole + verification_order + 1)
    _add_shifted(residual, powers_q[degree], degree, 1,
                 minimum_exponent, verification_order)
    _add_shifted(residual, powers_q_ell[degree], ell * degree, 1,
                 minimum_exponent, verification_order)

    pairs = [(i, j) for j in range(ell + 1) for i in range(j + 1)]
    pairs.sort(key=lambda pair: pair[0] + ell * pair[1], reverse=True)
    solved: dict[tuple[int, int], int] = {}
    for i, j in pairs:
        pole_order = i + ell * j
        basis = _pair_series(i, j, ell, powers_q, powers_q_ell,
                             series_degree, minimum_exponent, verification_order)
        pivot = -pole_order - minimum_exponent
        if basis[pivot] != 1:
            raise ArithmeticError(
                f"non-unit triangular pivot for ell={ell}, pair={(i, j)}")
        coefficient = -residual[pivot]
        if coefficient:
            for index, value in enumerate(basis):
                residual[index] += coefficient * value
            solved[(j, i)] = coefficient
            if i != j:
                solved[(i, j)] = coefficient

    first_nonzero = next(
        (exponent for exponent, value in
         zip(range(minimum_exponent, verification_order + 1), residual) if value),
        None,
    )
    if first_nonzero is not None:
        raise ArithmeticError(
            f"Phi_{ell} q-series identity failed at q^{first_nonzero}")

    solved[(degree, 0)] = 1
    solved[(0, degree)] = 1
    return solved


def format_sparse(ell: int, coefficients: dict[tuple[int, int], int]) -> str:
    """Format coefficients in the repository's deterministic sparse order."""

    degree = ell + 1
    lines = [f"# Full sparse classical modular polynomial Phi_{ell}(X,Y)."]
    for pair in ((degree, 0), (0, degree)):
        value = coefficients.get(pair, 0)
        if value:
            lines.append(f"{pair[0]} {pair[1]} {value}")
    # Phi_2 predates the general lexicographic repository order and places its
    # diagonal/high-total-degree terms first. Preserve that established byte
    # format. Levels 3 and above use x descending, then y ascending.
    if ell == 2:
        pairs = [(2, 2), (2, 1), (2, 0), (1, 1), (1, 0), (0, 0)]
    else:
        pairs = [
            (x_degree, y_degree)
            for x_degree in range(ell, -1, -1)
            for y_degree in range(x_degree + 1)
        ]
    for x_degree, y_degree in pairs:
        value = coefficients.get((x_degree, y_degree), 0)
        if not value:
            continue
        lines.append(f"{x_degree} {y_degree} {value}")
        if x_degree != y_degree:
            mirror = coefficients.get((y_degree, x_degree), 0)
            if mirror != value:
                raise ArithmeticError("asymmetric modular-polynomial coefficients")
            lines.append(f"{y_degree} {x_degree} {value}")
    return "\n".join(lines) + "\n"


def generate_sparse(ell: int, verification_order: int | None = None) -> str:
    return format_sparse(ell, generate_coefficients(ell, verification_order))


def _arguments(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--level", type=int, choices=SUPPORTED_LEVELS, required=True)
    parser.add_argument("--verification-order", type=int)
    destination = parser.add_mutually_exclusive_group()
    destination.add_argument("--output", type=Path, help="write output instead of stdout")
    destination.add_argument("--check", type=Path, help="fail unless this file byte-matches")
    return parser.parse_args(list(argv))


def main(argv: Iterable[str] = sys.argv[1:]) -> int:
    arguments = _arguments(argv)
    output = generate_sparse(arguments.level, arguments.verification_order)
    if arguments.check is not None:
        expected = arguments.check.read_bytes()
        actual = output.encode("ascii")
        if actual != expected:
            print(f"generated Phi_{arguments.level} does not match {arguments.check}",
                  file=sys.stderr)
            return 1
        print(f"ok: generated Phi_{arguments.level} byte-matches {arguments.check}")
        return 0
    if arguments.output is not None:
        arguments.output.write_bytes(output.encode("ascii"))
    else:
        sys.stdout.write(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
