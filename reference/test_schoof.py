"""Differential tests for the pure-Python reference Schoof implementation."""

from __future__ import annotations

import random
import unittest

if __package__:
    from .schoof import trace_mod_ell
else:
    from schoof import trace_mod_ell


def _brute_force_trace(p: int, a: int, b: int) -> int:
    """Test-only point enumeration, independent of the Schoof code."""

    points = 1  # point at infinity
    for x in range(p):
        rhs = (x**3 + a * x + b) % p
        points += sum(1 for y in range(p) if y * y % p == rhs)
    return p + 1 - points


class SchoofReferenceTest(unittest.TestCase):
    def test_differential_many_small_curves(self) -> None:
        rng = random.Random(0x5C400F)
        cases = []
        for p in (5, 7, 11, 13, 17, 19, 23, 29, 31):
            curves = set()
            while len(curves) < 8:
                a = rng.randrange(p)
                b = rng.randrange(p)
                if (4 * a**3 + 27 * b**2) % p:
                    curves.add((a, b))
            for a, b in sorted(curves):
                trace = _brute_force_trace(p, a, b)
                for ell in (3, 5, 7):
                    if ell != p:
                        cases.append((p, a, b, ell, trace))

        # This deliberately exercises well over one hundred independently
        # enumerated (curve, ell) combinations.
        self.assertGreaterEqual(len(cases), 150)
        for p, a, b, ell, trace in cases:
            with self.subTest(p=p, a=a, b=b, ell=ell):
                self.assertEqual(trace_mod_ell(p, a, b, ell), trace % ell)

    def test_larger_small_ell(self) -> None:
        # ell=11 is slower, so use a compact set that still hits several
        # Frobenius conjugacy classes.
        for p, a, b in ((13, 1, 1), (17, 2, 3), (19, 0, 2), (23, 5, 7)):
            trace = _brute_force_trace(p, a, b)
            self.assertEqual(trace_mod_ell(p, a, b, 11), trace % 11)

    def test_input_validation(self) -> None:
        with self.assertRaisesRegex(ValueError, "p must"):
            trace_mod_ell(15, 1, 1, 3)
        with self.assertRaisesRegex(ValueError, "ell must"):
            trace_mod_ell(11, 1, 1, 9)
        with self.assertRaisesRegex(ValueError, "differ"):
            trace_mod_ell(5, 1, 1, 5)
        with self.assertRaisesRegex(ValueError, "singular"):
            trace_mod_ell(7, 0, 0, 3)


if __name__ == "__main__":
    unittest.main()
