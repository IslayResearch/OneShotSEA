"""CAS-free exact Elkies reference for levels 5 and 7.

This is a kernel-first prototype.  It factors the odd division polynomial over
F_p, assembles rational divisors of degree (ell-1)/2, and retains exactly those
whose roots are closed under multiplication in a cyclic ell-subgroup.  It then
computes Velu's normalized codomain from kernel power sums and identifies the
Frobenius eigenvalue in F_p[x]/h.  Classical modular-polynomial data is not
needed for correctness and is not available in this repository at these levels.

The implementation is deliberately small and auditable, not fast enough for
production-size levels.  It uses only Python integer and tuple arithmetic.
"""

from __future__ import annotations

from dataclasses import dataclass
from functools import lru_cache
import hashlib
from typing import Iterable


Poly = tuple[int, ...]  # constant coefficient first


class NotElkiesError(ValueError):
    """Raised when no Frobenius-stable cyclic ell-subgroup exists over F_p."""


@dataclass(frozen=True)
class GeneralElkiesKernel:
    ell: int
    polynomial: Poly
    codomain_a: int
    codomain_b: int
    eigenvalue: int
    trace_residue: int


def _trim(values: Iterable[int], p: int) -> Poly:
    result = [value % p for value in values]
    while result and result[-1] == 0:
        result.pop()
    return tuple(result)


def _add(left: Poly, right: Poly, p: int) -> Poly:
    return _trim(
        (
            (left[i] if i < len(left) else 0)
            + (right[i] if i < len(right) else 0)
            for i in range(max(len(left), len(right)))
        ),
        p,
    )


def _neg(value: Poly, p: int) -> Poly:
    return _trim((-coefficient for coefficient in value), p)


def _sub(left: Poly, right: Poly, p: int) -> Poly:
    return _add(left, _neg(right, p), p)


def _scale(value: Poly, scalar: int, p: int) -> Poly:
    return _trim((scalar * coefficient for coefficient in value), p)


def _mul(left: Poly, right: Poly, p: int) -> Poly:
    if not left or not right:
        return ()
    result = [0] * (len(left) + len(right) - 1)
    for i, left_coefficient in enumerate(left):
        for j, right_coefficient in enumerate(right):
            result[i + j] += left_coefficient * right_coefficient
    return _trim(result, p)


def _divmod_poly(numerator: Poly, denominator: Poly, p: int) -> tuple[Poly, Poly]:
    if not denominator:
        raise ZeroDivisionError("polynomial division by zero")
    denominator = _trim(denominator, p)
    remainder = list(_trim(numerator, p))
    quotient = [0] * max(1, len(remainder) - len(denominator) + 1)
    inverse_lead = pow(denominator[-1], -1, p)
    while remainder and len(remainder) >= len(denominator):
        shift = len(remainder) - len(denominator)
        factor = remainder[-1] * inverse_lead % p
        quotient[shift] = factor
        for i, coefficient in enumerate(denominator):
            remainder[shift + i] = (remainder[shift + i] - factor * coefficient) % p
        while remainder and remainder[-1] == 0:
            remainder.pop()
    return _trim(quotient, p), _trim(remainder, p)


def _mod(value: Poly, modulus: Poly, p: int) -> Poly:
    return _divmod_poly(value, modulus, p)[1]


def _monic(value: Poly, p: int) -> Poly:
    if not value:
        return ()
    return _scale(value, pow(value[-1], -1, p), p)


def _gcd(left: Poly, right: Poly, p: int) -> Poly:
    while right:
        left, right = right, _mod(left, right, p)
    return _monic(left, p)


def _derivative(value: Poly, p: int) -> Poly:
    return _trim((i * value[i] for i in range(1, len(value))), p)


def _powmod(base: Poly, exponent: int, modulus: Poly, p: int) -> Poly:
    if exponent < 0:
        raise ValueError("negative polynomial exponent")
    result: Poly = (1,)
    base = _mod(base, modulus, p)
    while exponent:
        if exponent & 1:
            result = _mod(_mul(result, base, p), modulus, p)
        exponent >>= 1
        if exponent:
            base = _mod(_mul(base, base, p), modulus, p)
    return result


