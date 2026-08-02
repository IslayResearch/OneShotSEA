#!/usr/bin/env python3
"""Lightweight contract tests for the streaming oracle corpus driver."""

from __future__ import annotations

import json
import hashlib
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "oracle" / "corpus_audit.py"


FAKE_NATIVE = r"""
#!/usr/bin/env python3
import json, os, sys, time
if os.environ.get("FAKE_NATIVE_SLEEP"):
    started = os.environ.get("FAKE_NATIVE_STARTED_MARKER")
    should_sleep = not started or not os.path.exists(started)
    if started and should_sleep:
        open(started, "w").close()
    if should_sleep:
        time.sleep(2)
        finished = os.environ.get("FAKE_NATIVE_FINISHED_MARKER")
        if finished:
            open(finished, "w").close()
if os.environ.get("FAKE_NATIVE_OVERSIZE"):
    sys.stderr.write("x" * 4096); sys.stderr.flush()
command = sys.argv[1]
options = dict(zip(sys.argv[2::2], sys.argv[3::2]))
p = int(options["--p"])
if command == "curve":
    singular = bool(
        os.environ.get("FAKE_NATIVE_ALWAYS_SINGULAR")
        or (os.environ.get("FAKE_NATIVE_SINGULAR_FIRST") and options["--index"] == "0")
    )
    a, b = (0, 0) if singular else (2, 3)
    result = {"p": str(p), "seed": int(options["--seed"]),
              "index": int(options["--index"]), "a": str(a), "b": str(b),
              "singular": singular}
    if not singular:
        discriminant = (4 * a**3 + 27 * b**2) % p
        result["j"] = str((1728 * 4 * a**3 * pow(discriminant, -1, p)) % p)
elif command == "schoof-residue":
    ell = int(options["--ell"])
    residue = ell if os.environ.get("FAKE_NATIVE_BAD_RESIDUE") else 1 % ell
    if os.environ.get("FAKE_NATIVE_MISMATCH"):
        residue = 2
    if os.environ.get("FAKE_NATIVE_MISMATCH_LEVEL") == str(ell):
        residue = (residue + 1) % ell
    echoed_p = p + 2 if os.environ.get("FAKE_NATIVE_BAD_ECHO") else p
    result = {"p": str(echoed_p), "a": "2", "b": "3", "ell": ell,
              "trace_residue": residue}
elif command == "schoof-count":
    levels = [5,3,7] if os.environ.get("FAKE_NATIVE_BAD_COMPLETE") else [3,5,7]
    result = {"p": str(p), "a": "2", "b": "3", "order": str(p),
              "trace": "1", "residue_modulus": "105", "levels": levels}
else:
    raise SystemExit(2)
encoded = json.dumps(result, separators=(",", ":"))
if os.environ.get("FAKE_NATIVE_DUPLICATE_KEY") and command == "curve":
    encoded = encoded.replace('{"p":', '{"p":"0","p":', 1)
sys.stdout.write(encoded + "\n")
"""


FAKE_MAGMA = r"""
#!/usr/bin/env python3
import json, os, sys
if len(sys.argv) == 1:
    sys.stdout.write("Magma V2.29-1     Darwin arm64\n")
    raise SystemExit(0)
p = int(os.environ["ONESHOT_SEA_ORACLE_P"])
if sys.argv[-1].endswith("prime_check.m"):
    echoed_p = p + 2 if os.environ.get("FAKE_MAGMA_BAD_PRIME_ECHO") else p
    is_prime = not bool(os.environ.get("FAKE_MAGMA_ALWAYS_COMPOSITE"))
    marker = os.environ.get("FAKE_MAGMA_REJECT_FIRST_MARKER")
    if marker and not os.path.exists(marker):
        open(marker, "w").close()
        is_prime = False
    result = {"p":echoed_p,"is_prime":is_prime}
else:
    a = int(os.environ["ONESHOT_SEA_ORACLE_A"]) % p
    b = int(os.environ["ONESHOT_SEA_ORACLE_B"]) % p
    echoed_p = p + 2 if os.environ.get("FAKE_MAGMA_BAD_ECHO") else p
    result = {"p":echoed_p,"a":a,"b":b,"order":p,"trace":1}
json.dump(result, sys.stdout, separators=(",", ":")); sys.stdout.write("\n")
"""


class OracleCorpusAuditTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.native = self.root / "native"
        self.magma = self.root / "magma"
        (self.root / "package").mkdir()
        (self.root / "package" / "spec").write_text("fake spec\n", encoding="utf-8")
        self._write_executable(self.native, FAKE_NATIVE)
        self._write_executable(self.magma, FAKE_MAGMA)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    @staticmethod
    def _write_executable(path: Path, source: str) -> None:
        path.write_text(textwrap.dedent(source).lstrip(), encoding="utf-8")
        path.chmod(0o700)

    def run_driver(
        self,
        output: Path,
        *,
        environment: dict[str, str] | None = None,
        magma: Path | None = None,
        extra_arguments: list[str] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        command = self.driver_command(
            output, magma=magma, extra_arguments=extra_arguments
        )
        merged = os.environ.copy()
        if environment:
            merged.update(environment)
        return subprocess.run(
            command,
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            env=merged,
            timeout=20,
        )

    def driver_command(
        self,
        output: Path,
        *,
        magma: Path | None = None,
        extra_arguments: list[str] | None = None,
    ) -> list[str]:
        command = [
            sys.executable,
            str(DRIVER),
            "--output-dir",
            str(output),
            "--native",
            str(self.native),
            "--magma-runtime",
            str(self.magma if magma is None else magma),
            "--magma-root",
            str(self.root),
            "--seed",
            "20260802",
            "--bit-sizes",
            "8,9",
            "--curves-per-size",
            "2",
            "--complete-count-max-bits",
            "9",
            "--max-ell",
            "19",
            "--residue-levels",
            "3,5",
        ]
        if extra_arguments:
            command.extend(extra_arguments)
        return command

    def test_streams_reproducible_success_manifest(self) -> None:
        first = self.root / "first"
        second = self.root / "second"
        one = self.run_driver(first)
        two = self.run_driver(second)
        self.assertEqual(one.returncode, 0, one.stderr)
        self.assertEqual(two.returncode, 0, two.stderr)
        first_manifest = json.loads((first / "manifest.json").read_text())
        second_manifest = json.loads((second / "manifest.json").read_text())
        self.assertEqual(first_manifest["status"], "complete")
        self.assertEqual(first_manifest["records"]["count"], 4)
        self.assertIn("host", first_manifest["identity"])
        self.assertEqual(first_manifest["identity"]["magma_runtime"]["version"], "Magma V2.29-1")
        self.assertEqual(len(first_manifest["identity"]["magma_runtime_sha256"]), 64)
        self.assertEqual(
            len(first_manifest["identity"]["loaded_corpus_code_sha256"]), 64
        )
        self.assertTrue(first_manifest["identity"]["validated_at_completion"])
        self.assertIn("corpus_bootstrap_sha256", first_manifest["identity"])
        self.assertIn("prime_check_script_sha256", first_manifest["identity"])
        self.assertEqual(
            first_manifest["identity"]["corpus_bootstrap_sha256"],
            hashlib.sha256((first / "inputs" / "corpus_audit.py").read_bytes()).hexdigest(),
        )
        self.assertEqual(
            first_manifest["identity"]["corpus_driver_sha256"],
            hashlib.sha256(
                (first / "inputs" / "corpus_audit_driver.py").read_bytes()
            ).hexdigest(),
        )
        records_bytes = (first / "records.ndjson").read_bytes()
        self.assertEqual(
            first_manifest["records"]["sha256"], hashlib.sha256(records_bytes).hexdigest()
        )
        self.assertEqual(
            first_manifest["records"]["sha256"], second_manifest["records"]["sha256"]
        )
        records = [json.loads(line) for line in (first / "records.ndjson").read_text().splitlines()]
        self.assertEqual([record["requested_bits"] for record in records], [8, 8, 9, 9])
        self.assertTrue(all(record["complete_schoof_count"] for record in records))
        self.assertTrue(
            all(
                [residue["ell"] for residue in record["schoof_residues"]]
                == [3, 5, 7]
                for record in records
            )
        )

    def test_mismatch_fails_closed_with_partial_manifest(self) -> None:
        output = self.root / "mismatch"
        completed = self.run_driver(output, environment={"FAKE_NATIVE_MISMATCH": "1"})
        self.assertNotEqual(completed.returncode, 0)
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertEqual(manifest["status"], "failed")
        self.assertIn("Schoof residue mismatch", manifest["error"])
        self.assertIn("partial_records", manifest)

    def test_deterministically_skips_singular_generated_curve(self) -> None:
        output = self.root / "singular"
        completed = self.run_driver(
            output, environment={"FAKE_NATIVE_SINGULAR_FIRST": "1"}
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        first = json.loads((output / "records.ndjson").read_text().splitlines()[0])
        self.assertEqual(first["curve_index"], "1")
        self.assertEqual(first["singular_curves_skipped"], 1)

    def test_missing_magma_fails_closed(self) -> None:
        output = self.root / "missing"
        completed = self.run_driver(output, magma=self.root / "does-not-exist")
        self.assertNotEqual(completed.returncode, 0)
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertEqual(manifest["status"], "failed")
        self.assertIn("missing or not executable", manifest["error"])

    def test_refuses_to_overwrite_output_directory(self) -> None:
        output = self.root / "existing"
        output.mkdir()
        completed = self.run_driver(output)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("File exists", completed.stderr)

    def test_rejects_mismatched_native_echo(self) -> None:
        output = self.root / "bad-echo"
        completed = self.run_driver(output, environment={"FAKE_NATIVE_BAD_ECHO": "1"})
        self.assertNotEqual(completed.returncode, 0)
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertIn("returned mismatched inputs", manifest["error"])

    def test_rejects_invalid_completion_metadata(self) -> None:
        output = self.root / "bad-completion"
        completed = self.run_driver(
            output, environment={"FAKE_NATIVE_BAD_COMPLETE": "1"}
        )
        self.assertNotEqual(completed.returncode, 0)
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertIn("returned invalid levels", manifest["error"])

    def test_rejects_noncanonical_residue(self) -> None:
        output = self.root / "bad-residue"
        completed = self.run_driver(
            output, environment={"FAKE_NATIVE_BAD_RESIDUE": "1"}
        )
        self.assertNotEqual(completed.returncode, 0)
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertIn("is noncanonical", manifest["error"])

    def test_audits_every_complete_count_level(self) -> None:
        output = self.root / "missing-level-residue"
        completed = self.run_driver(
            output, environment={"FAKE_NATIVE_MISMATCH_LEVEL": "7"}
        )
        self.assertNotEqual(completed.returncode, 0)
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertIn("Schoof residue mismatch", manifest["error"])

    def test_rejects_duplicate_json_keys(self) -> None:
        output = self.root / "duplicate-key"
        completed = self.run_driver(
            output, environment={"FAKE_NATIVE_DUPLICATE_KEY": "1"}
        )
        self.assertNotEqual(completed.returncode, 0)
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertIn("duplicate key", manifest["error"])

    def test_exact_prime_gate_binds_magma_response(self) -> None:
        output = self.root / "bad-prime-echo"
        completed = self.run_driver(
            output, environment={"FAKE_MAGMA_BAD_PRIME_ECHO": "1"}
        )
        self.assertNotEqual(completed.returncode, 0)
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertIn("prime validation returned a mismatched input", manifest["error"])

    def test_prime_generation_attempt_limit_fails_closed(self) -> None:
        output = self.root / "prime-attempt-limit"
        completed = self.run_driver(
            output,
            environment={"FAKE_MAGMA_ALWAYS_COMPOSITE": "1"},
            extra_arguments=["--max-prime-attempts", "5"],
        )
        self.assertNotEqual(completed.returncode, 0)
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertIn("prime generation exhausted 5 candidates", manifest["error"])

    def test_exact_prime_rejection_resamples_deterministically(self) -> None:
        first = self.root / "prime-resample-first"
        second = self.root / "prime-resample-second"
        first_marker = self.root / "first-prime-rejection"
        second_marker = self.root / "second-prime-rejection"
        one = self.run_driver(
            first, environment={"FAKE_MAGMA_REJECT_FIRST_MARKER": str(first_marker)}
        )
        two = self.run_driver(
            second, environment={"FAKE_MAGMA_REJECT_FIRST_MARKER": str(second_marker)}
        )
        self.assertEqual(one.returncode, 0, one.stderr)
        self.assertEqual(two.returncode, 0, two.stderr)
        first_manifest = json.loads((first / "manifest.json").read_text())
        second_manifest = json.loads((second / "manifest.json").read_text())
        self.assertEqual(
            first_manifest["records"]["sha256"], second_manifest["records"]["sha256"]
        )
        record = json.loads((first / "records.ndjson").read_text().splitlines()[0])
        self.assertGreater(record["prime_generation_attempts"], 2)

    def test_subprocess_timeout_fails_closed(self) -> None:
        output = self.root / "timeout"
        completed = self.run_driver(
            output,
            environment={"FAKE_NATIVE_SLEEP": "1"},
            extra_arguments=["--command-timeout-seconds", "1"],
        )
        self.assertNotEqual(completed.returncode, 0)
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertIn("timed out after 1 seconds", manifest["error"])

    def test_subprocess_output_cap_fails_closed(self) -> None:
        output = self.root / "oversize"
        completed = self.run_driver(
            output,
            environment={"FAKE_NATIVE_OVERSIZE": "1"},
            extra_arguments=["--max-output-bytes", "1024"],
        )
        self.assertNotEqual(completed.returncode, 0)
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertIn("output exceeded 1024 bytes", manifest["error"])

    def test_interrupt_kills_child_and_records_partial_manifest(self) -> None:
        output = self.root / "interrupted"
        started = self.root / "child-started"
        finished = self.root / "child-finished"
        environment = os.environ.copy()
        environment.update(
            {
                "FAKE_NATIVE_SLEEP": "1",
                "FAKE_NATIVE_STARTED_MARKER": str(started),
                "FAKE_NATIVE_FINISHED_MARKER": str(finished),
            }
        )
        process = subprocess.Popen(
            self.driver_command(output),
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=environment,
        )
        deadline = time.monotonic() + 5
        while not started.exists() and process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.02)
        self.assertTrue(started.exists(), "fake native child did not start")
        process.send_signal(signal.SIGINT)
        stdout, stderr = process.communicate(timeout=5)
        self.assertEqual(process.returncode, 130, (stdout, stderr))
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertEqual(manifest["status"], "interrupted")
        self.assertIn("partial_records", manifest)
        time.sleep(2.2)
        self.assertFalse(finished.exists(), "interrupted native child survived")

    def test_final_uint64_index_reports_exhausted_cursor(self) -> None:
        output = self.root / "final-index"
        completed = self.run_driver(
            output,
            extra_arguments=[
                "--bit-sizes",
                "8",
                "--curves-per-size",
                "1",
                "--start-index",
                str((1 << 64) - 1),
            ],
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertIsNone(manifest["records"]["next_curve_index"])

    def test_singular_retry_cannot_cross_uint64_boundary(self) -> None:
        output = self.root / "singular-final-index"
        completed = self.run_driver(
            output,
            environment={"FAKE_NATIVE_ALWAYS_SINGULAR": "1"},
            extra_arguments=[
                "--bit-sizes",
                "8",
                "--curves-per-size",
                "1",
                "--start-index",
                str((1 << 64) - 1),
            ],
        )
        self.assertNotEqual(completed.returncode, 0)
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertIn("retry would exceed UINT64_MAX", manifest["error"])

    def test_input_drift_fails_closed_after_snapshot_execution(self) -> None:
        output = self.root / "input-drift"
        started = self.root / "drift-child-started"
        environment = os.environ.copy()
        environment.update(
            {
                "FAKE_NATIVE_SLEEP": "1",
                "FAKE_NATIVE_STARTED_MARKER": str(started),
            }
        )
        process = subprocess.Popen(
            self.driver_command(
                output,
                extra_arguments=["--bit-sizes", "8", "--curves-per-size", "1"],
            ),
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=environment,
        )
        deadline = time.monotonic() + 5
        while not started.exists() and process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.02)
        self.assertTrue(started.exists(), "snapshotted fake native child did not start")
        self.native.write_text(self.native.read_text() + "\n# drift\n", encoding="utf-8")
        stdout, stderr = process.communicate(timeout=10)
        self.assertNotEqual(process.returncode, 0, (stdout, stderr))
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertIn("native source identity changed", manifest["error"])

    def test_rejects_invalid_residue_level_before_creating_output(self) -> None:
        output = self.root / "bad-level"
        completed = self.run_driver(
            output, extra_arguments=["--residue-levels", "9"]
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertFalse(output.exists())
        self.assertIn("residue-levels must be odd primes", completed.stderr)


if __name__ == "__main__":
    unittest.main()
