"""Differential tests for the kernel-first exact Elkies levels 5 and 7."""

from __future__ import annotations

import unittest

if __package__:
    from .elkies_general import NotElkiesError, elkies_kernels, trace_mod_ell
    from .schoof import trace_mod_ell as schoof_trace_mod_ell
else:
    from elkies_general import NotElkiesError, elkies_kernels, trace_mod_ell
    from schoof import trace_mod_ell as schoof_trace_mod_ell


def _point_count(p: int, a: int, b: int) -> int:
    count = 1
    for x in range(p):
        rhs = (x**3 + a * x + b) % p
        count += 1 if rhs == 0 else 2 if pow(rhs, (p - 1) // 2, p) == 1 else 0
    return count


def _legendre(value: int, ell: int) -> int:
    value %= ell
    if value == 0:
        return 0
    return 1 if pow(value, (ell - 1) // 2, ell) == 1 else -1


class GeneralElkiesReferenceTest(unittest.TestCase):
    def _check_exhaustive(self, ell: int, primes: tuple[int, ...]) -> tuple[int, int, int]:
        checked = elkies = atkin = 0
        for p in primes:
            if p == ell:
                continue
            for a in range(p):
                for b in range(p):
                    if (4 * a**3 + 27 * b**2) % p == 0:
                        continue
                    residue = schoof_trace_mod_ell(p, a, b, ell)
                    expected_elkies = _legendre(residue * residue - 4 * p, ell) >= 0
                    kernels = elkies_kernels(p, a, b, ell)
                    self.assertEqual(bool(kernels), expected_elkies, (p, a, b, residue))
                    checked += 1
                    if not kernels:
                        atkin += 1
                        with self.assertRaises(NotElkiesError):
                            trace_mod_ell(p, a, b, ell)
                        continue
                    elkies += 1
                    self.assertEqual(trace_mod_ell(p, a, b, ell), residue)
                    source_order = _point_count(p, a, b)
                    for kernel in kernels:
                        self.assertEqual(len(kernel.polynomial) - 1, (ell - 1) // 2)
                        self.assertEqual(kernel.polynomial[-1], 1)
                        self.assertEqual(kernel.trace_residue, residue)
                        self.assertEqual(
                            _point_count(p, kernel.codomain_a, kernel.codomain_b),
                            source_order,
                            (p, a, b, kernel),
                        )
        return checked, elkies, atkin

    def test_level_5_all_curves(self) -> None:
        checked, elkies, atkin = self._check_exhaustive(5, (7, 11, 13))
        self.assertEqual(checked, 308)
        self.assertGreater(elkies, 80)
        self.assertGreater(atkin, 80)

    def test_level_7_all_curves(self) -> None:
        checked, elkies, atkin = self._check_exhaustive(7, (5, 11))
        self.assertEqual(checked, 130)
        self.assertGreater(elkies, 30)
        self.assertGreater(atkin, 30)

    def test_scalar_frobenius_recovers_every_kernel(self) -> None:
        # These fixtures have scalar Frobenius on E[ell], so all ell+1 cyclic
        # subgroups are rational.  They exercise the worst factor/subset shape.
        for p, a, b, ell in ((19, 0, 4, 5), (37, 0, 3, 7)):
            kernels = elkies_kernels(p, a, b, ell)
            residue = schoof_trace_mod_ell(p, a, b, ell)
            source_order = _point_count(p, a, b)
            self.assertEqual(len(kernels), ell + 1)
            self.assertEqual({kernel.trace_residue for kernel in kernels}, {residue})
            for kernel in kernels:
                self.assertEqual(
                    _point_count(p, kernel.codomain_a, kernel.codomain_b), source_order
                )

    def test_416_bit_target_field(self) -> None:
        p = int(
            "10000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "0000000000000000000000000000000000000000000237"
        )
        for ell in (5, 7):
            kernels = elkies_kernels(p, 2, 3, ell)
            self.assertTrue(kernels)
            self.assertEqual(
                {kernel.trace_residue for kernel in kernels},
                {schoof_trace_mod_ell(p, 2, 3, ell)},
            )

    def test_input_validation(self) -> None:
        with self.assertRaisesRegex(ValueError, "p must"):
            elkies_kernels(15, 1, 1, 5)
        with self.assertRaisesRegex(ValueError, "singular"):
            elkies_kernels(11, 0, 0, 5)
        with self.assertRaisesRegex(ValueError, "differ"):
            elkies_kernels(5, 1, 1, 5)
        with self.assertRaises(NotImplementedError):
            elkies_kernels(11, 1, 1, 3)


if __name__ == "__main__":
    unittest.main()
