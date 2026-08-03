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


def fixture(second_residue: str = "5") -> dict[str, object]:
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
                "constraint_modulus": "5",
                "exact_trace_candidate_count": "8",
                "trace_candidate_count": "8",
            },
            {
                "pass": "1",
                "ell": "7",
                "exact": False,
                "exact_modulus": "5",
                "constraint_modulus": "35",
                "exact_trace_candidate_count": "8",
                "trace_candidate_count": "5",
                "atkin_projective_order": "8",
                "atkin_residue_count": "4",
            },
            {
                "pass": "1",
                "ell": "11",
                "exact": True,
                "trace_residue": second_residue,
                "exact_modulus": "55",
                "constraint_modulus": "385",
                "exact_trace_candidate_count": "1",
                "trace_candidate_count": "1",
            },
        ],
        "state": {"prime": "101"},
    }


def direct_fixture() -> dict[str, object]:
    return {
        "schema": "oneshotsea.search-curve.v1",
        "index": "7",
        "trace_prior": {"modulus": "4", "residue": "2"},
        "sea_level_timings": [],
        "classical_direct_levels": [
            {
                "pass": "1",
                "ell": "7",
                "exact": True,
                "trace_residue": "4",
                "order_discriminant": "-567",
                "class_number": "12",
                "auxiliary_prime_count": "18",
                "elkies_kernel_count": "2",
                "exact_modulus": "28",
                "constraint_modulus": "28",
                "exact_trace_candidate_count": "2",
                "trace_candidate_count": "2",
                "atkin_projective_order": None,
                "atkin_residue_count": "0",
                "elapsed_us": "182440",
            },
            {
                "pass": "1",
                "ell": "11",
                "exact": True,
                "trace_residue": "1",
                "order_discriminant": "-5103",
                "class_number": "36",
                "auxiliary_prime_count": "26",
                "elkies_kernel_count": "2",
                "exact_modulus": "308",
                "constraint_modulus": "308",
                "exact_trace_candidate_count": "1",
                "trace_candidate_count": "1",
                "atkin_projective_order": None,
                "atkin_residue_count": "0",
                "elapsed_us": "1450836",
            },
        ],
        "state": {"prime": "101"},
    }


class ProgressAuditTests(unittest.TestCase):
    def run_audit(
        self, value: dict[str, object], trace: str = "-6"
    ) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as directory:
            progress = Path(directory) / "progress.jsonl"
            progress.write_text(json.dumps(value) + "\n", encoding="utf-8")
            return subprocess.run(
                [
                    "python3", str(AUDITOR), "--progress", str(progress),
                    "--index", "7", "--trace", trace,
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
        self.assertEqual(result["atkin_levels"], 1)
        self.assertEqual(result["passes"], [{"pass": 1, "modulus": "55"}])

    def test_rejects_residue_disagreement(self) -> None:
        completed = self.run_audit(fixture(second_residue="2"))
        self.assertEqual(completed.returncode, 1)
        self.assertIn("independent trace disagrees at ell=11", completed.stderr)

    def test_rejects_corrupt_atkin_order(self) -> None:
        value = fixture()
        value["sea_level_timings"][1]["atkin_projective_order"] = "4"
        completed = self.run_audit(value)
        self.assertEqual(completed.returncode, 1)
        self.assertIn("Atkin order 4", completed.stderr)

    def test_replays_every_level_from_exact_trace_prior(self) -> None:
        value = fixture()
        value["trace_prior"] = {"modulus": "4", "residue": "2"}
        levels = value["sea_level_timings"]
        levels[0].update(
            {
                "exact_modulus": "20",
                "constraint_modulus": "20",
                "exact_trace_candidate_count": "2",
                "trace_candidate_count": "2",
            }
        )
        levels[1].update(
            {
                "exact_modulus": "20",
                "constraint_modulus": "140",
                "exact_trace_candidate_count": "2",
                "trace_candidate_count": "1",
            }
        )
        levels[2].update(
            {
                "exact_modulus": "220",
                "constraint_modulus": "1540",
                "exact_trace_candidate_count": "1",
                "trace_candidate_count": "1",
            }
        )
        completed = self.run_audit(value)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        result = json.loads(completed.stdout)
        self.assertEqual(result["trace_prior"], {"modulus": "4", "residue": "2"})
        self.assertEqual(result["passes"], [{"pass": 1, "modulus": "220"}])

    def test_rejects_trace_prior_disagreement(self) -> None:
        value = fixture()
        value["trace_prior"] = {"modulus": "4", "residue": "1"}
        completed = self.run_audit(value)
        self.assertEqual(completed.returncode, 1)
        self.assertIn("independent trace disagrees with trace_prior", completed.stderr)

    def test_replays_direct_tail_from_trace_prior(self) -> None:
        # The fixture residues correspond to the real p=101 search trace -10.
        replay = self.run_audit(direct_fixture(), "-10")
        self.assertEqual(replay.returncode, 0, replay.stderr)
        result = json.loads(replay.stdout)
        self.assertEqual(result["weber_levels"], 0)
        self.assertEqual(result["classical_direct_levels"], 2)
        self.assertEqual(result["exact_levels"], 2)
        self.assertEqual(result["passes"], [])
        self.assertEqual(
            result["classical_direct_passes"],
            [{"pass": 1, "modulus": "308"}],
        )

    def test_rejects_corrupt_direct_cm_evidence(self) -> None:
        for field, value, message in (
            ("order_discriminant", "567", "invalid CM/CRT evidence"),
            ("class_number", "0", "invalid CM/CRT evidence"),
            ("auxiliary_prime_count", "0", "invalid CM/CRT evidence"),
            ("elkies_kernel_count", "0", "inconsistent Elkies kernel"),
        ):
            record = direct_fixture()
            record["classical_direct_levels"][0][field] = value
            completed = self.run_audit(record, "-10")
            self.assertEqual(completed.returncode, 1)
            self.assertIn(message, completed.stderr)

    def test_direct_tail_continues_table_crt_state(self) -> None:
        record = direct_fixture()
        record["sea_level_timings"] = [
            {
                "pass": "1",
                "ell": "5",
                "exact": True,
                "trace_residue": "0",
                "exact_modulus": "20",
                "constraint_modulus": "20",
                "exact_trace_candidate_count": "2",
                "trace_candidate_count": "2",
                "atkin_projective_order": None,
                "atkin_residue_count": "0",
            }
        ]
        direct = record["classical_direct_levels"][0]
        direct["exact_modulus"] = "140"
        direct["constraint_modulus"] = "140"
        direct["exact_trace_candidate_count"] = "1"
        direct["trace_candidate_count"] = "1"
        record["classical_direct_levels"] = [direct]
        completed = self.run_audit(record, "-10")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        result = json.loads(completed.stdout)
        self.assertEqual(result["passes"], [{"pass": 1, "modulus": "20"}])
        self.assertEqual(
            result["classical_direct_passes"],
            [{"pass": 1, "modulus": "140"}],
        )


if __name__ == "__main__":
    unittest.main()
