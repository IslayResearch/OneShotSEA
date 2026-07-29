"""Differential and invariant tests for the CAS-free level-3 Elkies path."""

from __future__ import annotations

import random
import unittest

if __package__:
    from .elkies import NotElkiesError, elkies_kernels, trace_mod_ell
    from .schoof import trace_mod_ell as schoof_trace_mod_ell
else:
    from elkies import NotElkiesError, elkies_kernels, trace_mod_ell
    from schoof import trace_mod_ell as schoof_trace_mod_ell


def _legendre(value: int, p: int) -> int:
    value %= p
    if value == 0:
        return 0
    symbol = pow(value, (p - 1) // 2, p)
    return 1 if symbol == 1 else -1


class ElkiesReferenceTest(unittest.TestCase):
    def test_many_small_curves_against_schoof(self) -> None:
        rng = random.Random(0xE1C1E5)
        elkies_cases = 0
        atkin_cases = 0
        eigenvalues = set()
        checked = 0

        for p in (5, 7, 11, 13, 17, 19, 23, 29, 31, 41, 53, 73, 101):
            curves = {(0, 1), (1, 0)}  # force j=0 and j=1728 coverage
            while len(curves) < 14:
                curves.add((rng.randrange(p), rng.randrange(p)))
            for a, b in sorted(curves):
                if (4 * a**3 + 27 * b**2) % p == 0:
                    continue
                expected = schoof_trace_mod_ell(p, a, b, 3)
                discriminant_symbol = _legendre(expected * expected - 4 * p, 3)
                kernels = elkies_kernels(p, a, b)
                # Kernel existence is the definitive test even at j=0,1728,
                # where the coarse modular polynomial can have twist-ambiguous
                # rational roots.
                self.assertEqual(bool(kernels), discriminant_symbol >= 0)
                checked += 1

                if not kernels:
                    atkin_cases += 1
                    with self.assertRaises(NotElkiesError):
                        trace_mod_ell(p, a, b)
                    continue

                result = trace_mod_ell(p, a, b)
                self.assertEqual(result.trace_residue, expected)
                self.assertIn(result.eigenvalue, (1, 2))
                self.assertEqual(result.kernel.polynomial, (-result.kernel.root % p, 1))
                for kernel in kernels:
                    self.assertEqual(len(kernel.polynomial) - 1, 1)
                    self.assertEqual(kernel.polynomial[-1], 1)
                eigenvalues.add(result.eigenvalue)
                elkies_cases += 1

        self.assertGreaterEqual(checked, 160)
        self.assertGreaterEqual(elkies_cases, 60)
        self.assertGreaterEqual(atkin_cases, 40)
        self.assertEqual(eigenvalues, {1, 2})

    def test_all_curves_over_two_tiny_fields(self) -> None:
        checked = 0
        for p in (5, 7):
            for a in range(p):
                for b in range(p):
                    if (4 * a**3 + 27 * b**2) % p == 0:
                        continue
                    expected = schoof_trace_mod_ell(p, a, b, 3)
                    kernels = elkies_kernels(p, a, b)
                    if kernels:
                        self.assertEqual(trace_mod_ell(p, a, b).trace_residue, expected)
                    checked += 1
        self.assertEqual(checked, 62)

    def test_input_and_level_validation(self) -> None:
        with self.assertRaisesRegex(ValueError, "p must"):
            elkies_kernels(15, 1, 1)
        with self.assertRaisesRegex(ValueError, "singular"):
            elkies_kernels(7, 0, 0)
        with self.assertRaisesRegex(NotImplementedError, "BMSS"):
            elkies_kernels(101, 2, 3, 5)


if __name__ == "__main__":
    unittest.main()
