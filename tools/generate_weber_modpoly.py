#!/usr/bin/env python3
"""Generate toy Weber-f modular polynomials from exact q-products.

The normalization is q=exp(2*pi*i*z), r=q^(1/48), and

    f(z) = r^-1 product_{m odd, m>=1} (1+r^(24m)).

For primes ell not dividing 48, Phi_ell^f is symmetric and monic of degree
ell+1.  Its X^a Y^b coefficient vanishes unless ell*a+b == ell+1 (mod 24).
The remaining coefficients are solved exactly from
Phi_ell^f(f(z),f(ell*z))=0 in Z((r)).  This intentionally small generator is
for normalization/identity fixtures, not large-level production CRT work.
"""

from __future__ import annotations

import argparse
from math import gcd
import sys


MAX_TOY_LEVEL = 43


def _is_prime(value: int) -> bool:
    return value >= 2 and all(value % divisor for divisor in range(2, int(value**0.5) + 1))


def _multiply(left: list[int], right: list[int], degree: int) -> list[int]:
    result = [0] * (degree + 1)
    for i, a in enumerate(left):
        if not a:
            continue
        for j, b in enumerate(right[: degree - i + 1]):
            if b:
                result[i + j] += a * b
    return result


def _powers(series: list[int], maximum: int, degree: int) -> list[list[int]]:
    result = [[1] + [0] * degree]
    for _ in range(maximum):
        result.append(_multiply(result[-1], series, degree))
    return result


def weber_unit_series(degree: int) -> list[int]:
    """Return U(r)=product_(m odd)(1+r^(24m)) through r^degree."""

    result = [1] + [0] * degree
    for odd in range(1, degree // 24 + 1, 2):
        step = 24 * odd
        for index in range(degree, step - 1, -1):
            result[index] += result[index - step]
    return result


def _add_shifted(target: list[int], source: list[int], shift: int, scale: int,
                 minimum: int, maximum: int) -> None:
    for degree, coefficient in enumerate(source):
        exponent = degree - shift
        if exponent > maximum:
            break
        if exponent >= minimum and coefficient:
            target[exponent - minimum] += scale * coefficient


def generate_coefficients(ell: int, verification_order: int | None = None) -> dict[tuple[int, int], int]:
    if not _is_prime(ell) or gcd(ell, 48) != 1:
        raise ValueError("Weber-f levels must be prime and coprime to 48")
    if ell > MAX_TOY_LEVEL:
        raise ValueError(f"toy q-product generator is capped at ell={MAX_TOY_LEVEL}")
    degree = ell + 1
    maximum_pole = ell * degree
    if verification_order is None:
        verification_order = maximum_pole + 8
    if verification_order < 1:
        raise ValueError("verification order must be positive")
    series_degree = maximum_pole + verification_order
    unit = weber_unit_series(series_degree)
    unit_ell = [0] * (series_degree + 1)
    for index in range(series_degree // ell + 1):
        unit_ell[index * ell] = unit[index]
    powers = _powers(unit, degree, series_degree)
    powers_ell = _powers(unit_ell, degree, series_degree)
    minimum = -maximum_pole

    def shifted(series: list[int], shift: int) -> list[int]:
        result = [0] * (maximum_pole + verification_order + 1)
        _add_shifted(result, series, shift, 1, minimum, verification_order)
        return result

    residual = [
        a + b for a, b in zip(
            shifted(powers[degree], degree),
            shifted(powers_ell[degree], ell * degree),
        )
    ]
    pairs = [
        (i, j)
        for j in range(ell + 1)
        for i in range(j + 1)
        if (i + ell * j - degree) % 24 == 0
    ]
    pairs.sort(key=lambda pair: pair[0] + ell * pair[1], reverse=True)
    coefficients: dict[tuple[int, int], int] = {(degree, 0): 1, (0, degree): 1}
    for i, j in pairs:
        basis = shifted(_multiply(powers[i], powers_ell[j], series_degree), i + ell * j)
        if i != j:
            mirror = shifted(
                _multiply(powers[j], powers_ell[i], series_degree), j + ell * i)
            basis = [a + b for a, b in zip(basis, mirror)]
        pivot = -(i + ell * j) - minimum
        if basis[pivot] != 1:
            raise ArithmeticError("Weber-f Laurent system lost its unit pivot")
        coefficient = -residual[pivot]
        if coefficient:
            residual = [a + coefficient * b for a, b in zip(residual, basis)]
            coefficients[j, i] = coefficient
            coefficients[i, j] = coefficient
    failure = next(
        (exponent for exponent, value in
         zip(range(minimum, verification_order + 1), residual) if value), None)
    if failure is not None:
        raise ArithmeticError(f"Weber-f identity failed at r^{failure}")
    return coefficients


def format_sparse(ell: int, coefficients: dict[tuple[int, int], int]) -> str:
    degree = ell + 1
    lines = [f"# Weber-f modular polynomial Phi_{ell}^f(X,Y); r=q^(1/48)."]
    for x_degree, y_degree in ((degree, 0), (0, degree)):
        lines.append(f"{x_degree} {y_degree} 1")
    for x_degree in range(ell, -1, -1):
        for y_degree in range(x_degree + 1):
            coefficient = coefficients.get((x_degree, y_degree), 0)
            if not coefficient:
                continue
            lines.append(f"{x_degree} {y_degree} {coefficient}")
            if x_degree != y_degree:
                lines.append(f"{y_degree} {x_degree} {coefficient}")
    return "\n".join(lines) + "\n"


def specialize(coefficients: dict[tuple[int, int], int], x: int,
               modulus: int) -> list[int]:
    """Return ascending Y coefficients of Phi(x,Y) modulo modulus."""

    if modulus <= 1:
        raise ValueError("modulus must exceed one")
    maximum_y = max(y_degree for _, y_degree in coefficients)
    result = [0] * (maximum_y + 1)
    for (x_degree, y_degree), coefficient in coefficients.items():
        result[y_degree] = (
            result[y_degree] + coefficient * pow(x, x_degree, modulus)
        ) % modulus
    return result


def j_from_weber(value: int, modulus: int) -> int:
    """Evaluate j=(f^24-16)^3/f^24 over a prime field."""

    power = pow(value, 24, modulus)
    if power == 0:
        raise ZeroDivisionError("a Weber-f value cannot be zero")
    return pow(power - 16, 3, modulus) * pow(power, -1, modulus) % modulus


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--level", type=int, required=True)
    parser.add_argument("--specialize", type=int, metavar="X")
    parser.add_argument("--modulus", type=int)
    arguments = parser.parse_args()
    if (arguments.specialize is None) != (arguments.modulus is None):
        parser.error("--specialize and --modulus must be supplied together")
    coefficients = generate_coefficients(arguments.level)
    if arguments.specialize is None:
        sys.stdout.write(format_sparse(arguments.level, coefficients))
    else:
        values = specialize(coefficients, arguments.specialize, arguments.modulus)
        print(" ".join(str(value) for value in values))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