def _evaluate(value: Poly, argument: int, p: int) -> int:
    result = 0
    for coefficient in reversed(value):
        result = (result * argument + coefficient) % p
    return result


def _compose_evaluate(value: Poly, argument: Poly, modulus: Poly, p: int) -> Poly:
    result: Poly = ()
    for coefficient in reversed(value):
        result = _mod(_add(_mul(result, argument, p), (coefficient,), p), modulus, p)
    return result


def _inverse_mod(value: Poly, modulus: Poly, p: int) -> Poly:
    """Invert a unit in F_p[x]/modulus using extended Euclid."""

    old_r, r = modulus, _mod(value, modulus, p)
    old_t, t = (), (1,)
    while r:
        quotient, remainder = _divmod_poly(old_r, r, p)
        old_r, r = r, remainder
        old_t, t = t, _sub(old_t, _mul(quotient, t, p), p)
    if len(old_r) != 1:
        raise ZeroDivisionError("nonunit in kernel quotient algebra")
    return _mod(_scale(old_t, pow(old_r[0], -1, p), p), modulus, p)


RawElement = tuple[Poly, Poly]  # u(x) + y*v(x)


def _raw_add(left: RawElement, right: RawElement, p: int) -> RawElement:
    return _add(left[0], right[0], p), _add(left[1], right[1], p)


def _raw_sub(left: RawElement, right: RawElement, p: int) -> RawElement:
    return _sub(left[0], right[0], p), _sub(left[1], right[1], p)


def _raw_mul(left: RawElement, right: RawElement, curve_rhs: Poly, p: int) -> RawElement:
    return (
        _add(
            _mul(left[0], right[0], p),
            _mul(curve_rhs, _mul(left[1], right[1], p), p),
            p,
        ),
        _add(_mul(left[0], right[1], p), _mul(left[1], right[0], p), p),
    )


def _division_polynomial(ell: int, a: int, b: int, p: int) -> Poly:
    a %= p
    b %= p
    curve_rhs = _trim((b, a, 0, 1), p)

    @lru_cache(maxsize=None)
    def psi(index: int) -> RawElement:
        if index == 0:
            return (), ()
        if index == 1:
            return (1,), ()
        if index == 2:
            return (), (2,)
        if index == 3:
            return _trim((-a * a, 12 * b, 6 * a, 0, 3), p), ()
        if index == 4:
            inside = _trim(
                (-a**3 - 8 * b * b, -4 * a * b, -5 * a * a, 20 * b, 5 * a, 0, 1),
                p,
            )
            return (), _scale(inside, 4, p)
        if index & 1:
            m = (index - 1) // 2
            first = _raw_mul(
                psi(m + 2),
                _raw_mul(_raw_mul(psi(m), psi(m), curve_rhs, p), psi(m), curve_rhs, p),
                curve_rhs,
                p,
            )
            second = _raw_mul(
                psi(m - 1),
                _raw_mul(
                    _raw_mul(psi(m + 1), psi(m + 1), curve_rhs, p),
                    psi(m + 1),
                    curve_rhs,
                    p,
                ),
                curve_rhs,
                p,
            )
            return _raw_sub(first, second, p)
        m = index // 2
        bracket = _raw_sub(
            _raw_mul(
                psi(m + 2), _raw_mul(psi(m - 1), psi(m - 1), curve_rhs, p), curve_rhs, p
            ),
            _raw_mul(
                psi(m - 2), _raw_mul(psi(m + 1), psi(m + 1), curve_rhs, p), curve_rhs, p
            ),
            p,
        )
        numerator = _raw_mul(psi(m), bracket, curve_rhs, p)
        if numerator[1]:
            raise ArithmeticError("unexpected y term in division-polynomial recurrence")
        quotient, remainder = _divmod_poly(numerator[0], curve_rhs, p)
        if remainder:
            raise ArithmeticError("division-polynomial recurrence was not exact")
        return (), _scale(quotient, pow(2, -1, p), p)

    result = psi(ell)
    if result[1] or not result[0]:
        raise ArithmeticError("invalid odd division polynomial")
    return _monic(result[0], p)


