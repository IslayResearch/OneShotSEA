"""CAS-free reference implementation of the first exact Elkies residue path.

The production implementation will use fast codomain and kernel recovery.  This
module deliberately starts with the smallest odd level for which the repository
contains a classical modular polynomial, ell=3.  At this level a cyclic kernel
has polynomial ``x-r``.  Its roots can therefore be recovered independently as
the rational roots of the third division polynomial, then validated against a
root of ``Phi_3(j(E), Y)`` using Velu's codomain formulas.

No point counting or Schoof characteristic-equation computation is used here.
Polynomial factorisation is a tiny deterministic-seeded Cantor--Zassenhaus
implementation over F_p, sufficient for the degree-four polynomials involved.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
from pathlib import Path
from typing import Iterable


Poly = tuple[int, ...]  # coefficients in increasing degree order
_DEFAULT_PHI3 = Path(__file__).resolve().parents[1] / "data/modpoly/j/phi_3.txt"


class NotElkiesError(ValueError):
    """Raised when Phi_ell(j(E),Y) has no F_p-rational isogeny root."""


@dataclass(frozen=True)
class ElkiesKernel:
    polynomial: Poly
    root: int
    codomain_a: int
    codomain_b: int
    neighbor_j: int


@dataclass(frozen=True)
class ElkiesResult:
    ell: int
    trace_residue: int
    eigenvalue: int
    kernel: ElkiesKernel


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


def _mul(left: Poly, right: Poly, p: int) -> Poly:
    if not left or not right:
        return ()
    output = [0] * (len(left) + len(right) - 1)
    for i, left_coefficient in enumerate(left):
        for j, right_coefficient in enumerate(right):
            output[i + j] += left_coefficient * right_coefficient
    return _trim(output, p)


def _scale(value: Poly, scalar: int, p: int) -> Poly:
    return _trim((scalar * coefficient for coefficient in value), p)


def _divmod_poly(numerator: Poly, denominator: Poly, p: int) -> tuple[Poly, Poly]:
    if not denominator:
        raise ZeroDivisionError("polynomial division by zero")
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


def _factor_seed(p: int, polynomial: Poly, counter: int) -> Poly:
    """Return a deterministic pseudorandom polynomial of degree below f."""

    degree = len(polynomial) - 1
    coefficients = []
    for index in range(degree):
        digest = hashlib.sha256(
            f"elkies-factor:{p}:{polynomial}:{counter}:{index}".encode("ascii")
        ).digest()
        coefficients.append(int.from_bytes(digest, "big") % p)
    return _trim(coefficients, p)


def _split_linear_factors(value: Poly, p: int) -> list[Poly]:
    value = _monic(value, p)
    degree = len(value) - 1
    if degree <= 0:
        return []
    if degree == 1:
        return [value]

    # The caller has already intersected with x^p-x, so every irreducible
    # factor is linear.  A quadratic-character split is Cantor--Zassenhaus.
    one = (1,)
    for counter in range(512):
        candidate = _factor_seed(p, value, counter)
        if not candidate:
            continue
        partition = _sub(_powmod(candidate, (p - 1) // 2, value, p), one, p)
        divisor = _gcd(value, partition, p)
        divisor_degree = len(divisor) - 1
        if 0 < divisor_degree < degree:
            quotient, remainder = _divmod_poly(value, divisor, p)
            if remainder:
                raise ArithmeticError("internal polynomial split was not exact")
            return _split_linear_factors(divisor, p) + _split_linear_factors(
                quotient, p
            )

    # This is reachable only after an extraordinarily unlikely run of failed
    # deterministic partitions.  Exhaustion is still useful for tiny test
    # fields and gives a definite answer rather than silently misclassifying.
    if p <= 1_000_000:
        roots = [x for x in range(p) if _evaluate(value, x, p) == 0]
        if len(roots) == degree:
            return [_trim((-root, 1), p) for root in roots]
    raise ArithmeticError("failed to split a known product of linear factors")


def _rational_roots(value: Poly, p: int) -> list[int]:
    value = _monic(value, p)
    if len(value) <= 1:
        return []
    x = (0, 1)
    rational_part = _gcd(value, _sub(_powmod(x, p, value, p), x, p), p)
    factors = _split_linear_factors(rational_part, p)
    roots = {(-factor[0] * pow(factor[1], -1, p)) % p for factor in factors}
    return sorted(roots)


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


def _j_invariant(a: int, b: int, p: int) -> int:
    four_a_cubed = 4 * pow(a, 3, p) % p
    denominator = (four_a_cubed + 27 * b * b) % p
    if denominator == 0:
        raise ValueError("the curve is singular")
    return 1728 * four_a_cubed * pow(denominator, -1, p) % p


def _load_modular_polynomial(path: str | Path) -> tuple[tuple[int, int, int], ...]:
    terms = []
    for line_number, raw_line in enumerate(Path(path).read_text().splitlines(), 1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = line.split()
        if len(fields) != 3:
            raise ValueError(f"malformed modular polynomial line {line_number}")
        x_degree, y_degree, coefficient = map(int, fields)
        terms.append((x_degree, y_degree, coefficient))
    if not terms:
        raise ValueError("empty modular polynomial")
    return tuple(terms)


def _specialize_x(
    terms: tuple[tuple[int, int, int], ...], x_value: int, p: int
) -> Poly:
    maximum_y = max(y_degree for _, y_degree, _ in terms)
    coefficients = [0] * (maximum_y + 1)
    for x_degree, y_degree, coefficient in terms:
        coefficients[y_degree] += coefficient * pow(x_value, x_degree, p)
    return _trim(coefficients, p)


def _phi_evaluate(
    terms: tuple[tuple[int, int, int], ...], x_value: int, y_value: int, p: int
) -> int:
    return sum(
        coefficient * pow(x_value, x_degree, p) * pow(y_value, y_degree, p)
        for x_degree, y_degree, coefficient in terms
    ) % p


def _psi3(a: int, b: int, p: int) -> Poly:
    # psi_3 = 3*x^4 + 6*a*x^2 + 12*b*x - a^2.
    return _trim((-a * a, 12 * b, 6 * a, 0, 3), p)


def _velu_codomain_3(a: int, b: int, root: int, p: int) -> tuple[int, int]:
    """Normalized short-Weierstrass codomain for ker(x-root)."""

    codomain_a = -9 * a - 30 * root * root
    codomain_b = -27 * b - 42 * a * root - 70 * root**3
    return codomain_a % p, codomain_b % p


def elkies_kernels(
    p: int,
    a: int,
    b: int,
    ell: int = 3,
    modular_polynomial: str | Path | None = None,
) -> tuple[ElkiesKernel, ...]:
    """Construct and validate every rational level-3 kernel of ``E/F_p``.

    A level is Atkin exactly when the returned tuple is empty.  Classical-j
    exceptional cases may return more than two kernels or several kernels with
    the same neighbor invariant; each kernel is retained and checked.
    """

    if not _is_prime(p) or p <= 3:
        raise ValueError("p must be an odd prime greater than 3")
    if ell != 3:
        raise NotImplementedError(
            "the CAS-free bootstrap currently supports ell=3; higher odd levels "
            "need BMSS kernel reconstruction and corresponding modular-polynomial data"
        )
    if p == ell:
        raise ValueError("ell must differ from p")
    a %= p
    b %= p
    source_j = _j_invariant(a, b, p)
    terms = _load_modular_polynomial(modular_polynomial or _DEFAULT_PHI3)
    specialized = _specialize_x(terms, source_j, p)
    neighbor_roots = set(_rational_roots(specialized, p))

    kernels = []
    for root in _rational_roots(_psi3(a, b, p), p):
        kernel = _trim((-root, 1), p)
        if len(kernel) - 1 != (ell - 1) // 2 or kernel[-1] != 1:
            raise ArithmeticError("kernel polynomial has the wrong degree or normalization")
        if _evaluate(_psi3(a, b, p), root, p) != 0:
            raise ArithmeticError("kernel does not divide psi_3")
        codomain_a, codomain_b = _velu_codomain_3(a, b, root, p)
        neighbor_j = _j_invariant(codomain_a, codomain_b, p)
        if neighbor_j not in neighbor_roots or _phi_evaluate(
            terms, source_j, neighbor_j, p
        ):
            raise ArithmeticError("Velu codomain is not the selected modular neighbor")
        kernels.append(
            ElkiesKernel(kernel, root, codomain_a, codomain_b, neighbor_j)
        )

    # A root of the coarse classical-j polynomial need not identify one stable
    # kernel when distinct geometric isogenies collide at the same neighbor.
    # This occurs at j=0/1728 and can also occur on the supersingular graph (for
    # example p=19, E:[8,14], j=7, t=0).  Thus actual rational kernel factors
    # are authoritative.  Every constructed kernel was nevertheless required
    # above to land on a specialized-Phi_3 root.
    return tuple(kernels)


def trace_mod_ell(
    p: int,
    a: int,
    b: int,
    ell: int = 3,
    modular_polynomial: str | Path | None = None,
) -> ElkiesResult:
    """Return one exact Elkies trace residue and its validated kernel witness."""

    kernels = elkies_kernels(p, a, b, ell, modular_polynomial)
    if not kernels:
        raise NotElkiesError(f"the curve is Atkin at ell={ell}")

    kernel = kernels[0]
    # In F_p[x]/(x-r), Frobenius fixes x and sends y to
    # (r^3+a*r+b)^((p-1)/2)*y.  The multiplier is precisely lambda in F_3^*.
    a %= p
    b %= p
    curve_rhs = (kernel.root**3 + a * kernel.root + b) % p
    if curve_rhs == 0:
        raise ArithmeticError("odd-order kernel unexpectedly intersects 2-torsion")
    y_multiplier = pow(curve_rhs, (p - 1) // 2, p)
    if y_multiplier == 1:
        eigenvalue = 1
    elif y_multiplier == p - 1:
        eigenvalue = ell - 1
    else:
        raise ArithmeticError("Frobenius multiplier is not a sign")

    # For ell=3, [1]P=P and [2]P=-P, so the preceding y comparison validates
    # pi(P)=[lambda]P in both coordinates.  The characteristic polynomial then
    # gives t = lambda + p/lambda modulo ell.
    trace_residue = (eigenvalue + (p % ell) * pow(eigenvalue, -1, ell)) % ell
    return ElkiesResult(ell, trace_residue, eigenvalue, kernel)


__all__ = [
    "ElkiesKernel",
    "ElkiesResult",
    "NotElkiesError",
    "elkies_kernels",
    "trace_mod_ell",
]
