#!/usr/bin/env python3
"""Schema and option-bound tests for profile_classical_direct_cohort."""

from __future__ import annotations

import hashlib
import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ONESHOTSEA = Path(os.environ.get(
    "ONESHOTSEA_BINARY", ROOT / "build" / "oneshotsea"))
PROFILER = Path(os.environ.get(
    "ONESHOTSEA_COHORT_PROFILER",
    ROOT / "build" / "profile_classical_direct_cohort"))
U64_MAX = (1 << 64) - 1


def decimal(value: object) -> int:
    if not isinstance(value, str) or re.fullmatch(r"0|[1-9][0-9]*", value) is None:
        raise AssertionError(f"not a canonical decimal string: {value!r}")
    parsed = int(value)
    if parsed > U64_MAX:
        raise AssertionError(f"decimal exceeds uint64: {value}")
    return parsed


class ClassicalDirectCohortProfilerTests(unittest.TestCase):
    maxDiff = None

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.cache = self.root / "direct.ctx"
        prepared = subprocess.run(
            [
                str(ONESHOTSEA), "prepare-classical-direct-context",
                "--p", "1000000009",
                "--classical-direct-levels", "5",
                "--output", str(self.cache),
                "--sea-threads", "1",
            ],
            text=True, capture_output=True, check=False,
        )
        self.assertEqual(prepared.returncode, 0, prepared.stderr)
        record = json.loads(prepared.stdout)
        self.digest = hashlib.sha256(self.cache.read_bytes()).hexdigest()
        self.assertEqual(record["sha256"], self.digest)
        self.base = [
            str(PROFILER),
            "--p", "1000000009",
            "--seed", "17",
            "--range-start", "0",
            "--count", "1",
            "--threads", "1",
            "--require-point4", "0",
            "--cache", str(self.cache),
            "--cache-sha256", self.digest,
            "--cache-resident-bytes", "1000000",
            "--schoof-through", "5",
            "--maximum-prime-candidates", "1000000",
            "--maximum-x-candidates", "1000000",
            "5",
        ]

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def run_profiler(self, *extra: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [*self.base, *extra], text=True, capture_output=True, check=False)

    def assert_rejected(self, *extra: str) -> None:
        result = self.run_profiler(*extra)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertEqual(result.stdout, "")
        self.assertIn("classical direct cohort profile failed:", result.stderr)

    def test_help_and_numeric_option_bounds(self) -> None:
        help_result = subprocess.run(
            [str(PROFILER), "--help"], text=True, capture_output=True,
            check=False)
        self.assertEqual(help_result.returncode, 0, help_result.stderr)
        self.assertTrue(help_result.stdout.startswith(
            "usage: profile_classical_direct_cohort"))

        self.assert_rejected("--count", "0")
        self.assert_rejected("--range-start", str(U64_MAX), "--count", "2")
        self.assert_rejected("--threads", "0")
        self.assert_rejected("--require-point4", "2")
        self.assert_rejected("--maximum-prime-candidates", "0")
        self.assert_rejected("--maximum-x-candidates", "0")
        self.assert_rejected("--seed", str(U64_MAX + 1))
        self.assert_rejected("--cache-resident-bytes", str(U64_MAX + 1))
        self.assert_rejected("--cache-sha256", "not-a-digest")
        self.assert_rejected("5")  # Duplicate level.
        self.assert_rejected("4")  # Unsorted, non-prime level.
        self.assert_rejected("4294967296")  # Exceeds unsigned level bound.

        missing_levels = subprocess.run(
            self.base[:-1], text=True, capture_output=True, check=False)
        self.assertNotEqual(missing_levels.returncode, 0)
        self.assertEqual(missing_levels.stdout, "")
        self.assertIn("requires a cache, digest, and levels",
                      missing_levels.stderr)

    def test_success_schema_order_bounds_and_raw_totals(self) -> None:
        result = self.run_profiler()
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(rows), 3)
        level, curve, summary = rows
        self.assertEqual(
            [row["schema"] for row in rows],
            [
                "oneshotsea.classical-direct-cohort-level.v1",
                "oneshotsea.classical-direct-cohort-curve.v1",
                "oneshotsea.classical-direct-cohort-summary.v1",
            ],
        )
        self.assertEqual(set(level), {
            "schema", "global_index", "curve_j", "selected_side",
            "trace_prior_modulus", "trace_prior_residue", "ell", "exact",
            "trace_residue", "atkin_projective_order", "atkin_residue_count",
            "information_microbits", "evaluation_us", "materialization_count",
            "materialization_us", "schoof_residue",
            "schoof_control_applicable", "schoof_us",
            "process_peak_rss_bytes",
        })
        self.assertEqual(set(curve), {
            "schema", "global_index", "levels", "generation_us",
            "evaluation_us", "materialization_us", "schoof_us", "profile_us",
            "process_peak_rss_bytes",
        })
        self.assertEqual(set(summary), {
            "schema", "prime", "seed", "range_start", "count", "threads",
            "require_point_four", "maximum_prime_candidates",
            "maximum_x_candidates_per_surface", "schoof_through",
            "cache_sha256", "cache_index_us", "cache_residency_budget_bytes",
            "cached_level_load_count", "cached_level_load_us",
            "cached_context_evictions", "final_cached_retained_contexts",
            "final_cached_retained_payload_bytes", "process_peak_rss_bytes",
            "generation_us", "elapsed_us", "levels",
            "warm_information_per_cost_order",
            "observed_information_per_cost_order", "claim_scope",
        })

        self.assertEqual(level["global_index"], "0")
        self.assertEqual(level["ell"], "5")
        self.assertIs(type(level["exact"]), bool)
        self.assertIs(type(level["schoof_control_applicable"]), bool)
        self.assertTrue(level["schoof_control_applicable"])
        self.assertIsNotNone(level["schoof_residue"])
        self.assertLess(decimal(level["schoof_residue"]), 5)
        self.assertEqual(curve["global_index"], "0")
        self.assertEqual(decimal(curve["levels"]), 1)
        self.assertEqual(summary["prime"], "1000000009")
        self.assertEqual(summary["seed"], "17")
        self.assertEqual(summary["count"], "1")
        self.assertEqual(summary["threads"], "1")
        self.assertFalse(summary["require_point_four"])
        self.assertEqual(summary["cache_sha256"], self.digest)
        self.assertEqual(summary["warm_information_per_cost_order"], ["5"])
        self.assertEqual(summary["observed_information_per_cost_order"], ["5"])

        for field in ("evaluation_us", "materialization_us", "schoof_us"):
            self.assertEqual(decimal(curve[field]), decimal(level[field]))
        self.assertGreaterEqual(
            decimal(curve["profile_us"]),
            decimal(curve["evaluation_us"]) +
            decimal(curve["materialization_us"]) +
            decimal(curve["schoof_us"]),
        )
        aggregate = summary["levels"][0]
        self.assertEqual(set(aggregate), {
            "ell", "samples", "exact", "atkin", "unconstrained",
            "information_microbits", "evaluation_us", "materializations",
            "materialization_us", "schoof_attempts", "schoof_validations",
            "schoof_us",
        })
        self.assertEqual(aggregate["ell"], "5")
        self.assertEqual(decimal(aggregate["samples"]), 1)
        self.assertEqual(decimal(aggregate["exact"]), int(level["exact"]))
        self.assertEqual(
            decimal(aggregate["atkin"]),
            int(not level["exact"] and
                level["atkin_projective_order"] is not None),
        )
        self.assertEqual(
            decimal(aggregate["unconstrained"]),
            int(not level["exact"] and
                level["atkin_projective_order"] is None),
        )
        for summary_field, level_field in (
            ("information_microbits", "information_microbits"),
            ("evaluation_us", "evaluation_us"),
            ("materializations", "materialization_count"),
            ("materialization_us", "materialization_us"),
            ("schoof_us", "schoof_us"),
        ):
            self.assertEqual(decimal(aggregate[summary_field]),
                             decimal(level[level_field]))
        self.assertEqual(decimal(aggregate["schoof_attempts"]), 1)
        self.assertEqual(decimal(aggregate["schoof_validations"]), 1)
        self.assertEqual(decimal(summary["cached_level_load_count"]),
                         decimal(level["materialization_count"]))
        self.assertEqual(decimal(summary["cached_level_load_us"]),
                         decimal(level["materialization_us"]))
        self.assertEqual(decimal(summary["generation_us"]),
                         decimal(curve["generation_us"]))
        self.assertGreaterEqual(
            decimal(summary["elapsed_us"]),
            decimal(curve["generation_us"]) + decimal(curve["profile_us"]),
        )
        self.assertEqual(
            decimal(summary["process_peak_rss_bytes"]),
            max(decimal(row["process_peak_rss_bytes"])
                for row in (level, curve, summary)),
        )


if __name__ == "__main__":
    unittest.main()
