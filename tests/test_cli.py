#!/usr/bin/env python3
"""CLI parsing regressions that do not require a computer algebra oracle."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "build" / "oneshotsea"
PHI3 = ROOT / "data" / "modpoly" / "j" / "phi_3.txt"
PHI5 = ROOT / "data" / "modpoly" / "j" / "phi_5.txt"
WEBER_PHI5 = ROOT / "data" / "modpoly" / "weber_f" / "phi_5.txt"


class CliTests(unittest.TestCase):
    def run_cli(self, *arguments: object) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(BINARY), *(str(argument) for argument in arguments)],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def assert_rejected(self, *arguments: object) -> None:
        result = self.run_cli(*arguments)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertEqual(result.stdout, "")

    def test_unsigned_options_reject_signs_and_overflow(self) -> None:
        common = ("--p", 101, "--a", 2, "--b", 3)
        for value in ("-4294967293", "4294967299", "18446744073709551616"):
            self.assert_rejected(
                "elkies-residue", *common, "--ell", value, "--file", PHI3
            )
        self.assert_rejected("curve", "--p", 101, "--seed", -1, "--index", 0)
        self.assert_rejected("curve", "--p", 101, "--seed", 0, "--index", "+1")
        self.assert_rejected(
            "modpoly", *common, "--level", "4294967299", "--file", PHI3
        )

    def test_valid_level_is_not_changed(self) -> None:
        result = self.run_cli(
            "elkies-residue",
            "--p",
            101,
            "--a",
            2,
            "--b",
            3,
            "--ell",
            3,
            "--file",
            PHI3,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["ell"], 3)

    def test_bmss_cli_matches_division_reference(self) -> None:
        common = ("--p", 101, "--a", 39, "--b", 44, "--ell", 5)
        bmss = self.run_cli(
            "elkies-bmss-residue", *common, "--file", PHI5
        )
        division = self.run_cli("elkies-division-residue", *common)
        self.assertEqual(bmss.returncode, 0, bmss.stderr)
        self.assertEqual(division.returncode, 0, division.stderr)
        bmss_result = json.loads(bmss.stdout)
        division_result = json.loads(division.stdout)
        self.assertTrue(bmss_result["elkies"])
        self.assertEqual(
            bmss_result["trace_residue"], division_result["trace_residue"]
        )

    def test_weber_cli_matches_classical_bmss(self) -> None:
        common = ("--p", 109, "--a", 82, "--b", 45, "--ell", 5)
        weber = self.run_cli(
            "elkies-weber-residue", *common, "--file", WEBER_PHI5
        )
        classical = self.run_cli(
            "elkies-bmss-residue", *common, "--file", PHI5
        )
        self.assertEqual(weber.returncode, 0, weber.stderr)
        self.assertEqual(classical.returncode, 0, classical.stderr)
        self.assertEqual(
            json.loads(weber.stdout)["trace_residue"],
            json.loads(classical.stdout)["trace_residue"],
        )


if __name__ == "__main__":
    unittest.main()
