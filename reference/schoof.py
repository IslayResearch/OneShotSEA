"""A small, pure-Python reference implementation of Schoof's algorithm.

This module is deliberately independent of the production SEA code.  It is
intended to answer one question reliably for differential tests::

    what is the Frobenius trace modulo a small odd prime ell?

The implementation works in

    F_p[x, y] / (psi_ell(x), y^2 - (x^3 + a*x + b))

and checks Schoof's characteristic equation on the generic ell-torsion
point.  Jacobian coordinates avoid inversions in this (usually non-field)
ring.  Nothing here enumerates the points of the curve.

The code favours explicit formulas and auditability over speed.  In
particular, its polynomial arithmetic consists only of Python integer lists.
It is suitable for small ell values used by tests, not as a production SEA
engine.
"""

from __future__ import annotations

from dataclasses import dataclass
from functools import lru_cache


Poly = tuple[int, ...]  # coefficient order: constant term first


def _trim(values: list[int] | tuple[int, ...], p: int) -> Poly:
    result = [value % p for value in values]
    while result and result[-1] == 0:
        result.pop()
    return tuple(result)


def _add(left: Poly, right: Poly, p: int) -> Poly:
    size = max(len(left), len(right))
    return _trim(
        [
            (left[i] if i < len(left) else 0)
            + (right[i] if i < len(right) else 0)
            for i in range(size)
        ],
        p,
    )


def _neg(value: Poly, p: int) -> Poly:
    return _trim([-coefficient for coefficient in value], p)


def _sub(left: Poly, right: Poly, p: int) -> Poly:
    return _add(left, _neg(right, p), p)


def _scale(value: Poly, scalar: int, p: int) -> Poly:
    return _trim([scalar * coefficient for coefficient in value], p)


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
    remainder = list(numerator)
    quotient = [0] * max(1, len(numerator) - len(denominator) + 1)
    inverse_lead = pow(denominator[-1], -1, p)
    while remainder and len(remainder) >= len(denominator):
        degree = len(remainder) - len(denominator)
        coefficient = remainder[-1] * inverse_lead % p
        quotient[degree] = coefficient
        for i, denominator_coefficient in enumerate(denominator):
            remainder[degree + i] = (
                remainder[degree + i] - coefficient * denominator_coefficient
            ) % p
        while remainder and remainder[-1] == 0:
            remainder.pop()
    return _trim(quotient, p), _trim(remainder, p)


def _exact_div(numerator: Poly, denominator: Poly, p: int) -> Poly:
    quotient, remainder = _divmod_poly(numerator, denominator, p)
    if remainder:
        raise ArithmeticError("division-polynomial recurrence was not exact")
    return quotient


def _mod(value: Poly, modulus: Poly, p: int) -> Poly:
    return _divmod_poly(value, modulus, p)[1]


RawElement = tuple[Poly, Poly]  # u(x) + y*v(x), before reducing by psi_ell


def _raw_add(left: RawElement, right: RawElement, p: int) -> RawElement:
    return _add(left[0], right[0], p), _add(left[1], right[1], p)


def _raw_sub(left: RawElement, right: RawElement, p: int) -> RawElement:
    return _sub(left[0], right[0], p), _sub(left[1], right[1], p)


def _raw_mul(left: RawElement, right: RawElement, f: Poly, p: int) -> RawElement:
    # (u + y*v)(r + y*s) = ur + f*vs + y*(us + vr).
    return (
        _add(_mul(left[0], right[0], p), _mul(f, _mul(left[1], right[1], p), p), p),
        _add(_mul(left[0], right[1], p), _mul(left[1], right[0], p), p),
    )


def _raw_square(value: RawElement, f: Poly, p: int) -> RawElement:
    return _raw_mul(value, value, f, p)


