#!/usr/bin/env python3
"""Contracts for the production-Weber/Magma corpus audit."""

from __future__ import annotations

import copy
from contextlib import redirect_stderr
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / "build" / "oracle_weber_audit"
BOOTSTRAP = ROOT / "oracle" / "weber_corpus_audit.py"
TABLES = ROOT / "data" / "modpoly" / "weber_f"
MULTILIMB_ARTIFACT = ROOT / "artifacts" / "local" / "weber-oracle-multilimb-20260803"
sys.path.insert(0, str(ROOT / "oracle"))
import weber_corpus_audit_driver as driver  # noqa: E402
import weber_early_abort_audit as early_audit  # noqa: E402
import retained_weber_corpus as retained_corpus  # noqa: E402


FAKE_MAGMA = r"""
#!/usr/bin/env python3
import json, os, sys
if len(sys.argv) == 1:
    print("Magma V2.29-1     fake exact runtime")
    raise SystemExit(0)
p = int(os.environ["ONESHOT_SEA_ORACLE_P"])
if sys.argv[-1].endswith("prime_check.m"):
    result = {"p": p, "is_prime": True}
else:
    a = int(os.environ["ONESHOT_SEA_ORACLE_A"]) % p
    b = int(os.environ["ONESHOT_SEA_ORACLE_B"]) % p
    order = 1
    for x in range(p):
        rhs = (x**3 + a*x + b) % p
        symbol = 0 if rhs == 0 else (1 if pow(rhs, (p-1)//2, p) == 1 else -1)
        order += 1 + symbol
    result = {"p": p, "a": a, "b": b, "order": order,
              "trace": p + 1 - order}
json.dump(result, sys.stdout, separators=(",", ":")); print()
"""


