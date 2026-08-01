#!/usr/bin/env python3
"""CLI parsing regressions that do not require a computer algebra oracle."""

from __future__ import annotations

import json
import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "build" / "oneshotsea"
PHI3 = ROOT / "data" / "modpoly" / "j" / "phi_3.txt"
PHI5 = ROOT / "data" / "modpoly" / "j" / "phi_5.txt"
WEBER_PHI5 = ROOT / "data" / "modpoly" / "weber_f" / "phi_5.txt"
WEBER_PHI11 = ROOT / "data" / "modpoly" / "weber_f" / "phi_11.txt"


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

    def test_empty_weber_result_is_not_mislabeled_atkin(self) -> None:
        # This level is genuinely Elkies: t=18 and
        # t^2-4p = 3 (mod 11), a quadratic residue.  The rational Weber
        # lifts are exceptional, however, so an empty kernel list supplies
        # no Atkin classification evidence.
        result = self.run_cli(
            "elkies-weber-residue",
            "--p",
            193,
            "--a",
            1,
            "--b",
            29,
            "--ell",
            11,
            "--file",
            WEBER_PHI11,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        record = json.loads(result.stdout)
        self.assertEqual(record["status"], "unavailable")
        self.assertIsNone(record["elkies"])
        self.assertEqual(record["kernel_count"], 0)

    def test_stateful_weber_sea_streams_exact_progress(self) -> None:
        result = self.run_cli(
            "sea-weber-count",
            "--p",
            193,
            "--a",
            148,
            "--b",
            168,
            "--max-level",
            11,
            "--table-dir",
            ROOT / "data" / "modpoly" / "weber_f",
            "--trace-cap",
            1,
            "--sea-threads",
            1,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        records = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([record["ell"] for record in records[:-1]], [5, 7, 11])
        self.assertTrue(
            all(record["modular_root_workers"] == 1 for record in records[:-1])
        )
        self.assertEqual(
            [record["exact"] for record in records[:-1]], [True, False, True]
        )
        self.assertEqual(records[-1]["type"], "summary")
        self.assertEqual(records[-1]["status"], "trace_set_enumerated")
        self.assertEqual(records[-1]["exact_modulus"], "55")
        self.assertEqual(records[-1]["traces"], ["-6"])

    def test_no_rational_weber_lift_is_explicitly_incomplete(self) -> None:
        result = self.run_cli(
            "sea-weber-count",
            "--p",
            97,
            "--a",
            1,
            "--b",
            1,
            "--max-level",
            11,
            "--table-dir",
            ROOT / "data" / "modpoly" / "weber_f",
            "--trace-cap",
            1,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        records = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]["status"], "no_rational_weber_lift")
        self.assertFalse(records[0]["complete"])
        self.assertEqual(records[0]["levels_processed"], 0)

    def test_deterministic_search_cli_checkpoints_and_verifies(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = self.run_cli(
                "search",
                "--p", 101,
                "--seed", 4,
                "--range-start", 1,
                "--range-end", 2,
                "--worker-id", 0,
                "--worker-count", 1,
                "--max-level", 11,
                "--trace-cap", 16,
                "--sea-threads", 2,
                "--table-dir", ROOT / "data" / "modpoly" / "weber_f",
                "--smooth-cache", root / "smooth.cache",
                "--checkpoint", root / "checkpoint.json",
                "--progress", root / "progress.ndjson",
                "--certificate-out", root / "certificate.txt",
                "--max-curves", 1,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            records = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertEqual(records[0]["schema"], "oneshotsea.search-start.v1")
            self.assertEqual(len(records[0]["smooth_cache_sha256"]), 64)
            self.assertEqual(len(records[0]["table_manifest_sha256"]), 64)
            self.assertEqual(len(records[0]["verifier_sha256"]), 64)
            self.assertFalse(records[0]["heuristic_rejection"])
            self.assertEqual(records[0]["resources"]["sea_threads"], "2")
            self.assertEqual(
                records[0]["resources"]["smooth_root_auxiliary_bytes"],
                str(128 * 1024 * 1024),
            )
            self.assertEqual(records[-1]["schema"],
                             "oneshotsea.search-summary.v1")
            self.assertTrue(records[-1]["verified"])
            curve_records = records[1:-1]
            self.assertEqual([record["index"] for record in curve_records],
                             ["1"])
            self.assertEqual(curve_records[-1]["status"],
                             "verified_certificate")
            self.assertEqual(curve_records[-1]["certificate"]["line"],
                             "101 35 25 28")
            self.assertTrue(
                all(
                    1 <= int(level["modular_root_workers"]) <= 2
                    for level in curve_records[-1]["sea_level_timings"]
                )
            )
            levels = curve_records[-1]["sea_level_timings"]
            self.assertTrue(all("exact_modulus" in level for level in levels))
            self.assertTrue(
                all("trace_candidate_count" in level for level in levels)
            )
            self.assertTrue(
                all("compatible_source_lifts" in level for level in levels)
            )
            self.assertTrue(
                all(
                    ("trace_residue" in level) == level["exact"]
                    for level in levels
                )
            )
            exact_moduli = [
                int(level["exact_modulus"])
                for level in levels if level["exact"]
            ]
            self.assertEqual(exact_moduli, sorted(exact_moduli))
            self.assertEqual((root / "certificate.txt").read_text(),
                             "101 35 25 28\n")
            self.assertTrue((root / "checkpoint.json").is_file())
            self.assertEqual(
                len((root / "progress.ndjson").read_text().splitlines()), 1
            )

    def test_search_rejects_non_python_success_executable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assert_rejected(
                "search", "--p", 101, "--seed", 17,
                "--range-start", 0, "--range-end", 1,
                "--worker-id", 0, "--worker-count", 1,
                "--max-level", 31,
                "--table-dir", ROOT / "data" / "modpoly" / "weber_f",
                "--smooth-cache", root / "smooth.cache",
                "--checkpoint", root / "checkpoint.json",
                "--python", "/usr/bin/true",
                "--max-curves", 0,
            )

    def test_every_search_requires_preexisting_cache_digest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            common = (
                "search", "--p", 101, "--seed", 17,
                "--range-start", 0, "--range-end", 1,
                "--worker-id", 0, "--worker-count", 1,
                "--max-level", 31,
                "--table-dir", ROOT / "data" / "modpoly" / "weber_f",
                "--smooth-cache", root / "smooth.cache",
                "--max-curves", 0,
            )
            built = self.run_cli(*common, "--checkpoint", root / "build.json")
            self.assertEqual(built.returncode, 0, built.stderr)
            start = json.loads(built.stdout.splitlines()[0])
            self.assertEqual(start["resources"]["trace_cap"], "64")
            self.assertEqual(start["resources"]["smooth_max_batch"], "128")
            self.assertEqual(start["resources"]["sea_threads"], "0")
            self.assert_rejected(
                *common, "--checkpoint", root / "fresh.json"
            )
            self.assert_rejected(
                *common, "--checkpoint", root / "build.json"
            )
            digest = hashlib.sha256((root / "smooth.cache").read_bytes()).hexdigest()
            trusted = self.run_cli(
                *common, "--checkpoint", root / "fresh.json",
                "--smooth-cache-sha256", digest,
            )
            self.assertEqual(trusted.returncode, 0, trusted.stderr)

    def test_search_rejects_aliased_output_paths(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            same = root / "same"
            self.assert_rejected(
                "search", "--p", 101, "--seed", 17,
                "--range-start", 0, "--range-end", 1,
                "--worker-id", 0, "--worker-count", 1,
                "--max-level", 31,
                "--table-dir", ROOT / "data" / "modpoly" / "weber_f",
                "--smooth-cache", same,
                "--checkpoint", same,
                "--max-curves", 0,
            )

    def test_search_rejects_hardlinked_output_paths(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            smooth_cache = root / "smooth.cache"
            checkpoint = root / "checkpoint.json"
            smooth_cache.write_bytes(b"same inode")
            checkpoint.hardlink_to(smooth_cache)
            self.assert_rejected(
                "search", "--p", 101, "--seed", 17,
                "--range-start", 0, "--range-end", 1,
                "--worker-id", 0, "--worker-count", 1,
                "--max-level", 31,
                "--table-dir", ROOT / "data" / "modpoly" / "weber_f",
                "--smooth-cache", smooth_cache,
                "--checkpoint", checkpoint,
                "--max-curves", 0,
            )


if __name__ == "__main__":
    unittest.main()
