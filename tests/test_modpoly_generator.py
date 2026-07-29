#!/usr/bin/env python3
"""Tests for the exact classical modular-polynomial generator."""

from __future__ import annotations

from hashlib import sha256
import json
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import generate_classical_modpoly as generator  # noqa: E402


EXPECTED_DIGESTS = {
    2: "c4febfd79090d71431db6ef665b7b7aed89640179bd0ae05ce684baf9a4b78c2",
    3: "dc9f810a0d6830e557e44c15e5355d24caaef4d54b748c806d2c9eb2cf23ea60",
    5: "b18e44299e31a71fb8e565bef48454a79e27cfba1e98bda95d6ec03f4555cca2",
    7: "52e1d4c1d1bd85c91083d5265e349a3623b27ba31f39e35c41c0dcd2a8002c23",
}


def add_points(point, other, a: int, prime: int):
    if point is None:
        return other
    if other is None:
        return point
    x1, y1 = point
    x2, y2 = other
    if x1 == x2 and (y1 + y2) % prime == 0:
        return None
    if point == other:
        slope = (3 * x1 * x1 + a) * pow(2 * y1, -1, prime)
    else:
        slope = (y2 - y1) * pow(x2 - x1, -1, prime)
    slope %= prime
    x3 = (slope * slope - x1 - x2) % prime
    return x3, (slope * (x1 - x3) - y1) % prime


def multiply_point(scalar: int, point, a: int, prime: int):
    result = None
    while scalar:
        if scalar & 1:
            result = add_points(result, point, a, prime)
        point = add_points(point, point, a, prime)
        scalar >>= 1
    return result


def j_invariant(a: int, b: int, prime: int) -> int:
    numerator = 4 * pow(a, 3, prime) % prime
    denominator = (numerator + 27 * b * b) % prime
    return 1728 * numerator * pow(denominator, -1, prime) % prime


def velu_quotient(
    a: int, b: int, kernel_generator, prime: int, ell: int = 5
) -> tuple[int, int]:
    kernel = [
        multiply_point(scalar, kernel_generator, a, prime)
        for scalar in range(1, ell)
    ]
    assert all(point is not None for point in kernel)
    t = sum(3 * x * x + a for x, _ in kernel) % prime
    w = sum(5 * x**3 + 3 * a * x + 2 * b for x, _ in kernel) % prime
    return (a - 5 * t) % prime, (b - 7 * w) % prime


def evaluate(coefficients: dict[tuple[int, int], int], x: int, y: int,
             prime: int) -> int:
    return sum(
        coefficient * pow(x, x_degree, prime) * pow(y, y_degree, prime)
        for (x_degree, y_degree), coefficient in coefficients.items()
    ) % prime


class GeneratorTests(unittest.TestCase):
    def test_exact_outputs_and_determinism(self) -> None:
        for level, expected_digest in EXPECTED_DIGESTS.items():
            with self.subTest(level=level):
                first = generator.generate_sparse(level).encode("ascii")
                second = generator.generate_sparse(level).encode("ascii")
                fixture = (ROOT / f"data/modpoly/j/phi_{level}.txt").read_bytes()
                self.assertEqual(first, second)
                self.assertEqual(first, fixture)
                self.assertEqual(sha256(first).hexdigest(), expected_digest)

    def test_manifest_matches_generated_artifacts(self) -> None:
        manifest = json.loads(
            (ROOT / "data/modpoly/GENERATOR_MANIFEST.json").read_text()
        )
        self.assertEqual(manifest["schema"], 1)
        self.assertEqual(
            manifest["generator"], "tools/generate_classical_modpoly.py"
        )
        outputs = {entry["level"]: entry for entry in manifest["outputs"]}
        self.assertEqual(set(outputs), set(EXPECTED_DIGESTS))
        for level, digest in EXPECTED_DIGESTS.items():
            entry = outputs[level]
            self.assertEqual(entry["sha256"], digest)
            self.assertEqual(
                sha256((ROOT / entry["path"]).read_bytes()).hexdigest(), digest
            )

    def test_structure_and_known_level_five_coefficients(self) -> None:
        coefficients = generator.generate_coefficients(5)
        self.assertEqual(coefficients[6, 0], 1)
        self.assertEqual(coefficients[0, 6], 1)
        self.assertEqual(coefficients[5, 5], -1)
        self.assertEqual(coefficients[5, 4], 3720)
        self.assertEqual(
            coefficients[0, 0],
            141359947154721358697753474691071362751004672000,
        )
        self.assertEqual(max(x_degree for x_degree, _ in coefficients), 6)
        self.assertEqual(max(y_degree for _, y_degree in coefficients), 6)
        for (x_degree, y_degree), coefficient in coefficients.items():
            self.assertEqual(coefficient, coefficients[y_degree, x_degree])

    def test_level_five_velu_specializations(self) -> None:
        # These rational 5-torsion points on nonsingular short Weierstrass
        # curves over F_101 give a check independent of the q-series solve.
        prime = 101
        cases = [
            # a, b, kernel generator, expected (j(E), j(E/G))
            (1, 1, (46, 25), (34, 2)),
            (1, 2, (48, 42), (4, 50)),
            (1, 7, (72, 43), (32, 5)),
            (1, 8, (39, 33), (77, 50)),
            (1, 14, (52, 48), (1, 53)),
        ]
        coefficients = generator.generate_coefficients(5)
        for a, b, kernel_generator, expected_j_values in cases:
            with self.subTest(curve=(a, b)):
                x, y = kernel_generator
                self.assertEqual(y * y % prime, (x**3 + a * x + b) % prime)
                self.assertIsNone(multiply_point(5, kernel_generator, a, prime))
                quotient_a, quotient_b = velu_quotient(a, b, kernel_generator, prime)
                j_values = (
                    j_invariant(a, b, prime),
                    j_invariant(quotient_a, quotient_b, prime),
                )
                self.assertEqual(j_values, expected_j_values)
                self.assertEqual(evaluate(coefficients, *j_values, prime), 0)

    def test_level_seven_velu_specializations(self) -> None:
        prime = 101
        cases = [
            # a, b, kernel generator, expected (j(E), j(E/G))
            (1, 1, (3, 43), (34, 90)),
            (1, 4, (21, 14), (14, 19)),
            (1, 5, (8, 11), (85, 23)),
            (1, 6, (65, 28), (70, 83)),
            (1, 9, (21, 10), (67, 96)),
        ]
        coefficients = generator.generate_coefficients(7)
        for a, b, kernel_generator, expected_j_values in cases:
            with self.subTest(curve=(a, b)):
                x, y = kernel_generator
                self.assertEqual(y * y % prime, (x**3 + a * x + b) % prime)
                self.assertIsNone(multiply_point(7, kernel_generator, a, prime))
                quotient_a, quotient_b = velu_quotient(
                    a, b, kernel_generator, prime, 7
                )
                j_values = (
                    j_invariant(a, b, prime),
                    j_invariant(quotient_a, quotient_b, prime),
                )
                self.assertEqual(j_values, expected_j_values)
                self.assertEqual(evaluate(coefficients, *j_values, prime), 0)

    def test_rejects_unsupported_or_insufficient_requests(self) -> None:
        with self.assertRaises(ValueError):
            generator.generate_coefficients(11)
        with self.assertRaises(ValueError):
            generator.generate_coefficients(5, verification_order=0)


if __name__ == "__main__":
    unittest.main()