def _factor_seed(p: int, value: Poly, degree: int, attempt: int) -> Poly:
    coefficients = []
    for index in range(len(value) - 1):
        digest = hashlib.sha256(
            f"elkies-general:{p}:{value}:{degree}:{attempt}:{index}".encode("ascii")
        ).digest()
        coefficients.append(int.from_bytes(digest, "big") % p)
    return _trim(coefficients, p)


def _equal_degree_factors(value: Poly, degree: int, p: int) -> list[Poly]:
    value = _monic(value, p)
    if len(value) - 1 == degree:
        return [value]
    exponent = (pow(p, degree) - 1) // 2
    for attempt in range(512):
        candidate = _factor_seed(p, value, degree, attempt)
        if not candidate:
            continue
        divisor = _gcd(value, _sub(_powmod(candidate, exponent, value, p), (1,), p), p)
        if 0 < len(divisor) - 1 < len(value) - 1:
            cofactor, remainder = _divmod_poly(value, divisor, p)
            if remainder:
                raise ArithmeticError("factor split was not exact")
            return _equal_degree_factors(divisor, degree, p) + _equal_degree_factors(
                cofactor, degree, p
            )
    raise ArithmeticError("equal-degree factorization attempt limit reached")


def _factor_squarefree(value: Poly, p: int) -> list[Poly]:
    """Factor a square-free polynomial into monic irreducibles over F_p."""

    remaining = _monic(value, p)
    if _gcd(remaining, _derivative(remaining, p), p) != (1,):
        raise ArithmeticError("division polynomial is not square-free")
    factors: list[Poly] = []
    x = (0, 1)
    frobenius_x = x
    degree = 1
    while remaining and 2 * degree <= len(remaining) - 1:
        frobenius_x = _powmod(_mod(frobenius_x, remaining, p), p, remaining, p)
        block = _gcd(remaining, _sub(frobenius_x, x, p), p)
        block_degree = len(block) - 1
        if block_degree > 0:
            factors.extend(_equal_degree_factors(block, degree, p))
            remaining, remainder = _divmod_poly(remaining, block, p)
            if remainder:
                raise ArithmeticError("distinct-degree factorization was not exact")
            remaining = _monic(remaining, p)
            if len(remaining) <= 1:
                break
            frobenius_x = _mod(frobenius_x, remaining, p)
        degree += 1
    if len(remaining) > 1:
        factors.append(_monic(remaining, p))
    factors.sort(key=lambda factor: (len(factor), factor))
    reconstructed: Poly = (1,)
    for factor in factors:
        reconstructed = _mul(reconstructed, factor, p)
    if _monic(reconstructed, p) != _monic(value, p):
        raise ArithmeticError("irreducible-factor reconstruction failed")
    return factors


def _candidate_divisors(factors: list[Poly], target_degree: int, p: int) -> list[Poly]:
    candidates = set()

    def visit(index: int, degree: int, value: Poly) -> None:
        if degree == target_degree:
            candidates.add(_monic(value, p))
            return
        for factor_index in range(index, len(factors)):
            factor = factors[factor_index]
            new_degree = degree + len(factor) - 1
            if new_degree > target_degree:
                continue
            visit(factor_index + 1, new_degree, _mul(value, factor, p))

    visit(0, 0, (1,))
    return sorted(candidates)


@dataclass(frozen=True)
class _Point:
    x: Poly
    y_multiplier: Poly  # y-coordinate is generic y times this value


def _ring_add(left: Poly, right: Poly, modulus: Poly, p: int) -> Poly:
    return _mod(_add(left, right, p), modulus, p)


def _ring_sub(left: Poly, right: Poly, modulus: Poly, p: int) -> Poly:
    return _mod(_sub(left, right, p), modulus, p)


