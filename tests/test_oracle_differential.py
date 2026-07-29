#!/usr/bin/env python3
"""Differential tests for native reference kernels against local Magma."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / "build" / "oneshotsea"
sys.path.insert(0, str(ROOT / "oracle"))
import point_count  # noqa: E402


MAGMA: str | None = None


def native(*arguments: object) -> dict[str, object]:
    completed = subprocess.run(
        [str(NATIVE), *(str(argument) for argument in arguments)],
        cwd=ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"native command failed ({completed.returncode}): {completed.stderr.strip()}"
        )
    return json.loads(completed.stdout)


class NativeOracleDifferentialTests(unittest.TestCase):
    PRIMES = (97, 101, 127, 211, 307, 509, 1009)

    def test_deterministic_curves_and_point_counts(self) -> None:
        assert MAGMA is not None
        tested = 0
        for p in self.PRIMES:
            for index in range(6):
                curve = native("curve", "--p", p, "--seed", 20260729, "--index", index)
                if curve["singular"]:
                    continue
                a = int(curve["a"])
                b = int(curve["b"])
                local = native("point-count", "--p", p, "--a", a, "--b", b)
                oracle = point_count.count_curve(MAGMA, p, a, b)
                self.assertEqual(int(local["order"]), oracle["order"])
                self.assertEqual(int(local["trace"]), oracle["trace"])
                tested += 1
        self.assertGreaterEqual(tested, 35)

    def test_phi3_classification_against_oracle_trace(self) -> None:
        assert MAGMA is not None
        checked = 0
        for p in self.PRIMES:
            if p == 3:
                continue
            for index in range(12):
                curve = native("curve", "--p", p, "--seed", 314159, "--index", index)
                if curve["singular"]:
                    continue
                a = int(curve["a"])
                b = int(curve["b"])
                if int(curve["j"]) in (0, 1728 % p):
                    continue
                oracle = point_count.count_curve(MAGMA, p, a, b)
                trace = oracle["trace"]
                if trace == 0:
                    continue  # supersingular factorization is handled separately
                discriminant = (trace * trace - 4 * p) % 3
                if discriminant == 0:
                    continue  # ramified case
                classification = native(
                    "modpoly",
                    "--p",
                    p,
                    "--a",
                    a,
                    "--b",
                    b,
                    "--level",
                    3,
                    "--file",
                    "data/modpoly/j/phi_3.txt",
                )
                self.assertEqual(
                    int(classification["rational_roots"]) > 0,
                    discriminant == 1,
                    (p, a, b, trace, classification),
                )
                checked += 1
        self.assertGreaterEqual(checked, 35)

    def test_native_schoof_residues_against_magma_trace(self) -> None:
        assert MAGMA is not None
        checked = 0
        for p in self.PRIMES[:4]:
            for index in range(4):
                curve = native("curve", "--p", p, "--seed", 0x5C400F, "--index", index)
                if curve["singular"]:
                    continue
                a = int(curve["a"])
                b = int(curve["b"])
                trace = point_count.count_curve(MAGMA, p, a, b)["trace"]
                for ell in (3, 5, 7):
                    if ell == p:
                        continue
                    residue = native(
                        "schoof-residue",
                        "--p",
                        p,
                        "--a",
                        a,
                        "--b",
                        b,
                        "--ell",
                        ell,
                    )
                    self.assertEqual(int(residue["trace_residue"]), trace % ell)
                    checked += 1
        self.assertGreaterEqual(checked, 40)

    def test_complete_native_schoof_counts(self) -> None:
        assert MAGMA is not None
        checked = 0
        for p in (97, 101, 1009):
            for index in range(2):
                curve = native("curve", "--p", p, "--seed", 0xC0FFEE, "--index", index)
                if curve["singular"]:
                    continue
                a = int(curve["a"])
                b = int(curve["b"])
                oracle = point_count.count_curve(MAGMA, p, a, b)
                count = native(
                    "schoof-count",
                    "--p",
                    p,
                    "--a",
                    a,
                    "--b",
                    b,
                    "--max-ell",
                    13,
                )
                self.assertEqual(int(count["order"]), oracle["order"])
                self.assertEqual(int(count["trace"]), oracle["trace"])
                self.assertGreater(int(count["residue_modulus"]), 0)
                checked += 1
        self.assertGreaterEqual(checked, 6)

    def test_level_3_elkies_residues(self) -> None:
        assert MAGMA is not None
        checked = 0
        elkies = 0
        atkin = 0
        phi3 = ROOT / "data" / "modpoly" / "j" / "phi_3.txt"
        for p in (97, 101, 1009):
            for index in range(8):
                curve = native("curve", "--p", p, "--seed", 0xE1C1E5, "--index", index)
                if curve["singular"]:
                    continue
                a = int(curve["a"])
                b = int(curve["b"])
                oracle = point_count.count_curve(MAGMA, p, a, b)
                result = native(
                    "elkies-residue",
                    "--p",
                    p,
                    "--a",
                    a,
                    "--b",
                    b,
                    "--ell",
                    3,
                    "--file",
                    phi3,
                )
                residue = oracle["trace"] % 3
                discriminant = (residue * residue - 4 * p) % 3
                expected_elkies = discriminant in (0, 1)
                self.assertEqual(result["elkies"], expected_elkies)
                if expected_elkies:
                    self.assertEqual(result["trace_residue"], residue)
                    self.assertGreater(result["kernel_count"], 0)
                    elkies += 1
                else:
                    self.assertNotIn("trace_residue", result)
                    atkin += 1
                checked += 1
        self.assertGreaterEqual(checked, 20)
        self.assertGreater(elkies, 0)
        self.assertGreater(atkin, 0)

    def test_division_kernel_elkies_residues(self) -> None:
        assert MAGMA is not None
        checked = 0
        for ell in (5, 7):
            elkies = 0
            atkin = 0
            for p in (97, 101):
                for index in range(5):
                    curve = native(
                        "curve", "--p", p, "--seed", 0xD1A1510, "--index", index
                    )
                    if curve["singular"]:
                        continue
                    a = int(curve["a"])
                    b = int(curve["b"])
                    oracle = point_count.count_curve(MAGMA, p, a, b)
                    result = native(
                        "elkies-division-residue",
                        "--p",
                        p,
                        "--a",
                        a,
                        "--b",
                        b,
                        "--ell",
                        ell,
                    )
                    residue = oracle["trace"] % ell
                    discriminant = (residue * residue - 4 * p) % ell
                    expected_elkies = discriminant == 0 or pow(
                        discriminant, (ell - 1) // 2, ell
                    ) == 1
                    self.assertEqual(result["elkies"], expected_elkies)
                    if expected_elkies:
                        self.assertEqual(result["trace_residue"], residue)
                        self.assertGreater(result["kernel_count"], 0)
                        elkies += 1
                    else:
                        self.assertNotIn("trace_residue", result)
                        atkin += 1
                    checked += 1
            self.assertGreater(elkies, 0)
            self.assertGreater(atkin, 0)
        self.assertGreaterEqual(checked, 18)

    def test_bmss_residues_against_magma_trace(self) -> None:
        assert MAGMA is not None
        successes = 0
        for ell in (5, 7):
            phi = ROOT / "data" / "modpoly" / "j" / f"phi_{ell}.txt"
            for p in (97, 101):
                for index in range(5):
                    curve = native(
                        "curve", "--p", p, "--seed", 0xB055, "--index", index
                    )
                    if curve["singular"]:
                        continue
                    a = int(curve["a"])
                    b = int(curve["b"])
                    trace = point_count.count_curve(MAGMA, p, a, b)["trace"]
                    discriminant = (trace * trace - 4 * p) % ell
                    expected_elkies = discriminant == 0 or pow(
                        discriminant, (ell - 1) // 2, ell
                    ) == 1
                    result = native(
                        "elkies-bmss-residue",
                        "--p",
                        p,
                        "--a",
                        a,
                        "--b",
                        b,
                        "--ell",
                        ell,
                        "--file",
                        phi,
                    )
                    if not expected_elkies:
                        self.assertFalse(result["elkies"])
                    if result["elkies"]:
                        self.assertEqual(result["trace_residue"], trace % ell)
                        successes += 1
        self.assertGreaterEqual(successes, 5)


if __name__ == "__main__":
    try:
        MAGMA = point_count.resolve_magma(os.environ.get("MAGMA"))
    except RuntimeError as exc:
        print(f"differential tests: {exc}", file=sys.stderr)
        raise SystemExit(2)
    unittest.main()