def _division_polynomial(ell: int, a: int, b: int, p: int) -> Poly:
    """Return the odd division polynomial psi_ell in F_p[x]."""

    a %= p
    b %= p
    f = _trim((b, a, 0, 1), p)

    @lru_cache(maxsize=None)
    def psi(index: int) -> RawElement:
        if index == 0:
            return (), ()
        if index == 1:
            return (1,), ()
        if index == 2:
            return (), (2 % p,)
        if index == 3:
            return _trim((-a * a, 12 * b, 6 * a, 0, 3), p), ()
        if index == 4:
            inside = _trim(
                (
                    -a**3 - 8 * b * b,
                    -4 * a * b,
                    -5 * a * a,
                    20 * b,
                    5 * a,
                    0,
                    1,
                ),
                p,
            )
            return (), _scale(inside, 4, p)

        if index % 2 == 1:
            m = (index - 1) // 2
            first = _raw_mul(
                psi(m + 2),
                _raw_mul(_raw_square(psi(m), f, p), psi(m), f, p),
                f,
                p,
            )
            second = _raw_mul(
                psi(m - 1),
                _raw_mul(_raw_square(psi(m + 1), f, p), psi(m + 1), f, p),
                f,
                p,
            )
            return _raw_sub(first, second, p)

        m = index // 2
        bracket = _raw_sub(
            _raw_mul(psi(m + 2), _raw_square(psi(m - 1), f, p), f, p),
            _raw_mul(psi(m - 2), _raw_square(psi(m + 1), f, p), f, p),
            p,
        )
        numerator = _raw_mul(psi(m), bracket, f, p)
        if numerator[1]:
            raise ArithmeticError("unexpected y term in even division polynomial")
        # numerator/(2*y) = y * numerator/(2*f).
        quotient = _exact_div(numerator[0], f, p)
        return (), _scale(quotient, pow(2, -1, p), p)

    result = psi(ell)
    if result[1]:
        raise ArithmeticError("odd division polynomial unexpectedly contains y")
    if not result[0]:
        raise ArithmeticError("division polynomial vanished modulo p")
    return _scale(result[0], pow(result[0][-1], -1, p), p)


class _QuotientRing:
    """The quadratic algebra F_p[x,y]/(psi_ell, y^2-f)."""

    def __init__(self, p: int, modulus: Poly, f: Poly):
        self.p = p
        self.modulus = modulus
        self.f = _mod(f, modulus, p)

    def element(self, u: Poly = (), v: Poly = ()) -> "_Element":
        return _Element(self, _mod(u, self.modulus, self.p), _mod(v, self.modulus, self.p))

    def constant(self, value: int) -> "_Element":
        return self.element((value % self.p,))


@dataclass(frozen=True)
class _Element:
    ring: _QuotientRing
    u: Poly
    v: Poly

    def __add__(self, other: "_Element") -> "_Element":
        self._same_ring(other)
        return self.ring.element(
            _add(self.u, other.u, self.ring.p),
            _add(self.v, other.v, self.ring.p),
        )

    def __neg__(self) -> "_Element":
        return self.ring.element(_neg(self.u, self.ring.p), _neg(self.v, self.ring.p))

    def __sub__(self, other: "_Element") -> "_Element":
        return self + (-other)

    def __mul__(self, other: "_Element") -> "_Element":
        self._same_ring(other)
        u, v = _raw_mul(
            (self.u, self.v),
            (other.u, other.v),
            self.ring.f,
            self.ring.p,
        )
        return self.ring.element(u, v)

    def square(self) -> "_Element":
        return self * self

    def pow(self, exponent: int) -> "_Element":
        if exponent < 0:
            raise ValueError("negative exponent in non-field quotient ring")
        result = self.ring.constant(1)
        base = self
        while exponent:
            if exponent & 1:
                result = result * base
            base = base.square()
            exponent >>= 1
        return result

    def scale(self, scalar: int) -> "_Element":
        return self.ring.element(
            _scale(self.u, scalar, self.ring.p),
            _scale(self.v, scalar, self.ring.p),
        )

    def is_zero(self) -> bool:
        return not self.u and not self.v

    def _same_ring(self, other: "_Element") -> None:
        if self.ring is not other.ring:
            raise TypeError("elements belong to different quotient rings")


@dataclass(frozen=True)
class _JacobianPoint:
    """A point (X:Y:Z), with affine coordinates (X/Z^2,Y/Z^3)."""

    x: _Element
    y: _Element
    z: _Element

    @property
    def ring(self) -> _QuotientRing:
        return self.x.ring

    def is_infinity(self) -> bool:
        return self.z.is_zero()


def _infinity(ring: _QuotientRing) -> _JacobianPoint:
    return _JacobianPoint(ring.constant(0), ring.constant(1), ring.constant(0))


def _double(point: _JacobianPoint, a: int) -> _JacobianPoint:
    if point.is_infinity() or point.y.is_zero():
        return _infinity(point.ring)
    xx = point.x.square()
    yy = point.y.square()
    yyyy = yy.square()
    zz = point.z.square()
    s = ((point.x + yy).square() - xx - yyyy).scale(2)
    m = xx.scale(3) + zz.square().scale(a)
    t = m.square() - s.scale(2)
    return _JacobianPoint(
        t,
        m * (s - t) - yyyy.scale(8),
        (point.y + point.z).square() - yy - zz,
    )


