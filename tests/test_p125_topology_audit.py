#!/usr/bin/env python3

import ast
import calendar
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import audit_p125_topology as topology  # noqa: E402


BINARY_PATH = (
    ROOT
    / "artifacts/runpod/p125-runpod-cpu16-replay-550815e-20260802a"
    / "ohfo3hbov7ot8v/binaries/candidate.bin"
)
VERIFIER_SHA256 = "e0ba3b8a7ed2ff48bd2fd824642bf67b0954a9f03f57daeb4ac4302691e1b666"
PYTHON_SHA256 = "298a9e830ed52f36c299427565485d717d1ce0179c0597cc16560513eb780b06"


class TopologyAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not BINARY_PATH.is_file():
            raise AssertionError("retained candidate binary is required by topology tests")
        if topology.runpod._sha256_file(BINARY_PATH) != topology.BINARY_SHA256:
            raise AssertionError("retained candidate binary digest drift")
        cls.producer = topology._binary_producers(BINARY_PATH)[0]

    def _write_json(self, path, value):
        path.write_text(
            json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )

    def _write_jsonl(self, path, values):
        path.write_text(
            "".join(
                json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n"
                for value in values
            ),
            encoding="utf-8",
        )

    def _checksum(self, root):
        lines = []
        for path in sorted(root.rglob("*")):
            if path.is_file() and path.name != "SHA256SUMS":
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                lines.append("{}  ./{}\n".format(digest, path.relative_to(root)))
        (root / "SHA256SUMS").write_text("".join(lines), encoding="utf-8")

    def _utc(self, epoch):
        return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(epoch))

    def _command(self, run_id, worker_id, worker_count, start, end):
        assigned = topology.runpod._partition(start, end, worker_id, worker_count)
        worker = topology.REMOTE_RUNS + "/{}/worker-{}".format(run_id, worker_id)
        threads = 8 if worker_count == 2 else 16
        return [
            "/workspace/OneShotSEA-550815e/build/oneshotsea",
            "search",
            "--p",
            topology.TARGET_PRIME,
            "--seed",
            topology.TARGET_SEED,
            "--range-start",
            str(start),
            "--range-end",
            str(end),
            "--worker-id",
            str(worker_id),
            "--worker-count",
            str(worker_count),
            "--max-level",
            "401",
            "--table-dir",
            "/workspace/OneShotSEA-550815e/data/modpoly/weber_f",
            "--smooth-cache",
            "/workspace/OneShotSEA/caches/p125.cache",
            "--smooth-cache-sha256",
            topology.CACHE_SHA256,
            "--checkpoint",
            worker + "/checkpoint.json",
            "--progress",
            worker + "/progress.jsonl",
            "--certificate-out",
            worker + "/certificate.txt",
            "--build-id",
            topology.BUILD_ID,
            "--curve-family",
            "x1-27",
            "--x1-require-point4",
            "1",
            "--curve-threads",
            str(threads),
            "--sea-level-telemetry",
            "0",
            "--schoof-fallback",
            "1",
            "--skip-incomplete-curves",
            "0",
            "--smooth-coordinators",
            "0",
            "--max-curves",
            str(assigned["count"]),
            "--checkpoint-every",
            "1",
            "--trace-cap",
            "16",
            "--sea-threads",
            "1",
            "--smooth-threads",
            "1",
            "--smooth-max-batch",
            "128",
            "--smooth-root-auxiliary-bytes",
            "134217728",
            "--smooth-build-segment-span",
            "4000000000",
            "--assembly-attempts",
            "400",
            "--max-certificate-candidates",
            "100000",
            "--max-candidate-search-nodes",
            "1000000",
        ]

    def _identity(self, worker_id, worker_count, assigned):
        return {
            "schema": "oneshotsea.search-progress.v1",
            "prime": topology.TARGET_PRIME,
            "seed": topology.TARGET_SEED,
            "worker_id": str(worker_id),
            "worker_count": str(worker_count),
            "range_start": str(assigned["start"]),
            "range_end": str(assigned["end"]),
            "schedule_sha256": topology.SCHEDULE_SHA256,
            "table_manifest_sha256": topology.TABLE_MANIFEST_SHA256,
            "build_id": topology.BUILD_ID,
        }

    def _records(self, assigned, worker_id, worker_count, rss_bytes):
        identity = self._identity(worker_id, worker_count, assigned)
        counters = {name: 0 for name in topology.runpod.CHECKPOINT_COUNTERS}
        records = []
        for index in range(assigned["start"], assigned["end"]):
            full = (index % 2) == 0
            counters["curves_attempted"] += 1
            counters["rejected_sound_early_abort"] += 1
            counters["full_point_counts_completed"] += int(full)
            counters["candidates_reaching_smoothness"] += 1
            state = topology.runpod._expected_state(
                identity,
                counters,
                index + 1,
                index + 1 == assigned["end"],
            )
            records.append(
                {
                    "schema": "oneshotsea.search-curve.v1",
                    "index": str(index),
                    "status": "sound_smoothness_reject",
                    "peak_rss_bytes": str(rss_bytes),
                    "heuristic": False,
                    "outcome_class": "sound_rejection",
                    "sound_early_abort": True,
                    "full_point_count": full,
                    "reached_smoothness": True,
                    "generator_rejections": str(index % 17),
                    "trace_prior": {"modulus": "432", "residue": "14"},
                    "sea_passes": "1",
                    "sea_levels": "50",
                    "exact_sea_levels": "30",
                    "atkin_sea_levels": "1",
                    "schoof_fallback_level_count": "0",
                    "initial_trace_count": "1",
                    "final_exact_trace_candidates": "1",
                    "final_trace_candidates": "1",
                    "trace": str(index * 2 + 1),
                    "candidate_attempts": "0",
                    "candidate_search_nodes": "0",
                    "assembly_calls": "0",
                    "canonical_rejections": "0",
                    "timings_us": {
                        "generation": "100",
                        "sea": str(1000 + worker_count),
                        "smoothness": "800",
                        "candidate": "0",
                        "assembly": "0",
                        "verifier": "0",
                        "total": str(2000 + worker_count),
                    },
                    "sea_level_timings": [],
                    "schoof_fallback_levels": [],
                    "state": state,
                }
            )
        return records, counters, identity

    def _checkpoint(self, path, identity, next_index, counters):
        fields = [
            ("schema_version", "1", False),
            ("prime", identity["prime"], True),
            ("seed", identity["seed"], True),
            ("worker_id", identity["worker_id"], True),
            ("worker_count", identity["worker_count"], True),
            ("range_start", identity["range_start"], True),
            ("range_end", identity["range_end"], True),
            ("schedule_sha256", identity["schedule_sha256"], True),
            ("table_manifest_sha256", identity["table_manifest_sha256"], True),
            ("build_id", identity["build_id"], True),
            ("next_index", str(next_index), True),
        ]
        prefix = "{" + ",".join(
            '"{}":{}'.format(name, json.dumps(value) if quoted else value)
            for name, value, quoted in fields
        )
        encoded_counters = ",".join(
            '"{}":"{}"'.format(name, counters[name])
            for name in topology.runpod.CHECKPOINT_COUNTERS
        )
        payload = (prefix + ',"counters":{' + encoded_counters + "}}").encode("utf-8")
        crc = topology.runpod._crc64_ecma(payload)
        path.write_bytes(payload[:-1] + ',"crc64_ecma":"{:016x}"}}\n'.format(crc).encode("ascii"))

    def _start_record(self, worker_id, worker_count, assigned):
        threads = 8 if worker_count == 2 else 16
        resources = dict(topology.START_RESOURCE_VALUES)
        resources["curve_threads"] = str(threads)
        return {
            "schema": "oneshotsea.search-start.v1",
            "prime": topology.TARGET_PRIME,
            "seed": topology.TARGET_SEED,
            "curve_family": "x1-27",
            "worker_id": str(worker_id),
            "worker_count": str(worker_count),
            "range_start": str(assigned["start"]),
            "range_end": str(assigned["end"]),
            "next_index": str(assigned["start"]),
            "schedule_sha256": topology.SCHEDULE_SHA256,
            "table_manifest_sha256": topology.TABLE_MANIFEST_SHA256,
            "smooth_cache_sha256": topology.CACHE_SHA256,
            "verifier_sha256": VERIFIER_SHA256,
            "python_executable": "/usr/bin/python3.8",
            "python_sha256": PYTHON_SHA256,
            "build_id": topology.BUILD_ID,
            "heuristic_rejection": False,
            "resources": resources,
        }

    def _summary(self, records):
        return {
            "schema": "oneshotsea.search-summary.v1",
            "processed": str(len(records)),
            "range_exhausted": True,
            "verified": False,
            "smooth_batch": {
                "enabled": False,
                "coordinator_count": "0",
                "submitted_requests": "0",
                "completed_requests": "0",
                "failed_requests": "0",
                "cancelled_requests": "0",
                "coordinator_batches": "0",
                "successful_cache_scan_chunks": "0",
                "submitted_orders": "0",
                "max_queued_requests_in_any_cohort": "0",
                "max_requests_per_batch_in_any_cohort": "0",
                "max_orders_per_successful_scan_chunk_in_any_cohort": "0",
                "successful_scan_chunk_size_histogram": [],
                "cohorts": [],
            },
            "state": records[-1]["state"],
        }

    def _command_text(self, argv, resource_path, log_path):
        launch = topology.runpod._expected_launch(argv, topology.WALL_TIME_LIMIT_SECONDS)
        return (
            "#!/usr/bin/env bash\nset -uo pipefail\n"
            "/usr/bin/time -a -v -o {} -- {} >>{} 2>&1\n".format(
                shlex.quote(str(resource_path)),
                shlex.join(launch),
                shlex.quote(str(log_path)),
            )
        )

    def _resource(self, start, elapsed, argv, rss_kib, swaps=0, status=0):
        launch = topology.runpod._expected_launch(argv, topology.WALL_TIME_LIMIT_SECONDS)
        minutes = int(elapsed // 60)
        seconds = elapsed - minutes * 60
        return (
            "attempt_start utc={} epoch={}\n"
            "\tCommand being timed: \"{}\"\n"
            "\tElapsed (wall clock) time (h:mm:ss or m:ss): {}:{:05.2f}\n"
            "\tMaximum resident set size (kbytes): {}\n"
            "\tSwaps: {}\n"
            "\tExit status: {}\n"
        ).format(
            self._utc(start),
            start,
            shlex.join(launch),
            minutes,
            seconds,
            rss_kib,
            swaps,
            status,
        )

    def _run_root(
        self,
        root,
        label,
        start,
        end,
        elapsed,
        worker_starts,
        rss_kib=10 * 1024 * 1024,
        swaps=0,
        status=0,
    ):
        root.mkdir()
        topology_name = topology.RUN_SPECS[label][0]
        worker_count = 2 if topology_name == "B" else 1
        run_id = "p125-topology-{}-550815e-20260803a".format(label)
        run_end = 0
        for worker_id in range(worker_count):
            assigned = topology.runpod._partition(start, end, worker_id, worker_count)
            directory = root / "worker-{}".format(worker_id)
            directory.mkdir()
            argv = self._command(run_id, worker_id, worker_count, start, end)
            worker_elapsed = elapsed - (4 if worker_count == 2 and worker_id == 1 else 0)
            worker_start = worker_starts[worker_id]
            worker_end = int(round(worker_start + worker_elapsed))
            run_end = max(run_end, worker_end)
            records, counters, identity = self._records(
                assigned, worker_id, worker_count, rss_kib * 1024
            )
            progress_path = directory / "progress.jsonl"
            self._write_jsonl(progress_path, records)
            self._checkpoint(directory / "checkpoint.json", identity, assigned["end"], counters)
            start_record = self._start_record(worker_id, worker_count, assigned)
            self._write_jsonl(
                directory / "worker.log", [start_record] + records + [self._summary(records)]
            )
            resource_path = directory / "resource-usage.txt"
            resource_path.write_text(
                self._resource(
                    worker_start, worker_elapsed, argv, rss_kib, swaps=swaps, status=status
                ),
                encoding="utf-8",
            )
            command_text = self._command_text(argv, resource_path, directory / "worker.log")
            (directory / "command.sh").write_text(command_text, encoding="utf-8")
            (directory / "command.sh").chmod(0o700)
            self._write_jsonl(
                directory / "attempts.jsonl",
                [
                    {"event": "start", "utc": self._utc(worker_start), "epoch": worker_start},
                    {
                        "event": "end",
                        "utc": self._utc(worker_end),
                        "epoch": worker_end,
                        "status": status,
                    },
                ],
            )
            manifest = {
                "schema": "oneshotsea.runpod-worker.v3",
                "run_id": run_id,
                "run_kind": "benchmark",
                "worker_id": worker_id,
                "worker_count": worker_count,
                "global_range": {
                    "start": str(start),
                    "end": str(end),
                    "count": str(end - start),
                },
                "assigned_range": {
                    "start": str(assigned["start"]),
                    "end": str(assigned["end"]),
                    "count": str(assigned["count"]),
                },
                "seed": topology.TARGET_SEED,
                "prime": topology.TARGET_PRIME,
                "deployment_commit": topology.DEPLOYMENT_COMMIT,
                "binary_sha256": topology.BINARY_SHA256,
                "build_id": topology.BUILD_ID,
                "wall_time_limit_seconds": topology.WALL_TIME_LIMIT_SECONDS,
                "command_sha256": hashlib.sha256(command_text.encode("utf-8")).hexdigest(),
                "command_argv": argv,
                "started_utc": self._utc(worker_start),
            }
            self._write_json(directory / "manifest.json", manifest)
        self._write_json(
            root / "fetch-metadata.json",
            {
                "schema": 1,
                "fetched_at": self._utc(run_end + 5),
                "run_id": run_id,
                "pod_id": "test-pod",
                "remote_source": topology.REMOTE_RUNS + "/" + run_id,
                "pod_state": None,
                "estimated_current_session_compute_cost_usd": None,
                "estimate_note": "test",
            },
        )
        self._checksum(root)

    def _fixture(self, directory, timings=None, rss_kib=10 * 1024 * 1024):
        roots = {name: directory / name for name in ("bx", "ax", "ay", "by")}
        values = {
            "bx": (90.0, [1000, 1002]),
            "ax": (100.0, [1100]),
            "ay": (90.0, [1210]),
            "by": (80.0, [1310, 1312]),
        }
        if timings:
            values.update(timings)
        self._run_root(
            roots["bx"], "bx", 1000827, 1000891, values["bx"][0], values["bx"][1], rss_kib
        )
        self._run_root(
            roots["ax"], "ax", 1000827, 1000891, values["ax"][0], values["ax"][1], rss_kib
        )
        self._run_root(
            roots["ay"], "ay", 1000891, 1000955, values["ay"][0], values["ay"][1], rss_kib
        )
        self._run_root(
            roots["by"], "by", 1000891, 1000955, values["by"][0], values["by"][1], rss_kib
        )
        provenance = directory / "build-provenance.json"
        self._write_json(
            provenance,
            {
                "schema": topology.BUILD_PROVENANCE_SCHEMA,
                "deployment_commit": topology.DEPLOYMENT_COMMIT,
                "binary_sha256": topology.BINARY_SHA256,
                "build_command": "g++ -std=c++20 -O2 -g -o oneshotsea main.cpp",
                "cxx_compiler_version": "GCC 11.5.0",
                "cxx_producer": self.producer,
            },
        )
        return roots, provenance

    def _audit(self, roots, provenance):
        return topology.audit(
            roots["bx"],
            roots["ax"],
            roots["ay"],
            roots["by"],
            provenance,
            BINARY_PATH,
            ROOT,
        )

    def _refresh_command(self, root, worker_id=0):
        directory = root / "worker-{}".format(worker_id)
        manifest_path = directory / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        resource_path = directory / "resource-usage.txt"
        command_text = self._command_text(
            manifest["command_argv"], resource_path, directory / "worker.log"
        )
        (directory / "command.sh").write_text(command_text, encoding="utf-8")
        manifest["command_sha256"] = hashlib.sha256(command_text.encode()).hexdigest()
        self._write_json(manifest_path, manifest)
        resource = resource_path.read_text()
        launch = topology.runpod._expected_launch(
            manifest["command_argv"], topology.WALL_TIME_LIMIT_SECONDS
        )
        resource = re.sub(
            r'^\s*Command being timed: ".*"\s*$',
            '\tCommand being timed: "{}"'.format(shlex.join(launch)),
            resource,
            flags=re.MULTILINE,
        )
        resource_path.write_text(resource, encoding="utf-8")
        self._checksum(root)

    def test_passes_and_supports_output_and_result_validation(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            roots, provenance = self._fixture(directory)
            result = self._audit(roots, provenance)
            self.assertTrue(result["accepted"])
            self.assertEqual(result["pairs"]["x"]["records_per_side"], 64)
            self.assertGreater(result["speedup_geometric_mean"], 1.05)

            output = directory / "result.json"
            command = [
                sys.executable,
                str(ROOT / "tools/audit_p125_topology.py"),
                str(roots["bx"]),
                str(roots["ax"]),
                str(roots["ay"]),
                str(roots["by"]),
                "--build-provenance",
                str(provenance),
                "--binary",
                str(BINARY_PATH),
                "--source-repo",
                str(ROOT),
                "--output",
                str(output),
            ]
            completed = subprocess.run(command, check=False, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            self.assertEqual(json.loads(completed.stdout), json.loads(output.read_text()))
            validated = subprocess.run(
                command[:-2] + ["--result", str(output)],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(validated.returncode, 0, validated.stdout + validated.stderr)

    def test_rejects_wrong_binary_workload_and_option_policy(self):
        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            manifest_path = roots["ax"] / "worker-0/manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["prime"] = "101"
            argv = manifest["command_argv"]
            argv[argv.index("--p") + 1] = "101"
            self._write_json(manifest_path, manifest)
            self._refresh_command(roots["ax"])
            with self.assertRaises(topology.AuditError):
                self._audit(roots, provenance)

        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            manifest_path = roots["ax"] / "worker-0/manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["command_argv"][0] = "/bin/true"
            self._write_json(manifest_path, manifest)
            self._refresh_command(roots["ax"])
            with self.assertRaisesRegex(topology.AuditError, r"argv\[0\]"):
                self._audit(roots, provenance)

        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            manifest_path = roots["ax"] / "worker-0/manifest.json"
            manifest = json.loads(manifest_path.read_text())
            argv = manifest["command_argv"]
            argv[argv.index("--max-level") + 1] = "193"
            self._write_json(manifest_path, manifest)
            self._refresh_command(roots["ax"])
            with self.assertRaisesRegex(topology.AuditError, "fixed command option"):
                self._audit(roots, provenance)

        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            manifest_path = roots["ax"] / "worker-0/manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["command_argv"].extend(["--unexpected", "1"])
            self._write_json(manifest_path, manifest)
            self._refresh_command(roots["ax"])
            with self.assertRaisesRegex(topology.AuditError, "unexpected option set"):
                self._audit(roots, provenance)

    def test_rejects_output_escape_and_missing_certificate_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            manifest_path = roots["ax"] / "worker-0/manifest.json"
            manifest = json.loads(manifest_path.read_text())
            argv = manifest["command_argv"]
            argv[argv.index("--checkpoint") + 1] = "/other/worker-0/checkpoint.json"
            self._write_json(manifest_path, manifest)
            self._refresh_command(roots["ax"])
            with self.assertRaisesRegex(topology.AuditError, "variable command option"):
                self._audit(roots, provenance)

        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            manifest_path = roots["ax"] / "worker-0/manifest.json"
            manifest = json.loads(manifest_path.read_text())
            argv = manifest["command_argv"]
            position = argv.index("--certificate-out")
            del argv[position : position + 2]
            self._write_json(manifest_path, manifest)
            self._refresh_command(roots["ax"])
            with self.assertRaises(topology.AuditError):
                self._audit(roots, provenance)

    def test_rejects_command_attempt_timing_and_fetch_tampering(self):
        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            command = roots["bx"] / "worker-0/command.sh"
            command.write_text(command.read_text() + "true\n")
            self._checksum(roots["bx"])
            with self.assertRaisesRegex(topology.AuditError, "command SHA-256"):
                self._audit(roots, provenance)

        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            resource = roots["bx"] / "worker-0/resource-usage.txt"
            resource.write_text(resource.read_text().replace("--max-level 401", "--max-level 193"))
            self._checksum(roots["bx"])
            with self.assertRaisesRegex(topology.AuditError, "GNU-time argv"):
                self._audit(roots, provenance)

        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            attempts = roots["ax"] / "worker-0/attempts.jsonl"
            values = [json.loads(line) for line in attempts.read_text().splitlines()]
            values[1]["utc"] = self._utc(values[1]["epoch"] + 86400)
            self._write_jsonl(attempts, values)
            self._checksum(roots["ax"])
            with self.assertRaisesRegex(topology.AuditError, "UTC and epoch differ"):
                self._audit(roots, provenance)

        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            fetch = roots["by"] / "fetch-metadata.json"
            value = json.loads(fetch.read_text())
            value["run_id"] = "other"
            self._write_json(fetch, value)
            self._checksum(roots["by"])
            with self.assertRaisesRegex(topology.AuditError, "fetch metadata run_id"):
                self._audit(roots, provenance)

    def test_accepts_git_normalized_command_mode_but_rejects_other_modes(self):
        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            for root in roots.values():
                for command in root.glob("worker-*/command.sh"):
                    command.chmod(0o755)
            self._audit(roots, provenance)

            command = roots["bx"] / "worker-0/command.sh"
            command.chmod(0o777)
            with self.assertRaisesRegex(topology.AuditError, "Git-normalized mode"):
                self._audit(roots, provenance)

    def test_rejects_malformed_progress_heuristics_and_log_drift(self):
        for attack in ("index-only", "heuristic", "garbage", "certificate"):
            with self.subTest(attack=attack), tempfile.TemporaryDirectory() as temporary:
                roots, provenance = self._fixture(Path(temporary))
                progress = roots["ax"] / "worker-0/progress.jsonl"
                records = [json.loads(line) for line in progress.read_text().splitlines()]
                if attack == "index-only":
                    records[0] = {"index": records[0]["index"]}
                elif attack == "heuristic":
                    records[0]["heuristic"] = True
                elif attack == "garbage":
                    records[0]["status"] = "garbage"
                else:
                    records[0]["certificate"] = {}
                self._write_jsonl(progress, records)
                self._checksum(roots["ax"])
                with self.assertRaises(topology.AuditError):
                    self._audit(roots, provenance)

        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            log = roots["ax"] / "worker-0/worker.log"
            values = [json.loads(line) for line in log.read_text().splitlines()]
            values[1]["outcome_class"] = "different"
            self._write_jsonl(log, values)
            self._checksum(roots["ax"])
            with self.assertRaisesRegex(topology.AuditError, "curve records differ"):
                self._audit(roots, provenance)

    def test_rejects_checkpoint_identity_and_crc_tampering(self):
        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            checkpoint = roots["ax"] / "worker-0/checkpoint.json"
            encoded = checkpoint.read_text()
            checkpoint.write_text(encoded.replace(topology.TARGET_PRIME, "101"))
            self._checksum(roots["ax"])
            with self.assertRaisesRegex(topology.AuditError, "CRC64"):
                self._audit(roots, provenance)

        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            checkpoint = roots["ax"] / "worker-0/checkpoint.json"
            value = json.loads(checkpoint.read_text())
            identity = {
                "prime": "101",
                "seed": value["seed"],
                "worker_id": value["worker_id"],
                "worker_count": value["worker_count"],
                "range_start": value["range_start"],
                "range_end": value["range_end"],
                "schedule_sha256": value["schedule_sha256"],
                "table_manifest_sha256": value["table_manifest_sha256"],
                "build_id": value["build_id"],
            }
            counters = {
                name: int(value["counters"][name])
                for name in topology.runpod.CHECKPOINT_COUNTERS
            }
            self._checkpoint(checkpoint, identity, int(value["next_index"]), counters)
            self._checksum(roots["ax"])
            with self.assertRaisesRegex(topology.AuditError, "checkpoint identity"):
                self._audit(roots, provenance)

    def test_rejects_nonchronological_and_nonconcurrent_brackets(self):
        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(
                Path(temporary),
                timings={
                    "bx": (90.0, [1000, 1002]),
                    "ax": (100.0, [1050]),
                    "ay": (90.0, [1210]),
                    "by": (80.0, [1310, 1312]),
                },
            )
            with self.assertRaisesRegex(topology.AuditError, "chronology"):
                self._audit(roots, provenance)

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            roots, provenance = self._fixture(directory)
            shutil.rmtree(roots["bx"])
            self._run_root(
                roots["bx"], "bx", 1000827, 1000891, 90.0, [1000, 1031]
            )
            proved = topology._build_provenance(provenance, BINARY_PATH, ROOT)
            with self.assertRaisesRegex(topology.AuditError, "launch skew"):
                topology._run(roots["bx"], "bx", "B", "x", proved)

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            roots, provenance = self._fixture(directory)
            shutil.rmtree(roots["bx"])
            self._run_root(
                roots["bx"], "bx", 1000827, 1000891, 90.0, [1000, 1030]
            )
            proved = topology._build_provenance(provenance, BINARY_PATH, ROOT)
            run = topology._run(roots["bx"], "bx", "B", "x", proved)
            self.assertEqual(run["attempt_start_epoch"], 1000)

    def test_rejects_unpinned_or_nonadjacent_range(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            roots, provenance = self._fixture(directory)
            shutil.rmtree(roots["ay"])
            self._run_root(roots["ay"], "ay", 2000, 2064, 90.0, [1210])
            proved = topology._build_provenance(provenance, BINARY_PATH, ROOT)
            with self.assertRaises(topology.AuditError):
                topology._run(roots["ay"], "ay", "A", "y", proved)

    def test_rejects_declarative_or_weak_build_provenance(self):
        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            value = json.loads(provenance.read_text())
            value["build_command"] = "g++ -O2 -O0 main.cpp"
            self._write_json(provenance, value)
            with self.assertRaisesRegex(topology.AuditError, "final effective optimization"):
                self._audit(roots, provenance)

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            roots, provenance = self._fixture(directory)
            wrong = directory / "wrong.bin"
            wrong.write_bytes(b"GNU C++20 11.5.0 -O2\n")
            with self.assertRaisesRegex(topology.AuditError, "binary SHA-256"):
                topology.audit(
                    roots["bx"], roots["ax"], roots["ay"], roots["by"],
                    provenance, wrong, ROOT,
                )

        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            with self.assertRaisesRegex(topology.AuditError, "source repository"):
                topology.audit(
                    roots["bx"], roots["ax"], roots["ay"], roots["by"],
                    provenance, BINARY_PATH, Path(temporary) / "missing",
                )

    def test_rejects_semantic_mismatch_after_valid_rehash(self):
        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            path = roots["ax"] / "worker-0/progress.jsonl"
            records = [json.loads(line) for line in path.read_text().splitlines()]
            records[12]["trace"] = "999"
            self._write_jsonl(path, records)
            log = roots["ax"] / "worker-0/worker.log"
            values = [json.loads(line) for line in log.read_text().splitlines()]
            values[13]["trace"] = "999"
            self._write_jsonl(log, values)
            self._checksum(roots["ax"])
            with self.assertRaisesRegex(topology.AuditError, "semantic projections differ"):
                self._audit(roots, provenance)

    def test_strict_threshold_boundaries_and_resource_failures(self):
        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(
                Path(temporary),
                timings={"bx": (100.0, [1000, 1002]), "ax": (100.0, [1101])},
            )
            result = self._audit(roots, provenance)
            self.assertFalse(result["accepted"])
            self.assertFalse(result["gates"]["pair_x_speedup_gt_1"])

        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(
                Path(temporary),
                timings={
                    "bx": (100.0, [1000, 1002]),
                    "ax": (105.0, [1110]),
                    "ay": (105.0, [1230]),
                    "by": (100.0, [1350, 1352]),
                },
            )
            result = self._audit(roots, provenance)
            self.assertAlmostEqual(result["speedup_geometric_mean"], 1.05)
            self.assertFalse(result["gates"]["geometric_mean_speedup_gt_1_05"])

        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(
                Path(temporary), rss_kib=24 * 1024 * 1024
            )
            result = self._audit(roots, provenance)
            self.assertEqual(
                result["pairs"]["x"]["dual_sum_peak_rss_bytes"], 48 * 1024 ** 3
            )
            self.assertFalse(result["pairs"]["x"]["gates"]["dual_sum_peak_rss_lt_48_gib"])

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            roots, provenance = self._fixture(directory)
            resource = roots["bx"] / "worker-0/resource-usage.txt"
            resource.write_text(resource.read_text().replace("\tSwaps: 0", "\tSwaps: 1"))
            self._checksum(roots["bx"])
            with self.assertRaisesRegex(topology.AuditError, "swapped"):
                self._audit(roots, provenance)

        with tempfile.TemporaryDirectory() as temporary:
            roots, provenance = self._fixture(Path(temporary))
            attempts = roots["bx"] / "worker-0/attempts.jsonl"
            values = [json.loads(line) for line in attempts.read_text().splitlines()]
            values[1]["status"] = 1
            self._write_jsonl(attempts, values)
            resource = roots["bx"] / "worker-0/resource-usage.txt"
            resource.write_text(resource.read_text().replace("\tExit status: 0", "\tExit status: 1"))
            self._checksum(roots["bx"])
            with self.assertRaisesRegex(topology.AuditError, "unexpected exit status"):
                self._audit(roots, provenance)

    def test_actual_bx_ax_survive_hardened_partial_pair(self):
        bx_root = (
            ROOT
            / "artifacts/runpod/p125-topology-bx-550815e-20260803a"
            / "ohfo3hbov7ot8v"
        )
        ax_root = (
            ROOT
            / "artifacts/runpod/p125-topology-ax-550815e-20260803a"
            / "ohfo3hbov7ot8v"
        )
        self.assertTrue(bx_root.is_dir())
        self.assertTrue(ax_root.is_dir())
        with tempfile.TemporaryDirectory() as temporary:
            provenance = Path(temporary) / "build-provenance.json"
            self._write_json(
                provenance,
                {
                    "schema": topology.BUILD_PROVENANCE_SCHEMA,
                    "deployment_commit": topology.DEPLOYMENT_COMMIT,
                    "binary_sha256": topology.BINARY_SHA256,
                    "build_command": "g++ -std=c++20 -O2 -g -o oneshotsea main.cpp",
                    "cxx_compiler_version": "GCC 11.5.0",
                    "cxx_producer": self.producer,
                },
            )
            proved = topology._build_provenance(provenance, BINARY_PATH, ROOT)
            bx = topology._run(bx_root, "bx", "B", "x", proved)
            ax = topology._run(ax_root, "ax", "A", "x", proved)
            pair = topology._pair("x", bx, ax)
            self.assertAlmostEqual(pair["single_over_dual_speedup"], 1.1354678111)
            self.assertEqual(pair["dual_sum_peak_rss_bytes"], 21610180608)

    def test_python38_grammar(self):
        for path in (
            ROOT / "tools/audit_p125_topology.py",
            ROOT / "tests/test_p125_topology_audit.py",
        ):
            ast.parse(path.read_text(encoding="utf-8"), filename=str(path), feature_version=(3, 8))


if __name__ == "__main__":
    unittest.main()
