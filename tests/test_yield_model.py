#!/usr/bin/env python3
"""Deterministic checks for the p125 Dickman yield artifact."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "p125_yield_model.py"
ARTIFACT = ROOT / "artifacts" / "local" / "p125-yield-model-20260801" / "result.json"


class YieldModelTests(unittest.TestCase):
    def test_checked_artifact_is_exactly_reproducible(self) -> None:
        completed = subprocess.run(
            ["python3", str(SCRIPT), "--check", str(ARTIFACT)],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        digest = hashlib.sha256(ARTIFACT.read_bytes()).hexdigest()
        self.assertIn(f"sha256={digest}", completed.stdout)

    def test_default_output_and_model_invariants(self) -> None:
        completed = subprocess.run(
            ["python3", str(SCRIPT)],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(completed.stdout, ARTIFACT.read_text(encoding="utf-8"))
        result = json.loads(completed.stdout)

        self.assertEqual(result["target"]["bit_length"], 416)
        self.assertEqual(result["target"]["smooth_bound_n4"], 416**4)
        self.assertAlmostEqual(
            result["numerics"]["rho_2_validation"],
            result["numerics"]["rho_2_exact_1_minus_log_2"],
            delta=1e-10,
        )
        order_probability = result["numerics"][
            "production_generator_probability_per_actual_order"
        ]
        self.assertEqual(
            result["numerics"]["production_generator_forced_divisor"], 4
        )
        self.assertEqual(result["numerics"]["reported_significant_digits"], 5)
        self.assertLess(
            result["numerics"][
                "production_generator_probability_coarse_to_checked_relative_delta"
            ],
            1e-5,
        )
        curve_probability = result["curve_twist_projection"][
            "optimistic_probability_per_curve"
        ]
        self.assertAlmostEqual(
            curve_probability,
            1.0 - (1.0 - order_probability) ** 2,
            delta=1e-9,
        )
        retained = result["retained_search_interpretation"]
        self.assertEqual(retained["complete_trace_candidates"], 136)
        self.assertEqual(retained["trace_candidate_orders_exactly_screened"], 272)
        self.assertEqual(retained["actual_curve_twist_order_opportunities"], 24)
        self.assertIn("not independent", retained["distinction"])

        self.assertEqual(
            result["throughput"]["measured_k10_warm_seconds_per_curve"],
            27.071228,
        )
        forced = result["forced_divisor_scenarios"]
        self.assertIn("not a certificate rate", forced["classification"])
        scenarios = forced["scenarios"]
        expected_divisors = {
            "current_generator": (4, 4),
            "x1_11_cyclic": (44, 4),
            "x1_11_group": (88, 4),
            "x1_25_cyclic": (100, 4),
            "x1_25_group": (200, 4),
        }
        prior_probability = 0.0
        prior_multiplier = 0.0
        for identifier, (divisor, paired_divisor) in expected_divisors.items():
            scenario = scenarios[identifier]
            self.assertEqual(scenario["selected_side_forced_divisor"], divisor)
            self.assertEqual(
                scenario["paired_twist_forced_divisor"], paired_divisor
            )
            self.assertEqual(
                scenario["paired_twist_order_residue_mod_selected_divisor"]
                % paired_divisor,
                0,
            )
            selected = scenario["selected_side_smooth_opportunity_probability"]
            paired_twist = scenario[
                "paired_twist_smooth_opportunity_probability"
            ]
            paired = scenario[
                "optimistic_paired_smooth_opportunity_probability"
            ]
            self.assertAlmostEqual(
                paired,
                1.0 - (1.0 - selected) * (1.0 - paired_twist),
                delta=1e-9,
            )
            self.assertGreater(selected, prior_probability)
            self.assertGreaterEqual(
                scenario["optimistic_paired_yield_multiplier_over_current"],
                prior_multiplier,
            )
            prior_probability = selected
            prior_multiplier = scenario[
                "optimistic_paired_yield_multiplier_over_current"
            ]


if __name__ == "__main__":
    unittest.main()
