#!/usr/bin/env python3
"""Integration tests for the independent Magma point-counting oracle."""

from __future__ import annotations

import argparse
import sys
import unittest

import point_count


MAGMA: str | None = None


def brute_force_order(p: int, a: int, b: int) -> int:
    """Simple definition-level count, used only to audit the test fixtures."""
    squares = {y * y % p for y in range(p)}
    count = 1  # the point at infinity
    for x in range(p):
        rhs = (x * x * x + a * x + b) % p
        if rhs == 0:
            count += 1
        elif rhs in squares:
            count += 2
    return count


class PointCountOracleTests(unittest.TestCase):
    # These cover j=0, j=1728/supersingular, an ordinary curve, coefficient
    # reduction, and a medium-size field.  Expected values are fixed rather
    # than learned from Magma; brute_force_order separately audits them.
    CASES = (
        (5, 0, 1, 6, 0),
        (7, 1, 0, 8, 0),
        (11, 1, 6, 13, -1),
        (97, 2, 3, 100, -2),
        (1009, -3, 5, 1067, -57),
        (65537, 2, 3, 65386, 152),
    )

    def test_known_curves(self) -> None:
        assert MAGMA is not None
        for p, a, b, order, trace in self.CASES:
            with self.subTest(p=p, a=a, b=b):
                self.assertEqual(brute_force_order(p, a, b), order)
                result = point_count.count_curve(MAGMA, p, a, b)
                self.assertEqual(result["order"], order)
                self.assertEqual(result["trace"], trace)
                self.assertEqual(result["a"], a % p)
                self.assertEqual(result["b"], b % p)

    def test_rejects_singular_curve(self) -> None:
        assert MAGMA is not None
        with self.assertRaisesRegex(RuntimeError, "singular"):
            point_count.count_curve(MAGMA, 5, 0, 0)

    def test_rejects_composite_modulus(self) -> None:
        assert MAGMA is not None
        with self.assertRaisesRegex(RuntimeError, "prime greater than 3"):
            point_count.count_curve(MAGMA, 15, 1, 1)


def parse_args(argv: list[str]) -> tuple[argparse.Namespace, list[str]]:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--magma", metavar="PATH")
    return parser.parse_known_args(argv)


if __name__ == "__main__":
    options, unittest_args = parse_args(sys.argv[1:])
    try:
        MAGMA = point_count.resolve_magma(options.magma)
    except RuntimeError as exc:
        print(f"oracle tests: {exc}", file=sys.stderr)
        raise SystemExit(2)
    unittest.main(argv=[sys.argv[0], *unittest_args])
