#!/usr/bin/env python3
"""Tests for the retained SEA progress auditor."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
AUDITOR = ROOT / "tools" / "audit_sea_progress.py"


def fixture(second_residue: str = "1") -> dict[str, object]:
    return {
        "schema": "oneshotsea.search-curve.v1",
        "index": "7",
        "sea_level_timings": [
            {
                "pass": "1",
                "ell": "5",
                "exact": True,
                "trace_residue": "4",
                "exact_modulus": "5",
                "trace_candidate_count": "8",
            },
            {
                "pass": "1",
                "ell": "7",
                "exact": True,
                "trace_residue": second_residue,
                "exact_modulus": "35",
                "trace_candidate_count": "1",
            },
            {
                "pass": "1",
                "ell": "11",
                "exact": False,
                "exact_modulus": "35",
                "trace_candidate_count": "1",
            },
        ],
        "state": {"prime": "101"},
    }


class ProgressAuditTests(unittest.TestCase):
    def run_audit(self, value: dict[str, object]) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as directory:
            progress = Path(directory) / "progress.jsonl"
            progress.write_text(json.dumps(value) + "\n", encoding="utf-8")
            return subprocess.run(
                [
                    "python3", str(AUDITOR), "--progress", str(progress),
                    "--index", "7", "--trace", "-6",
                ],
                text=True,
                capture_output=True,
                check=False,
            )

    def test_accepts_exact_residues_and_crt_counts(self) -> None:
        completed = self.run_audit(fixture())
        self.assertEqual(completed.returncode, 0, completed.stderr)
        result = json.loads(completed.stdout)
        self.assertTrue(result["verified"])
        self.assertEqual(result["curve_order"], "108")
        self.assertEqual(result["twist_order"], "96")
        self.assertEqual(result["exact_levels"], 2)
        self.assertEqual(result["passes"], [{"pass": 1, "modulus": "35"}])

    def test_rejects_residue_disagreement(self) -> None:
        completed = self.run_audit(fixture(second_residue="2"))
        self.assertEqual(completed.returncode, 1)
        self.assertIn("independent trace disagrees at ell=7", completed.stderr)


if __name__ == "__main__":
    unittest.main()