class RetainedMultilimbCorpusTests(unittest.TestCase):
    @staticmethod
    def rewrite_records_and_hashes(
        artifact: Path, raw_records: bytes, record_count: int
    ) -> None:
        records_path = artifact / "raw" / "records.ndjson.gz"
        with gzip.open(records_path, "wb") as stream:
            stream.write(raw_records)
        manifest_path = artifact / "raw" / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["records"].update(
            {
                "sha256": hashlib.sha256(raw_records).hexdigest(),
                "count": record_count,
            }
        )
        manifest_path.write_text(
            json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )
        result_path = artifact / "result.json"
        result = json.loads(result_path.read_text(encoding="utf-8"))
        result["raw"].update(
            {
                "manifest_sha256": hashlib.sha256(
                    manifest_path.read_bytes()
                ).hexdigest(),
                "manifest_bytes": manifest_path.stat().st_size,
                "records_gzip_sha256": hashlib.sha256(
                    records_path.read_bytes()
                ).hexdigest(),
                "records_gzip_bytes": records_path.stat().st_size,
                "records_sha256": hashlib.sha256(raw_records).hexdigest(),
                "records_bytes": len(raw_records),
                "record_count": record_count,
            }
        )
        result_path.write_text(
            json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )

    @staticmethod
    def encode_records(records: list[dict[str, object]]) -> bytes:
        return b"".join(
            json.dumps(record, sort_keys=True, separators=(",", ":")).encode()
            + b"\n"
            for record in records
        )

    def test_replays_complete_64_through_416_bit_evidence(self) -> None:
        summary = retained_corpus.audit_artifact(MULTILIMB_ARTIFACT)
        self.assertEqual(summary["bit_sizes"], [64, 128, 256, 416])
        self.assertEqual(summary["record_count"], 4)
        self.assertEqual(
            summary["claim_counts"],
            {
                "complete_native_counts": 4,
                "exact_elkies": 70,
                "certified_atkin": 4,
                "unconstrained": 64,
                "exact_schoof": 7,
            },
        )

    def test_rejects_semantic_tamper_even_with_recomputed_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary) / "artifact"
            shutil.copytree(MULTILIMB_ARTIFACT, artifact)
            records_path = artifact / "raw" / "records.ndjson.gz"
            with gzip.open(records_path, "rt", encoding="utf-8") as stream:
                records = [json.loads(line) for line in stream]
            exact = next(
                level
                for level in records[0]["native"]["final"]["levels"]
                if level["classification"] == "exact_elkies"
            )
            exact["trace_residue"] = (exact["trace_residue"] + 1) % exact["ell"]
            self.rewrite_records_and_hashes(
                artifact, self.encode_records(records), len(records)
            )
            with self.assertRaisesRegex(
                retained_corpus.RetainedCorpusError,
                "exact Elkies level .* disagrees with Magma",
            ):
                retained_corpus.audit_artifact(artifact)

    def test_rejects_combined_curve_and_candidate_telemetry_tamper(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary) / "artifact"
            shutil.copytree(MULTILIMB_ARTIFACT, artifact)
            records_path = artifact / "raw" / "records.ndjson.gz"
            with gzip.open(records_path, "rt", encoding="utf-8") as stream:
                records = [json.loads(line) for line in stream]
            record = records[0]
            prime = int(record["native"]["p"])
            for path in ("native", "heuristic_fallback_off"):
                curve = record[path]["curve"]
                curve["a"] = str((int(curve["a"]) + 1) % prime)
            early = record["native"]["early"]
            early["exact_trace_candidate_count"] = str(
                int(early["exact_trace_candidate_count"]) + 1
            )
            self.rewrite_records_and_hashes(
                artifact, self.encode_records(records), len(records)
            )
            with self.assertRaisesRegex(
                retained_corpus.RetainedCorpusError,
                "curve is not the canonical production model|candidate count disagrees",
            ):
                retained_corpus.audit_artifact(artifact)

    def test_rejects_candidate_count_tamper_with_recomputed_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary) / "artifact"
            shutil.copytree(MULTILIMB_ARTIFACT, artifact)
            records_path = artifact / "raw" / "records.ndjson.gz"
            with gzip.open(records_path, "rt", encoding="utf-8") as stream:
                records = [json.loads(line) for line in stream]
            early = records[0]["native"]["early"]
            early["exact_trace_candidate_count"] = str(
                int(early["exact_trace_candidate_count"]) + 1
            )
            self.rewrite_records_and_hashes(
                artifact, self.encode_records(records), len(records)
            )
            with self.assertRaisesRegex(
                retained_corpus.RetainedCorpusError,
                "exact candidate count disagrees with replay",
            ):
                retained_corpus.audit_artifact(artifact)

    def test_rejects_newline_free_decompression_bomb_before_json(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary) / "artifact"
            shutil.copytree(MULTILIMB_ARTIFACT, artifact)
            bomb = b"x" * (retained_corpus.MAX_RECORD_BYTES + 1)
            self.rewrite_records_and_hashes(artifact, bomb, 1)
            with self.assertRaisesRegex(
                retained_corpus.RetainedCorpusError,
                "per-record audit limit",
            ):
                retained_corpus.audit_artifact(artifact)

    def test_rejects_oversized_top_level_json_before_parse(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary) / "artifact"
            shutil.copytree(MULTILIMB_ARTIFACT, artifact)
            (artifact / "result.json").write_bytes(
                b"{" + b" " * retained_corpus.MAX_JSON_BYTES + b"}"
            )
            with self.assertRaisesRegex(
                retained_corpus.RetainedCorpusError,
                "retained result exceeds",
            ):
                retained_corpus.audit_artifact(artifact)

    def test_path_swap_to_fifo_after_bounded_manifest_read_is_not_reopened(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary) / "artifact"
            shutil.copytree(MULTILIMB_ARTIFACT, artifact)
            probe = textwrap.dedent(
                """
                import os
                from pathlib import Path
                import sys

                sys.path.insert(0, sys.argv[1])
                import retained_weber_corpus as retained

                artifact = Path(sys.argv[2]).resolve()
                manifest = artifact / "raw" / "manifest.json"
                detached = artifact / "raw" / "manifest.detached.json"
                original = retained.read_bounded_file
                swapped = False

                def read_then_swap(path, label, maximum):
                    global swapped
                    payload = original(path, label, maximum)
                    if path == manifest and not swapped:
                        path.rename(detached)
                        os.mkfifo(path)
                        swapped = True
                    return payload

                retained.read_bounded_file = read_then_swap
                retained.audit_artifact(artifact)
                if not swapped:
                    raise RuntimeError("manifest swap was not exercised")
                """
            )
            completed = subprocess.run(
                [
                    sys.executable,
                    "-c",
                    probe,
                    str(ROOT / "oracle"),
                    str(artifact),
                ],
                cwd=ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
                timeout=15,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)


class WeberCorpusAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not NATIVE.is_file():
            raise unittest.SkipTest("build/oracle_weber_audit is not built")
        completed = subprocess.run(
            [
                str(NATIVE),
                "--p",
                "101",
                "--seed",
                "17",
                "--range-start",
                "0",
                "--count",
                "1",
                "--max-level",
                "5",
                "--trace-cap",
                "4",
                "--sea-threads",
                "1",
                "--table-dir",
                str(TABLES),
                "--schoof-fallback",
                "1",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=30,
        )
        if completed.returncode != 0:
            raise AssertionError(completed.stderr)
        cls.row = json.loads(completed.stdout)
        completed = subprocess.run(
            [
                str(NATIVE),
                "--p",
                "101",
                "--seed",
                "17",
                "--range-start",
                "0",
                "--count",
                "1",
                "--max-level",
                "5",
                "--trace-cap",
                "4",
                "--sea-threads",
                "1",
                "--table-dir",
                str(TABLES),
                "--schoof-fallback",
                "0",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=30,
        )
        if completed.returncode != 0:
            raise AssertionError(completed.stderr)
        cls.fallback_off_row = json.loads(completed.stdout)
        completed = subprocess.run(
            [
                str(NATIVE),
                "--p",
                "101",
                "--seed",
                "17",
                "--range-start",
                "1",
                "--count",
                "1",
                "--max-level",
                "5",
                "--trace-cap",
                "4",
                "--sea-threads",
                "1",
                "--table-dir",
                str(TABLES),
                "--schoof-fallback",
                "1",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=30,
        )
        if completed.returncode != 0:
            raise AssertionError(completed.stderr)
        cls.index_one_row = json.loads(completed.stdout)
        completed = subprocess.run(
            [
                str(NATIVE),
                "--p",
                "191",
                "--seed",
                "202608020003",
                "--range-start",
                "0",
                "--count",
                "1",
                "--max-level",
                "5",
                "--trace-cap",
                "4",
                "--sea-threads",
                "1",
                "--table-dir",
                str(TABLES),
                "--schoof-fallback",
                "1",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=30,
        )
        if completed.returncode != 0:
            raise AssertionError(completed.stderr)
        cls.fallback_row = json.loads(completed.stdout)
        completed = subprocess.run(
            [
                str(NATIVE),
                "--p",
                "42223",
                "--seed",
                "202608020005",
                "--range-start",
                "741",
                "--count",
                "1",
                "--max-level",
                "13",
                "--trace-cap",
                "16",
                "--sea-threads",
                "1",
                "--table-dir",
                str(TABLES),
                "--schoof-fallback",
                "1",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=30,
        )
        if completed.returncode != 0:
            raise AssertionError(completed.stderr)
        cls.supersingular_row = json.loads(completed.stdout)
        completed = subprocess.run(
            [
                str(NATIVE),
                "--p",
                "39367",
                "--seed",
                "202608020005",
                "--range-start",
                "21",
                "--count",
                "1",
                "--max-level",
                "13",
                "--trace-cap",
                "16",
                "--sea-threads",
                "1",
                "--table-dir",
                str(TABLES),
                "--schoof-fallback",
                "1",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=30,
        )
        if completed.returncode != 0:
            raise AssertionError(completed.stderr)
        cls.supersingular_atkin_row = json.loads(completed.stdout)
        completed = subprocess.run(
            [
                str(NATIVE),
                "--p",
                "55313",
                "--seed",
                "202608020005",
                "--range-start",
                "462",
                "--count",
                "1",
                "--max-level",
                "193",
                "--trace-cap",
                "16",
                "--sea-threads",
                "1",
                "--table-dir",
                str(TABLES),
                "--schoof-fallback",
                "0",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=30,
        )
        if completed.returncode != 0:
            raise AssertionError(completed.stderr)
        cls.square_unconstrained_row = json.loads(completed.stdout)

    def validated(self, row: dict[str, object] | None = None) -> dict[str, object]:
        return driver.validate_native_record(
            self.row if row is None else row,
            prime=101,
            seed=17,
            index=0,
            max_level=5,
            trace_cap=4,
            sea_threads=1,
            available_levels={5},
            curve_oracle={"order": 108, "trace": -6},
            twist_oracle={"order": 96, "trace": 6},
        )

    def test_direct_driver_refuses_without_preimport_bootstrap(self) -> None:
        environment = {
            key: value
            for key, value in os.environ.items()
            if not key.startswith("ONESHOTSEA_WEBER_AUDIT_")
        }
        completed = subprocess.run(
            [sys.executable, str(ROOT / "oracle" / "weber_corpus_audit_driver.py")],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            env=environment,
            timeout=10,
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("requires the pre-import bootstrap", completed.stderr)

    def test_full_module_digest_binds_top_level_bootstrap_assignments(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            probe = Path(temporary) / "weber_corpus_audit.py"
            source = BOOTSTRAP.read_text(encoding="utf-8")
            probe.write_text(source, encoding="utf-8")
            original = driver.source_module_code_digest(probe)
            changed = source.replace(
                "REPOSITORY_ROOT = HERE.parent",
                "REPOSITORY_ROOT = HERE.parent.parent",
                1,
            )
            self.assertNotEqual(source, changed)
            probe.write_text(changed, encoding="utf-8")
            self.assertNotEqual(original, driver.source_module_code_digest(probe))

    def test_rejects_unsafe_replay_caps_during_argument_parsing(self) -> None:
        base = [
            "--output-dir",
            "/tmp/unused-weber-audit-output",
            "--magma-runtime",
            "/tmp/unused-magma",
            "--magma-root",
            "/tmp/unused-magma-root",
            "--seed",
            "1",
        ]
        for arguments, expected in (
            (["--trace-cap", str(driver.MAX_TRACE_CAP + 1)], "trace-cap"),
            (
                [
                    "--max-generator-rejections",
                    str(driver.MAX_GENERATOR_REJECTIONS + 1),
                ],
                "max-generator-rejections",
            ),
        ):
            with self.subTest(expected=expected):
                with redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit):
                        driver.parse_args([*base, *arguments])

    def test_accepts_real_atkin_then_retained_fallback_record(self) -> None:
        normalized = self.validated()
        self.assertEqual(normalized["final_exact_trace"], "-6")
        self.assertEqual(
            normalized["early"]["atkin_constraints"],
            [{"ell": 5, "projective_order": 3, "trace_residues": [1, 4]}],
        )
        self.assertNotIn("timings", normalized["early"]["levels"][0])
        self.assertNotIn("elapsed_us", normalized["final"]["fallback_levels"][0])

    def test_validates_fallback_off_second_pass_incompleteness(self) -> None:
        normalized = driver.validate_native_record(
            self.fallback_off_row,
            prime=101,
            seed=17,
            index=0,
            max_level=5,
            trace_cap=4,
            sea_threads=1,
            available_levels={5},
            curve_oracle={"order": 108, "trace": -6},
            twist_oracle={"order": 96, "trace": 6},
            expected_fallback=False,
            require_complete=False,
        )
        self.assertEqual(normalized["early"]["trace_count"], "4")
        self.assertEqual(normalized["final"]["status"], "level_limit")
        self.assertFalse(normalized["complete"])
        self.assertIsNone(normalized["final_exact_trace"])

    def test_accepts_fail_closed_supersingular_atkin_exception(self) -> None:
        normalized = driver.validate_native_record(
            self.supersingular_row,
            prime=42223,
            seed=202608020005,
            index=741,
            max_level=13,
            trace_cap=16,
            sea_threads=1,
            available_levels={5, 7, 11, 13},
            curve_oracle={"order": 42224, "trace": 0},
            twist_oracle={"order": 42224, "trace": 0},
        )
        self.assertEqual(normalized["early"]["levels"][0]["ell"], 5)
        self.assertEqual(
            normalized["early"]["levels"][0]["classification"],
            "unconstrained",
        )
        self.assertEqual(normalized["final_exact_trace"], "0")

    def test_accepts_certified_supersingular_atkin_when_available(self) -> None:
        normalized = driver.validate_native_record(
            self.supersingular_atkin_row,
            prime=39367,
            seed=202608020005,
            index=21,
            max_level=13,
            trace_cap=16,
            sea_threads=1,
            available_levels={5, 7, 11, 13},
            curve_oracle={"order": 39368, "trace": 0},
            twist_oracle={"order": 39368, "trace": 0},
        )
        self.assertEqual(
            normalized["early"]["levels"][0]["classification"],
            "certified_atkin",
        )
        self.assertEqual(
            normalized["early"]["atkin_constraints"][0]["projective_order"],
            2,
        )

    def test_accepts_fail_closed_square_discriminant_without_kernel(self) -> None:
        normalized = driver.validate_native_record(
            self.square_unconstrained_row,
            prime=55313,
            seed=202608020005,
            index=462,
            max_level=193,
            trace_cap=16,
            sea_threads=1,
            available_levels={5, 7, 11, 13, 17, 19},
            curve_oracle={"order": 55236, "trace": 78},
            twist_oracle={"order": 55392, "trace": -78},
            expected_fallback=False,
            require_complete=False,
        )
        levels = {level["ell"]: level for level in normalized["final"]["levels"]}
        self.assertEqual(levels[17]["classification"], "unconstrained")
        self.assertEqual(levels[17]["exact_modulus"], "44")
        self.assertEqual(levels[19]["classification"], "exact_elkies")
        self.assertEqual(normalized["final_exact_trace"], "78")

    def test_independent_projective_order_and_full_atkin_set(self) -> None:
        self.assertEqual(driver.projective_order(5, 101, -6), 3)
        self.assertEqual(driver.atkin_residues(5, 101, 3), (1, 4))

    def test_rejects_relabelled_deterministic_curve_index(self) -> None:
        mutated = copy.deepcopy(self.index_one_row)
        mutated["index"] = "0"
        with self.assertRaisesRegex(driver.AuditError, "deterministic replay"):
            driver.validate_native_record(
                mutated,
                prime=101,
                seed=17,
                index=0,
                max_level=5,
                trace_cap=4,
                sea_threads=1,
                available_levels={5},
                curve_oracle={"order": 112, "trace": -10},
                twist_oracle={"order": 92, "trace": 10},
            )

    def test_validates_full_two_torsion_prior_provenance(self) -> None:
        curve = self.row["curve"]
        self.assertEqual(
            driver.rational_two_torsion_roots(
                101, int(curve["a"]), int(curve["b"])
            ),
            3,
        )
        mutated = copy.deepcopy(self.row)
        mutated["trace_prior"]["residue"] = "1"
        with self.assertRaisesRegex(driver.AuditError, "provenance"):
            self.validated(mutated)

    def test_rejects_incomplete_atkin_residue_set(self) -> None:
        mutated = copy.deepcopy(self.row)
        mutated["early"]["atkin_constraints"][0]["trace_residues"] = [4]
        with self.assertRaisesRegex(driver.AuditError, "complete Atkin residue set"):
            self.validated(mutated)

    def test_rejects_atkin_level_relabelled_as_exact_elkies(self) -> None:
        mutated = copy.deepcopy(self.row)
        level = mutated["early"]["levels"][0]
        level.update(
            {
                "classification": "exact_elkies",
                "exact": True,
                "trace_residue": 4,
                "atkin_projective_order": None,
                "atkin_residue_count": "0",
            }
        )
        with self.assertRaisesRegex(driver.AuditError, "classification disagrees"):
            self.validated(mutated)

    def test_rejects_atkin_level_downgraded_to_unconstrained(self) -> None:
        mutated = copy.deepcopy(self.row)
        level = mutated["early"]["levels"][0]
        level.update(
            {
                "classification": "unconstrained",
                "atkin_projective_order": None,
                "atkin_residue_count": "0",
            }
        )
        with self.assertRaisesRegex(driver.AuditError, "classification disagrees"):
            self.validated(mutated)

    def test_rejects_effective_state_that_does_not_replay(self) -> None:
        mutated = copy.deepcopy(self.row)
        mutated["early"]["effective_residue_classes"] = ["1"]
        with self.assertRaisesRegex(driver.AuditError, "effective state disagrees"):
            self.validated(mutated)

    def test_rejects_fallback_before_all_production_tables(self) -> None:
        mutated = copy.deepcopy(self.fallback_row)
        mutated["max_level"] = "7"
        with self.assertRaisesRegex(driver.AuditError, "before exhausting Weber tables"):
            driver.validate_native_record(
                mutated,
                prime=191,
                seed=202608020003,
                index=0,
                max_level=7,
                trace_cap=4,
                sea_threads=1,
                available_levels={5, 7},
                curve_oracle={"order": 204, "trace": -12},
                twist_oracle={"order": 180, "trace": 12},
            )

    def test_rejects_unrelated_declared_twist(self) -> None:
        mutated = copy.deepcopy(self.row)
        mutated["twist"]["a"] = str((int(mutated["twist"]["a"]) + 1) % 101)
        with self.assertRaisesRegex(driver.AuditError, "twist coefficients"):
            self.validated(mutated)

    def test_rejects_final_nonoracle_singleton(self) -> None:
        mutated = copy.deepcopy(self.row)
        mutated["final_exact_trace"] = "6"
        with self.assertRaisesRegex(driver.AuditError, "exact Magma-trace singleton"):
            self.validated(mutated)

    def test_end_to_end_fake_magma_is_reproducible(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            magma_root = root / "magma-install"
            for directory in ("bin", "package", "libs", "InternalHelp"):
                (magma_root / directory).mkdir(parents=True, exist_ok=True)
            magma = magma_root / "bin" / "magma"
            magma.write_text(textwrap.dedent(FAKE_MAGMA).lstrip(), encoding="utf-8")
            magma.chmod(0o700)
            (magma_root / "package" / "spec").write_text(
                "fake spec\n", encoding="utf-8"
            )
            (magma_root / "magmapassfile").write_text(
                "fake passfile\n", encoding="utf-8"
            )
            digests: list[str] = []
            for name in ("first", "second"):
                output = root / name
                completed = subprocess.run(
                    [
                        sys.executable,
                        str(BOOTSTRAP),
                        "--output-dir",
                        str(output),
                        "--native",
                        str(NATIVE),
                        "--table-dir",
                        str(TABLES),
                        "--magma-runtime",
                        str(magma),
                        "--magma-root",
                        str(magma_root),
                        "--seed",
                        "202608020003",
                        "--bit-sizes",
                        "8",
                        "--curves-per-size",
                        "1",
                        "--max-level",
                        "5",
                        "--trace-cap",
                        "4",
                        "--sea-threads",
                        "1",
                        "--command-timeout-seconds",
                        "30",
                    ],
                    cwd=ROOT,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    check=False,
                    timeout=60,
                )
                self.assertEqual(completed.returncode, 0, completed.stderr)
                manifest = json.loads((output / "manifest.json").read_text())
                self.assertEqual(manifest["status"], "complete")
                self.assertTrue(manifest["identity"]["validated_at_completion"])
                self.assertFalse(
                    manifest["identity"][
                        "nondeterministic_native_timing_fields_archived"
                    ]
                )
                # The contract suite intentionally runs from a dirty tree.
                # Normalize only this temporary fake artifact so the offline
                # clean-source gate can exercise the remaining v2 topology.
                manifest["identity"]["git_worktree_clean_at_start"] = True
                manifest["identity"]["git_worktree_clean_at_completion"] = True
                manifest["identity"]["validated_at_completion"] = True
                (output / "manifest.json").write_text(
                    early_audit.canonical_json(manifest) + "\n",
                    encoding="utf-8",
                )
                record = json.loads((output / "records.ndjson").read_text())
                self.assertTrue(record["native"]["complete"])
                self.assertFalse(
                    record["heuristic_fallback_off"]["schoof_fallback"]
                )
                self.assertEqual(
                    int(record["oracle"]["curve"]["order"])
                    + int(record["oracle"]["twist"]["order"]),
                    2 * int(record["native"]["p"]) + 2,
                )
                audit_report = early_audit.audit_corpus(output)
                self.assertEqual(
                    audit_report["results"]["sound_false_negatives"], 0
                )
                digests.append(manifest["records"]["sha256"])
            self.assertEqual(digests[0], digests[1])


if __name__ == "__main__":
    unittest.main()