def _ring_mul(left: Poly, right: Poly, modulus: Poly, p: int) -> Poly:
    return _mod(_mul(left, right, p), modulus, p)


def _point_double(point: _Point, base_rhs: Poly, a: int, modulus: Poly, p: int) -> _Point:
    numerator = _ring_add(
        _scale(_ring_mul(point.x, point.x, modulus, p), 3, p), (a,), modulus, p
    )
    denominator = _scale(_ring_mul(base_rhs, point.y_multiplier, modulus, p), 2, p)
    slope_multiplier = _ring_mul(numerator, _inverse_mod(denominator, modulus, p), modulus, p)
    x_new = _ring_sub(
        _ring_mul(base_rhs, _ring_mul(slope_multiplier, slope_multiplier, modulus, p), modulus, p),
        _scale(point.x, 2, p),
        modulus,
        p,
    )
    y_new = _ring_sub(
        _ring_mul(slope_multiplier, _ring_sub(point.x, x_new, modulus, p), modulus, p),
        point.y_multiplier,
        modulus,
        p,
    )
    return _Point(x_new, y_new)


def _point_add_generic_to_base(
    point: _Point, base: _Point, base_rhs: Poly, modulus: Poly, p: int
) -> _Point:
    numerator = _ring_sub(base.y_multiplier, point.y_multiplier, modulus, p)
    denominator = _ring_sub(base.x, point.x, modulus, p)
    slope_multiplier = _ring_mul(numerator, _inverse_mod(denominator, modulus, p), modulus, p)
    x_new = _ring_sub(
        _ring_sub(
            _ring_mul(
                base_rhs, _ring_mul(slope_multiplier, slope_multiplier, modulus, p), modulus, p
            ),
            point.x,
            modulus,
            p,
        ),
        base.x,
        modulus,
        p,
    )
    y_new = _ring_sub(
        _ring_mul(slope_multiplier, _ring_sub(point.x, x_new, modulus, p), modulus, p),
        point.y_multiplier,
        modulus,
        p,
    )
    return _Point(x_new, y_new)


def _small_multiples(h: Poly, a: int, b: int, ell: int, p: int) -> list[_Point]:
    x = _mod((0, 1), h, p)
    base_rhs = _mod((b, a, 0, 1), h, p)
    base = _Point(x, (1,))
    positive = [base]
    if (ell - 1) // 2 >= 2:
        positive.append(_point_double(base, base_rhs, a, h, p))
    while len(positive) < (ell - 1) // 2:
        positive.append(_point_add_generic_to_base(positive[-1], base, base_rhs, h, p))
    multiples = []
    for scalar in range(1, ell):
        if scalar <= (ell - 1) // 2:
            multiples.append(positive[scalar - 1])
        else:
            opposite = positive[ell - scalar - 1]
            multiples.append(_Point(opposite.x, _neg(opposite.y_multiplier, p)))
    return multiples


def _kernel_is_cyclic(h: Poly, a: int, b: int, ell: int, p: int) -> bool:
    try:
        multiples = _small_multiples(h, a, b, ell, p)
    except ZeroDivisionError:
        return False
    # h([k]x) vanishes for every nonzero multiple exactly when its roots are
    # the x-coordinates of one cyclic subgroup.
    return all(not _compose_evaluate(h, point.x, h, p) for point in multiples)


def _power_sums(h: Poly, maximum: int, p: int) -> list[int]:
    degree = len(h) - 1
    if not h or h[-1] != 1:
        raise ValueError("power sums require a monic polynomial")
    sums = [degree % p]
    c = [0] + [h[degree - index] for index in range(1, degree + 1)]
    for order in range(1, maximum + 1):
        total = 0
        if order <= degree:
            for index in range(1, order):
                total += c[index] * sums[order - index]
            total += order * c[order]
        else:
            for index in range(1, degree + 1):
                total += c[index] * sums[order - index]
        sums.append((-total) % p)
    return sums


