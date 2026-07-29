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


if __name__ == "__main__":
    try:
        MAGMA = point_count.resolve_magma(os.environ.get("MAGMA"))
    except RuntimeError as exc:
        print(f"differential tests: {exc}", file=sys.stderr)
        raise SystemExit(2)
    unittest.main()