def _add_points(left: _JacobianPoint, right: _JacobianPoint, a: int) -> _JacobianPoint:
    if left.is_infinity():
        return right
    if right.is_infinity():
        return left

    z1z1 = left.z.square()
    z2z2 = right.z.square()
    u1 = left.x * z2z2
    u2 = right.x * z1z1
    s1 = left.y * right.z * z2z2
    s2 = right.y * left.z * z1z1
    if u1 == u2:
        if s1 == s2:
            return _double(left, a)
        if s1 == -s2:
            return _infinity(left.ring)

    h = u2 - u1
    i = h.scale(2).square()
    j = h * i
    r = (s2 - s1).scale(2)
    v = u1 * i
    return _JacobianPoint(
        r.square() - j - v.scale(2),
        r * (v - (r.square() - j - v.scale(2))) - (s1 * j).scale(2),
        ((left.z + right.z).square() - z1z1 - z2z2) * h,
    )


def _scalar_multiply(scalar: int, point: _JacobianPoint, a: int) -> _JacobianPoint:
    if scalar < 0:
        return _scalar_multiply(-scalar, _JacobianPoint(point.x, -point.y, point.z), a)
    result = _infinity(point.ring)
    addend = point
    while scalar:
        if scalar & 1:
            result = _add_points(result, addend, a)
        addend = _double(addend, a)
        scalar >>= 1
    return result


def _same_point(left: _JacobianPoint, right: _JacobianPoint) -> bool:
    if left.is_infinity() or right.is_infinity():
        return left.is_infinity() and right.is_infinity()
    left_z2 = left.z.square()
    right_z2 = right.z.square()
    return (
        left.x * right_z2 == right.x * left_z2
        and left.y * right_z2 * right.z == right.y * left_z2 * left.z
    )


def _is_prime(value: int) -> bool:
    if value < 2:
        return False
    small_primes = (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37)
    for prime in small_primes:
        if value % prime == 0:
            return value == prime

    # Deterministic Miller--Rabin bases for 64-bit inputs.  For larger
    # reference inputs the small-prime bases are a strong probable-prime
    # screen; primality of p remains an API precondition, rather than making
    # this trace oracle contain a second primality prover.  Unlike trial
    # division, this check remains usable when differential tests use the
    # several-hundred-bit primes targeted by the project.
    if value < 1 << 64:
        bases = (2, 325, 9375, 28178, 450775, 9780504, 1795265022)
    else:
        bases = small_primes
    odd_part = value - 1
    power_of_two = 0
    while odd_part % 2 == 0:
        odd_part //= 2
        power_of_two += 1
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


def trace_mod_ell(p: int, a: int, b: int, ell: int) -> int:
    """Compute the Frobenius trace of ``y^2=x^3+a*x+b`` modulo ``ell``.

    ``p`` and ``ell`` must be distinct odd primes, and the curve must be
    nonsingular over F_p.  The returned integer is in ``range(ell)``.

    This is the original Schoof characteristic-equation test, not SEA: every
    residue is tried on the generic ell-torsion point.  It therefore makes a
    useful implementation-independent oracle for small ell.
    """

    if not _is_prime(p) or p <= 3:
        raise ValueError("p must be an odd prime greater than 3")
    if not _is_prime(ell) or ell == 2:
        raise ValueError("ell must be an odd prime")
    if ell == p:
        raise ValueError("ell must differ from p")
    a %= p
    b %= p
    if (4 * pow(a, 3, p) + 27 * pow(b, 2, p)) % p == 0:
        raise ValueError("the curve is singular")

    psi_ell = _division_polynomial(ell, a, b, p)
    f = _trim((b, a, 0, 1), p)
    ring = _QuotientRing(p, psi_ell, f)
    x = ring.element((0, 1))
    y = ring.element((), (1,))
    one = ring.constant(1)
    generic = _JacobianPoint(x, y, one)

    # Frobenius sends (x,y) to (x^p, y*f^((p-1)/2)).  Applying it twice
    # gives the same formula with p^2.  Exponentiation is always reduced in
    # the quotient ring, so even the p^2 exponent needs only O(log p) steps.
    f_element = ring.element(f)
    frobenius = _JacobianPoint(
        x.pow(p), y * f_element.pow((p - 1) // 2), one
    )
    p_squared = p * p
    frobenius_squared = _JacobianPoint(
        x.pow(p_squared),
        y * f_element.pow((p_squared - 1) // 2),
        one,
    )

    # Since generic is ell-torsion, [p]generic=[p mod ell]generic.
    left = _add_points(
        frobenius_squared,
        _scalar_multiply(p % ell, generic, a),
        a,
    )
    matches = [
        residue
        for residue in range(ell)
        if _same_point(left, _scalar_multiply(residue, frobenius, a))
    ]
    if len(matches) != 1:
        raise ArithmeticError(
            f"Schoof residue test found {len(matches)} candidates: {matches}"
        )
    return matches[0]


__all__ = ["trace_mod_ell"]