def _velu_codomain(h: Poly, a: int, b: int, p: int) -> tuple[int, int]:
    degree = len(h) - 1
    sums = _power_sums(h, 3, p)
    v = (6 * sums[2] + 2 * degree * a) % p
    w = (10 * sums[3] + 6 * a * sums[1] + 4 * degree * b) % p
    return (a - 5 * v) % p, (b - 7 * w) % p


def _is_prime(value: int) -> bool:
    if value < 2:
        return False
    for prime in (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37):
        if value % prime == 0:
            return value == prime
    odd_part = value - 1
    power_of_two = 0
    while odd_part % 2 == 0:
        odd_part //= 2
        power_of_two += 1
    bases = (
        (2, 325, 9375, 28178, 450775, 9780504, 1795265022)
        if value < 1 << 64
        else (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37)
    )
    for base in bases:
        if base % value == 0:
            continue
        witness = pow(base, odd_part, value)
        if witness in (1, value - 1):
            continue
        for _ in range(power_of_two - 1):
            witness = witness * witness % value
            if witness == value - 1:
                break
        else:
            return False
    return True


def elkies_kernels(p: int, a: int, b: int, ell: int) -> tuple[GeneralElkiesKernel, ...]:
    """Return all validated Frobenius-stable cyclic kernels at ell=5 or 7."""

    if not _is_prime(p) or p <= 3:
        raise ValueError("p must be an odd prime greater than 3")
    if ell not in (5, 7):
        raise NotImplementedError("the general prototype currently supports ell=5 and ell=7")
    if p == ell:
        raise ValueError("ell must differ from p")
    a %= p
    b %= p
    if (4 * a**3 + 27 * b * b) % p == 0:
        raise ValueError("the curve is singular")

    psi = _division_polynomial(ell, a, b, p)
    factors = _factor_squarefree(psi, p)
    target_degree = (ell - 1) // 2
    kernels = []
    for h in _candidate_divisors(factors, target_degree, p):
        quotient, remainder = _divmod_poly(psi, h, p)
        if remainder or not quotient:
            raise ArithmeticError("candidate kernel does not divide psi_ell")
        if not _kernel_is_cyclic(h, a, b, ell, p):
            continue

        multiples = _small_multiples(h, a, b, ell, p)
        generic_x = _mod((0, 1), h, p)
        curve_rhs = _mod((b, a, 0, 1), h, p)
        frobenius_x = _powmod(generic_x, p, h, p)
        frobenius_y = _powmod(curve_rhs, (p - 1) // 2, h, p)
        matches = [
            scalar
            for scalar, point in enumerate(multiples, 1)
            if point.x == frobenius_x and point.y_multiplier == frobenius_y
        ]
        if len(matches) != 1:
            raise ArithmeticError(
                f"Frobenius eigenvalue was not unique for ell={ell}: {matches}"
            )
        eigenvalue = matches[0]
        trace_residue = (eigenvalue + (p % ell) * pow(eigenvalue, -1, ell)) % ell
        codomain_a, codomain_b = _velu_codomain(h, a, b, p)
        if (4 * codomain_a**3 + 27 * codomain_b * codomain_b) % p == 0:
            raise ArithmeticError("Velu codomain is singular")
        kernels.append(
            GeneralElkiesKernel(
                ell, h, codomain_a, codomain_b, eigenvalue, trace_residue
            )
        )

    kernels.sort(key=lambda kernel: kernel.polynomial)
    if kernels and len({kernel.trace_residue for kernel in kernels}) != 1:
        raise ArithmeticError("stable kernels imply inconsistent trace residues")
    return tuple(kernels)


def trace_mod_ell(p: int, a: int, b: int, ell: int) -> int:
    kernels = elkies_kernels(p, a, b, ell)
    if not kernels:
        raise NotElkiesError(f"the curve is Atkin at ell={ell}")
    return kernels[0].trace_residue


__all__ = [
    "GeneralElkiesKernel",
    "NotElkiesError",
    "elkies_kernels",
    "trace_mod_ell",
]
