#!/usr/bin/env python3

import ast
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import audit_runpod_search as search_audit  # noqa: E402
import audit_search_coverage as coverage_audit  # noqa: E402


COMMIT = "a" * 40
BINARY = "b" * 64
CACHE = "c" * 64
SCHEDULE = "d" * 64
TABLES = "e" * 64
PYTHON_SHA = "f" * 64
PRIME = "101"
SEED = "202607300000"
REMOTE_ROOT = "/workspace/OneShotSEA"
VERIFIER = ROOT / "third_party" / "oneshot_primality_proofs" / "voneshot.py"
COUNTERS = search_audit.CHECKPOINT_COUNTERS


class RunPodSearchAuditTests(unittest.TestCase):
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
                lines.append(
                    "{}  ./{}\n".format(
                        hashlib.sha256(path.read_bytes()).hexdigest(),
                        path.relative_to(root),
                    )
                )
        (root / "SHA256SUMS").write_text("".join(lines), encoding="utf-8")

    def _checkpoint_text(self, identity, next_index, counters):
        payload = (
            '{{"schema_version":1,"prime":"{}","seed":"{}",'
            '"worker_id":"{}","worker_count":"{}","range_start":"{}",'
            '"range_end":"{}","schedule_sha256":"{}",'
            '"table_manifest_sha256":"{}","build_id":"{}",'
            '"next_index":"{}","counters":{{{}}}}}'
        ).format(
            identity["prime"], identity["seed"], identity["worker_id"],
            identity["worker_count"], identity["range_start"],
            identity["range_end"], identity["schedule_sha256"],
            identity["table_manifest_sha256"], identity["build_id"],
            next_index,
            ",".join('"{}":"{}"'.format(name, counters[name]) for name in COUNTERS),
        )
        crc = search_audit._crc64_ecma(payload.encode("utf-8"))
        return payload[:-1] + ',"crc64_ecma":"{:016x}"}}\n'.format(crc)

    def _state_counters(self, counters):
        return {
            "curves_attempted": str(counters["curves_attempted"]),
            "rejections": {
                "invalid_curve": str(counters["rejected_invalid_curve"]),
                "sea": str(counters["rejected_sea"]),
                "sound_early_abort": str(counters["rejected_sound_early_abort"]),
                "heuristic": str(counters["rejected_heuristic"]),
                "certificate_assembly": str(counters["rejected_certificate_assembly"]),
            },
            "completed_without_certificate": str(counters["completed_without_certificate"]),
            "full_point_counts_completed": str(counters["full_point_counts_completed"]),
            "candidates_reaching_smoothness": str(counters["candidates_reaching_smoothness"]),
            "certificates_found": str(counters["certificates_found"]),
        }

    def _state(self, identity, counters, next_index, assigned_end):
        state = dict(identity)
        state.update({
            "schema": "oneshotsea.search-progress.v1",
            "next_index": str(next_index),
            "complete": next_index == assigned_end,
            "counters": self._state_counters(counters),
        })
        return state

    def _policy(self, skip="0", telemetry="0", curve_threads="2"):
        return {
            "--max-level": "401",
            "--table-dir": REMOTE_ROOT + "/data/modpoly/weber_f",
            "--smooth-cache": REMOTE_ROOT + "/caches/p125.cache",
            "--curve-family": "x1-27",
            "--x1-require-point4": "1",
            "--curve-threads": curve_threads,
            "--sea-level-telemetry": telemetry,
            "--schoof-fallback": "1",
            "--skip-incomplete-curves": skip,
            "--smooth-coordinators": "0",
            "--checkpoint-every": "1",
            "--trace-cap": "16",
            "--sea-threads": "1",
            "--smooth-threads": "1",
            "--smooth-max-batch": "128",
            "--smooth-root-auxiliary-bytes": "134217728",
            "--smooth-build-segment-span": "4000000000",
            "--assembly-attempts": "400",
            "--max-certificate-candidates": "100000",
            "--max-candidate-search-nodes": "1000000",
        }

    def _command(self, run_id, worker_id, worker_count, start, end, assigned_count, policy):
        worker = "{}/runs/{}/worker-{}".format(REMOTE_ROOT, run_id, worker_id)
        build_id = "git:{}+binary-sha256:{}".format(COMMIT, BINARY)
        command = [
            REMOTE_ROOT + "/build/oneshotsea", "search",
            "--p", PRIME, "--seed", SEED,
            "--range-start", str(start), "--range-end", str(end),
            "--worker-id", str(worker_id), "--worker-count", str(worker_count),
            "--max-level", policy["--max-level"],
            "--table-dir", policy["--table-dir"],
            "--smooth-cache", policy["--smooth-cache"],
            "--smooth-cache-sha256", CACHE,
            "--checkpoint", worker + "/checkpoint.json",
            "--progress", worker + "/progress.jsonl",
            "--certificate-out", worker + "/certificate.txt",
            "--build-id", build_id,
        ]
        for name in (
            "--curve-family", "--x1-require-point4", "--curve-threads",
            "--sea-level-telemetry", "--schoof-fallback",
            "--skip-incomplete-curves", "--smooth-coordinators",
        ):
            command.extend([name, policy[name]])
        command.extend(["--max-curves", str(assigned_count)])
        for name in (
            "--checkpoint-every", "--trace-cap", "--sea-threads",
            "--smooth-threads", "--smooth-max-batch",
            "--smooth-root-auxiliary-bytes", "--smooth-build-segment-span",
            "--assembly-attempts", "--max-certificate-candidates",
            "--max-candidate-search-nodes",
        ):
            command.extend([name, policy[name]])
        return command

    def _canonical_script(self, worker, launch):
        join = " ".join(shlex.quote(item) for item in launch)
        attempts = worker + "/attempts.jsonl"
        resource = worker + "/resource-usage.txt"
        log = worker + "/worker.log"
        return (
            "#!/usr/bin/env bash\nset -uo pipefail\ncd {}\n"
            "started_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)\n"
            "started_epoch=$(date +%s)\n"
            "printf '{{\"event\":\"start\",\"utc\":\"%s\",\"epoch\":%s}}\n' "
            "\"$started_utc\" \"$started_epoch\" >>{}\n"
            "printf 'attempt_start utc=%s epoch=%s\n' \"$started_utc\" "
            "\"$started_epoch\" >>{}\nset +e\n"
            "/usr/bin/time -a -v -o {} -- {} >>{} 2>&1\n"
            "status=$?\nset -e\n"
            "ended_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)\n"
            "ended_epoch=$(date +%s)\n"
            "printf '{{\"event\":\"end\",\"utc\":\"%s\",\"epoch\":%s,"
            "\"status\":%s}}\n' \"$ended_utc\" \"$ended_epoch\" "
            "\"$status\" >>{}\nexit \"$status\"\n"
        ).format(
            REMOTE_ROOT, attempts, resource, resource, join, log, attempts)

    def _utc(self, epoch):
        return datetime.fromtimestamp(epoch, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    def _resource_block(self, launch, start_epoch, elapsed, status, rss_kib):
        lines = ["attempt_start utc={} epoch={}".format(self._utc(start_epoch), start_epoch)]
        if status:
            lines.append("Command exited with non-zero status {}".format(status))
        values = {
            "Command being timed": '"{}"'.format(" ".join(shlex.quote(x) for x in launch)),
            "User time (seconds)": "4.00", "System time (seconds)": "0.25",
            "Percent of CPU this job got": "85%",
            "Elapsed (wall clock) time (h:mm:ss or m:ss)": "0:{:05.2f}".format(float(elapsed)),
            "Average shared text size (kbytes)": "0",
            "Average unshared data size (kbytes)": "0",
            "Average stack size (kbytes)": "0", "Average total size (kbytes)": "0",
            "Maximum resident set size (kbytes)": str(rss_kib),
            "Average resident set size (kbytes)": "0",
            "Major (requiring I/O) page faults": "0",
            "Minor (reclaiming a frame) page faults": "10",
            "Voluntary context switches": "2", "Involuntary context switches": "3",
            "Swaps": "0", "File system inputs": "0", "File system outputs": "8",
            "Socket messages sent": "0", "Socket messages received": "0",
            "Signals delivered": "0", "Page size (bytes)": "4096",
            "Exit status": str(status),
        }
        lines.extend("\t{}: {}".format(name, values[name]) for name in search_audit.GNU_TIME_FIELDS)
        return "\n".join(lines) + "\n"

    def _start_record(self, identity, policy, next_index):
        return {
            "schema": "oneshotsea.search-start.v1", "prime": PRIME, "seed": SEED,
            "curve_family": policy["--curve-family"],
            "worker_id": identity["worker_id"], "worker_count": identity["worker_count"],
            "range_start": identity["range_start"], "range_end": identity["range_end"],
            "next_index": str(next_index), "schedule_sha256": SCHEDULE,
            "table_manifest_sha256": TABLES, "smooth_cache_sha256": CACHE,
            "verifier_sha256": search_audit.PINNED_VERIFIER_SHA256,
            "python_executable": "/usr/bin/python3.8", "python_sha256": PYTHON_SHA,
            "build_id": identity["build_id"],
            "heuristic_rejection": policy["--skip-incomplete-curves"] == "1",
            "resources": {
                "smooth_threads": policy["--smooth-threads"],
                "smooth_max_batch": policy["--smooth-max-batch"],
                "smooth_root_auxiliary_bytes": policy["--smooth-root-auxiliary-bytes"],
                "smooth_build_segment_span": policy["--smooth-build-segment-span"],
                "curve_threads": policy["--curve-threads"],
                "smooth_coordinators": policy["--smooth-coordinators"],
                "x1_require_point_four": policy["--x1-require-point4"] == "1",
                "skip_incomplete_curves": policy["--skip-incomplete-curves"] == "1",
                "schoof_fallback": policy["--schoof-fallback"] == "1",
                "sea_level_telemetry": policy["--sea-level-telemetry"] == "1",
                "sea_threads": policy["--sea-threads"],
                "assembly_attempts": policy["--assembly-attempts"],
                "trace_cap": policy["--trace-cap"],
                "max_certificate_candidates": policy["--max-certificate-candidates"],
                "max_candidate_search_nodes": policy["--max-candidate-search-nodes"],
            },
        }

    def _smooth_batch(self):
        return {
            "enabled": False, "coordinator_count": "0", "submitted_requests": "0",
            "completed_requests": "0", "failed_requests": "0",
            "cancelled_requests": "0", "coordinator_batches": "0",
            "successful_cache_scan_chunks": "0", "submitted_orders": "0",
            "max_queued_requests_in_any_cohort": "0",
            "max_requests_per_batch_in_any_cohort": "0",
            "max_orders_per_successful_scan_chunk_in_any_cohort": "0",
            "successful_scan_chunk_size_histogram": [], "cohorts": [],
        }

    def _curve(self, identity, counters, index, status, assigned_end):
        semantics = search_audit.STATUS_SEMANTICS[status]
        full = semantics["full"] if semantics["full"] is not None else False
        smooth = semantics["smooth"] if semantics["smooth"] is not None else True
        if semantics["advances"]:
            counters["curves_attempted"] += 1
            counters[semantics["counter"]] += 1
            counters["full_point_counts_completed"] += int(full)
            counters["candidates_reaching_smoothness"] += int(smooth)
            next_index = index + 1
        else:
            next_index = index
        certificate = status == "verified_certificate"
        record = {
            "schema": "oneshotsea.search-curve.v1", "index": str(index),
            "status": status, "peak_rss_bytes": "1048576",
            "heuristic": semantics["heuristic"],
            "outcome_class": semantics["outcome_class"],
            "sound_early_abort": semantics["sound"], "full_point_count": full,
            "reached_smoothness": smooth, "generator_rejections": "1",
            "trace_prior": None, "sea_passes": "1", "sea_levels": "1",
            "exact_sea_levels": "1", "atkin_sea_levels": "0",
            "schoof_fallback_level_count": "0", "initial_trace_count": "1",
            "candidate_attempts": "1" if certificate else "0",
            "candidate_search_nodes": "1" if certificate else "0",
            "assembly_calls": "1" if certificate else "0",
            "canonical_rejections": "0",
            "timings_us": {
                "generation": "1", "sea": "2", "smoothness": "3",
                "candidate": "1" if certificate else "0",
                "assembly": "1" if certificate else "0",
                "verifier": "1" if certificate else "0",
                "total": "9" if certificate else "6",
            },
            "sea_level_timings": [], "schoof_fallback_levels": [],
            "state": self._state(identity, counters, next_index, assigned_end),
        }
        if full:
            record["trace"] = "0"
            record["final_exact_trace_candidates"] = "1"
            record["final_trace_candidates"] = "1"
        if certificate:
            record["certificate"] = {
                "order_source": "curve", "odd_only": True,
                "montgomery_side": "curve", "line": "101 3 24 24",
            }
        return record, next_index

    def _worker(self, root, run_id, worker_id, worker_count, start, end,
                attempt_statuses, segments, wall_limit, policy):
        assigned = search_audit._partition(start, end, worker_id, worker_count)
        directory = root / "worker-{}".format(worker_id)
        directory.mkdir()
        remote_worker = "{}/runs/{}/worker-{}".format(REMOTE_ROOT, run_id, worker_id)
        command = self._command(
            run_id, worker_id, worker_count, start, end, assigned["count"], policy)
        launch = search_audit._expected_launch(command, wall_limit)
        script = self._canonical_script(remote_worker, launch)
        (directory / "command.sh").write_text(script, encoding="utf-8")
        build_id = "git:{}+binary-sha256:{}".format(COMMIT, BINARY)
        identity = {
            "prime": PRIME, "seed": SEED, "worker_id": str(worker_id),
            "worker_count": str(worker_count), "range_start": str(assigned["start"]),
            "range_end": str(assigned["end"]), "schedule_sha256": SCHEDULE,
            "table_manifest_sha256": TABLES, "build_id": build_id,
        }
        counters = {name: 0 for name in COUNTERS}
        cursor = assigned["start"]
        progress = []
        worker_log = []
        attempts = []
        resources = []
        base_epoch = 1785690000 + worker_id * 1000
        certificate_record = None
        for attempt_number, (status_code, statuses) in enumerate(zip(attempt_statuses, segments)):
            elapsed = wall_limit if status_code == 124 else 5
            start_epoch = base_epoch + attempt_number * 20
            end_epoch = start_epoch + elapsed
            attempts.extend([
                {"event": "start", "utc": self._utc(start_epoch), "epoch": start_epoch},
                {"event": "end", "utc": self._utc(end_epoch), "epoch": end_epoch,
                 "status": status_code},
            ])
            resources.append(self._resource_block(
                launch, start_epoch, elapsed, status_code, 1024 + worker_id))
            worker_log.append(self._start_record(identity, policy, cursor))
            advanced = 0
            found = False
            for status in statuses:
                record, next_cursor = self._curve(
                    identity, counters, cursor, status, assigned["end"])
                advanced += int(next_cursor != cursor)
                cursor = next_cursor
                found = found or status == "verified_certificate"
                if found:
                    certificate_record = record
                progress.append(record)
                worker_log.append(record)
            if status_code == 0:
                worker_log.append({
                    "schema": "oneshotsea.search-summary.v1",
                    "processed": str(advanced),
                    "range_exhausted": cursor == assigned["end"], "verified": found,
                    "smooth_batch": self._smooth_batch(),
                    "state": self._state(identity, counters, cursor, assigned["end"]),
                })
        manifest = {
            "schema": "oneshotsea.runpod-worker.v3", "run_id": run_id,
            "run_kind": "production" if 124 in attempt_statuses else "benchmark",
            "worker_id": worker_id, "worker_count": worker_count,
            "global_range": {"start": str(start), "end": str(end), "count": str(end-start)},
            "assigned_range": {"start": str(assigned["start"]),
                               "end": str(assigned["end"]),
                               "count": str(assigned["count"])},
            "seed": SEED, "prime": PRIME, "deployment_commit": COMMIT,
            "binary_sha256": BINARY, "build_id": build_id,
            "wall_time_limit_seconds": wall_limit,
            "command_sha256": hashlib.sha256(script.encode()).hexdigest(),
            "command_argv": command, "started_utc": attempts[0]["utc"],
        }
        self._write_json(directory / "manifest.json", manifest)
        self._write_jsonl(directory / "attempts.jsonl", attempts)
        (directory / "resource-usage.txt").write_text("".join(resources), encoding="utf-8")
        self._write_jsonl(directory / "progress.jsonl", progress)
        self._write_jsonl(directory / "worker.log", worker_log)
        (directory / "checkpoint.json").write_text(
            self._checkpoint_text(identity, cursor, counters), encoding="utf-8")
        if certificate_record is not None:
            line = certificate_record["certificate"]["line"]
            certificate_path = directory / "certificate.txt"
            certificate_path.write_text(line + "\n", encoding="utf-8")
            self._write_json(directory / "certificate.txt.meta.json", {
                "schema": "oneshotsea.certificate-binding.v1", "prime": PRIME,
                "seed": SEED, "worker_id": str(worker_id),
                "worker_count": str(worker_count),
                "range_start": str(assigned["start"]), "range_end": str(assigned["end"]),
                "schedule_sha256": SCHEDULE, "table_manifest_sha256": TABLES,
                "build_id": build_id, "global_index": certificate_record["index"],
                "certificate_sha256": hashlib.sha256((line + "\n").encode()).hexdigest(),
                "certificate_line": line,
            })
        return {
            "assigned": assigned, "cursor": cursor, "counters": counters,
            "statuses": attempt_statuses,
        }

    def _fixture(self, directory, worker_count=1, start=100, end=104,
                 worker_attempts=None, worker_segments=None, wall_limit=0,
                 policy=None, include_fetch=False, run_id="fixture-run"):
        root = directory / "pod-test"
        root.mkdir()
        policy = policy or self._policy()
        if worker_attempts is None:
            worker_attempts = [[0] for _ in range(worker_count)]
        if worker_segments is None:
            worker_segments = []
            for worker_id in range(worker_count):
                count = search_audit._partition(start, end, worker_id, worker_count)["count"]
                worker_segments.append([["sound_smoothness_reject"] * count])
        facts = []
        for worker_id in range(worker_count):
            facts.append(self._worker(
                root, run_id, worker_id, worker_count, start, end,
                worker_attempts[worker_id], worker_segments[worker_id],
                wall_limit, policy))
        aggregate = {name: sum(fact["counters"][name] for fact in facts) for name in COUNTERS}
        profile = {
            "schema": search_audit.PROFILE_SCHEMA, "run_id": run_id,
            "run_kind": "production" if any(124 in x for x in worker_attempts) else "benchmark",
            "prime": PRIME, "seed": SEED, "deployment_commit": COMMIT,
            "binary_sha256": BINARY, "smooth_cache_sha256": CACHE,
            "schedule_sha256": SCHEDULE, "table_manifest_sha256": TABLES,
            "verifier_sha256": search_audit.PINNED_VERIFIER_SHA256,
            "python_executable": "/usr/bin/python3.8", "python_sha256": PYTHON_SHA,
            "remote_root": REMOTE_ROOT, "working_directory": REMOTE_ROOT,
            "executable_path": REMOTE_ROOT + "/build/oneshotsea",
            "worker_count": worker_count,
            "global_range": {"start": str(start), "end": str(end), "count": str(end-start)},
            "wall_time_limit_seconds": wall_limit, "option_policy": policy,
            "allow_resume": any(len(x) > 1 for x in worker_attempts),
            "allow_timeout_124": any(124 in x for x in worker_attempts),
            "expected_attempt_statuses": worker_attempts,
            "expected_checkpoint_next_indices": [fact["cursor"] for fact in facts],
            "minimum_completed_count": aggregate["curves_attempted"],
            "expected_counters": aggregate,
            "minimum_aggregate_curves_per_hour": 0.0,
            "maximum_worker_rss_bytes": (1024 + worker_count - 1) * 1024,
            "maximum_swaps_sum": 0,
            "require_all_assigned_ranges_exhausted": all(
                fact["cursor"] == fact["assigned"]["end"] for fact in facts),
        }
        profile_path = directory / "profile.json"
        self._write_json(profile_path, profile)
        if include_fetch:
            last_end = max(
                json.loads((root / "worker-{}".format(wid) / "attempts.jsonl").read_text().splitlines()[-1])["epoch"]
                for wid in range(worker_count))
            self._write_json(root / "fetch-metadata.json", {
                "schema": 1, "fetched_at": self._utc(last_end + 1),
                "run_id": run_id, "pod_id": root.name,
                "remote_source": "{}/runs/{}".format(REMOTE_ROOT, run_id),
                "pod_state": None, "estimated_current_session_compute_cost_usd": None,
                "estimate_note": "fixture",
            })
        self._checksum(root)
        return root, profile_path, profile

    def _audit(self, root, profile, verifier=None):
        return search_audit.audit(
            root, profile=profile, canonical_verifier=verifier)

    def _actual_profile(self, directory, root):
        directory.mkdir(parents=True, exist_ok=True)
        manifests = [
            json.loads((root / "worker-{}".format(worker_id) / "manifest.json").read_text())
            for worker_id in range(json.loads((root / "worker-0/manifest.json").read_text())["worker_count"])
        ]
        first = manifests[0]
        start = json.loads((root / "worker-0/worker.log").read_text().splitlines()[0])
        _, options = search_audit._argv_options(first["command_argv"], "actual")
        policy = {name: value for name, value in options.items()
                  if name not in search_audit.DERIVED_OPTIONS}
        statuses = []
        cursors = []
        counters = {name: 0 for name in COUNTERS}
        max_rss = 0
        for worker_id in range(first["worker_count"]):
            worker = root / "worker-{}".format(worker_id)
            attempts = [json.loads(line) for line in (worker / "attempts.jsonl").read_text().splitlines()]
            statuses.append([attempts[i]["status"] for i in range(1, len(attempts), 2)])
            checkpoint = json.loads((worker / "checkpoint.json").read_text())
            cursors.append(int(checkpoint["next_index"]))
            for name in COUNTERS:
                counters[name] += int(checkpoint["counters"][name])
            for line in (worker / "resource-usage.txt").read_text().splitlines():
                if "Maximum resident set size" in line:
                    max_rss = max(max_rss, int(line.split(":", 1)[1]) * 1024)
        profile = {
            "schema": search_audit.PROFILE_SCHEMA, "run_id": first["run_id"],
            "run_kind": first["run_kind"], "prime": first["prime"], "seed": first["seed"],
            "deployment_commit": first["deployment_commit"],
            "binary_sha256": first["binary_sha256"],
            "smooth_cache_sha256": options["--smooth-cache-sha256"],
            "schedule_sha256": start["schedule_sha256"],
            "table_manifest_sha256": start["table_manifest_sha256"],
            "verifier_sha256": start["verifier_sha256"],
            "python_executable": start["python_executable"],
            "python_sha256": start["python_sha256"], "remote_root": REMOTE_ROOT,
            "working_directory": REMOTE_ROOT, "executable_path": first["command_argv"][0],
            "worker_count": first["worker_count"], "global_range": first["global_range"],
            "wall_time_limit_seconds": first["wall_time_limit_seconds"],
            "option_policy": policy, "allow_resume": any(len(x) > 1 for x in statuses),
            "allow_timeout_124": any(124 in x for x in statuses),
            "expected_attempt_statuses": statuses,
            "expected_checkpoint_next_indices": cursors,
            "minimum_completed_count": counters["curves_attempted"],
            "expected_counters": counters,
            "minimum_aggregate_curves_per_hour": 0.0,
            "maximum_worker_rss_bytes": max_rss, "maximum_swaps_sum": 0,
            "require_all_assigned_ranges_exhausted": all(
                cursor == search_audit._partition(
                    int(first["global_range"]["start"]), int(first["global_range"]["end"]),
                    worker_id, first["worker_count"])["end"]
                for worker_id, cursor in enumerate(cursors)),
        }
        path = directory / "actual-profile.json"
        self._write_json(path, profile)
        return path

    def test_clean_profile_cli_and_deterministic_result(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root, profile, _ = self._fixture(directory, include_fetch=True)
            result = self._audit(root, profile)
            self.assertTrue(result["accepted"])
            self.assertTrue(result["structural_integrity"]["accepted"])
            self.assertTrue(result["declared_outcome_gate"]["accepted"])
            output = directory / "audit.json"
            command = [sys.executable, str(ROOT / "tools/audit_runpod_search.py"),
                       str(root), "--profile", str(profile), "--output", str(output)]
            completed = subprocess.run(command, check=False, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            self.assertEqual(json.loads(completed.stdout), json.loads(output.read_text()))
            retained = subprocess.run(
                command[:-2] + ["--result", str(output)],
                check=False, capture_output=True, text=True)
            self.assertEqual(retained.returncode, 0, retained.stdout + retained.stderr)
            missing = subprocess.run(
                [sys.executable, str(ROOT / "tools/audit_runpod_search.py"), str(root)],
                check=False, capture_output=True, text=True)
            self.assertNotEqual(missing.returncode, 0)

    def test_profile_admits_workspace_sibling_deployment_but_not_broad_root(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root, profile, _ = self._fixture(directory)
            value = json.loads(profile.read_text())
            value["remote_root"] = "/workspace/OneShotSEA"
            value["working_directory"] = "/workspace/OneShotSEA-deadbeef"
            value["executable_path"] = "/workspace/OneShotSEA-deadbeef/build/oneshotsea"
            value["option_policy"]["--table-dir"] = \
                "/workspace/OneShotSEA-deadbeef/data/modpoly/weber_f"
            value["option_policy"]["--smooth-cache"] = \
                "/workspace/OneShotSEA/caches/p125.cache"
            self._write_json(profile, value)
            parsed, _ = search_audit._profile(profile, root)
            self.assertEqual(parsed["remote_root"], "/workspace/OneShotSEA")

            value["remote_root"] = "/workspace"
            self._write_json(profile, value)
            with self.assertRaisesRegex(search_audit.AuditError, "unsafe parent"):
                search_audit._profile(profile, root)

    def test_attempt_start_may_follow_manifest_by_one_second(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "attempts.jsonl"
            self._write_jsonl(path, [
                {"event": "start", "utc": self._utc(101), "epoch": 101},
                {"event": "end", "utc": self._utc(102), "epoch": 102,
                 "status": 0},
            ])
            attempts = search_audit._attempts(path, "worker-1", self._utc(100))
            self.assertEqual(attempts[0]["start_epoch"], 101)
            with self.assertRaisesRegex(search_audit.AuditError, "within 60 seconds"):
                search_audit._attempts(path, "worker-1", self._utc(40))

    def test_actual_probe_and_797_timeout_with_external_profiles(self):
        roots = [
            ROOT / "artifacts/runpod/p125-runpod-cpu16-probe-20260802b/ohfo3hbov7ot8v",
            ROOT / "artifacts/runpod/p125-runpod-cpu16-prod-20260802b/ohfo3hbov7ot8v",
        ]
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            results = []
            for number, root in enumerate(roots):
                profile = self._actual_profile(directory / str(number), root)
                results.append(self._audit(root, profile))
            self.assertEqual(results[0]["outcome"]["completed_global_index_count"], 30)
            self.assertEqual(results[1]["outcome"]["completed_global_index_count"], 797)
            self.assertEqual(results[1]["resources"]["attempt_exit_statuses"], [[124]])

    def test_retained_partial_dual_epoch_matches_external_profile_and_result(self):
        parent = ROOT / "artifacts/runpod/p125-production-dual8-550815e-20260803a"
        root = parent / "ohfo3hbov7ot8v"
        profile = parent / "audit-profile.json"
        retained = json.loads((parent / "audit-result.json").read_text())
        recomputed = self._audit(root, profile)
        self.assertEqual(recomputed, retained)
        self.assertTrue(recomputed["accepted"])
        self.assertEqual(recomputed["outcome"]["completed_global_index_count"], 546)
        self.assertEqual(recomputed["outcome"]["remaining_intervals"], [
            {"start": 1001373, "end": 1001551},
        ])
        self.assertEqual(recomputed["outcome"]["counters"]["rejected_heuristic"], 0)
        self.assertEqual(recomputed["outcome"]["certificates"], [])

    def test_retained_exact_recovery_matches_external_profile_and_result(self):
        parent = ROOT / (
            "artifacts/runpod/"
            "p125-recovery-1001373-batch15x1024-550815e-20260803a")
        root = parent / "ohfo3hbov7ot8v"
        profile = parent / "audit-profile.json"
        retained = json.loads((parent / "audit-result.json").read_text())
        recomputed = self._audit(root, profile)
        self.assertEqual(recomputed, retained)
        self.assertTrue(recomputed["accepted"])
        self.assertTrue(recomputed["outcome"]["all_assigned_ranges_exhausted"])
        self.assertEqual(recomputed["outcome"]["completed_intervals"], [
            {"start": 1001373, "end": 1001374},
        ])
        self.assertEqual(
            recomputed["outcome"]["counters"]["rejected_heuristic"], 0)
        self.assertEqual(recomputed["outcome"]["certificates"], [])

    def test_command_wrapper_option_path_and_identity_rehash_attacks(self):
        mutators = []

        def extra(root):
            path = root / "worker-0/command.sh"
            path.write_text(path.read_text() + "echo untrusted >/tmp/extra\n")
            manifest_path = root / "worker-0/manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["command_sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
            self._write_json(manifest_path, manifest)
        mutators.append(extra)

        def option(root):
            manifest_path = root / "worker-0/manifest.json"
            manifest = json.loads(manifest_path.read_text())
            position = manifest["command_argv"].index("--skip-incomplete-curves") + 1
            manifest["command_argv"][position] = "1"
            self._write_json(manifest_path, manifest)
        mutators.append(option)

        def path_escape(root):
            manifest_path = root / "worker-0/manifest.json"
            manifest = json.loads(manifest_path.read_text())
            position = manifest["command_argv"].index("--checkpoint") + 1
            manifest["command_argv"][position] = "/tmp/worker-0/checkpoint.json"
            self._write_json(manifest_path, manifest)
        mutators.append(path_escape)

        def target(root):
            manifest_path = root / "worker-0/manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["prime"] = "103"
            self._write_json(manifest_path, manifest)
        mutators.append(target)

        for mutator in mutators:
            with self.subTest(mutator=mutator.__name__), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                root, profile, _ = self._fixture(directory)
                mutator(root)
                self._checksum(root)
                with self.assertRaises(search_audit.AuditError):
                    self._audit(root, profile)

    def test_status_truth_table_nonadvancing_and_heuristic_policy(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root, profile, _ = self._fixture(
                directory, end=102, worker_segments=[[["sea_level_limit"]]])
            result = self._audit(root, profile)
            self.assertEqual(result["outcome"]["completed_global_index_count"], 0)
            self.assertEqual(result["workers"][0]["diagnostic_record_count"], 1)

            progress_path = root / "worker-0/progress.jsonl"
            progress = [json.loads(line) for line in progress_path.read_text().splitlines()]
            progress[0]["outcome_class"] = "sound_rejection"
            progress[0]["sound_early_abort"] = True
            self._write_jsonl(progress_path, progress)
            log_path = root / "worker-0/worker.log"
            log = [json.loads(line) for line in log_path.read_text().splitlines()]
            log[1] = progress[0]
            self._write_jsonl(log_path, log)
            self._checksum(root)
            with self.assertRaisesRegex(search_audit.AuditError, "status flags"):
                self._audit(root, profile)

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            policy = self._policy(skip="1")
            root, profile, _ = self._fixture(
                directory, end=101, policy=policy,
                worker_segments=[[["heuristic_no_lift_skip"]]])
            result = self._audit(root, profile)
            self.assertEqual(result["outcome"]["counters"]["rejected_heuristic"], 1)

    def test_resume_attempts_resources_and_partial_timeout_cursor(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root, profile, _ = self._fixture(
                directory, end=103, wall_limit=10,
                worker_attempts=[[124, 0]],
                worker_segments=[[['sound_smoothness_reject'],
                                  ['sound_smoothness_reject', 'sound_smoothness_reject']]])
            result = self._audit(root, profile)
            self.assertTrue(result["accepted"])
            self.assertEqual(result["workers"][0]["worker_log"]["attempt_count"], 2)
            self.assertEqual(result["resources"]["attempt_exit_statuses"], [[124, 0]])

            value = json.loads(profile.read_text())
            value["allow_resume"] = False
            self._write_json(profile, value)
            with self.assertRaisesRegex(search_audit.AuditError, "resumed attempts"):
                self._audit(root, profile)

    def test_declared_outcome_threshold_boundaries_and_zero_work_attack(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root, profile, value = self._fixture(
                directory, end=101, wall_limit=10,
                worker_attempts=[[124]],
                worker_segments=[[['sound_smoothness_reject']]])
            baseline = self._audit(root, profile)
            self.assertTrue(baseline["accepted"])
            exact_throughput = baseline["resources"]["aggregate_curves_per_hour"]
            value["minimum_aggregate_curves_per_hour"] = exact_throughput
            self._write_json(profile, value)
            self.assertTrue(self._audit(root, profile)["accepted"])
            value["minimum_aggregate_curves_per_hour"] = exact_throughput + 0.001
            self._write_json(profile, value)
            failed = self._audit(root, profile)
            self.assertTrue(failed["structural_integrity"]["accepted"])
            self.assertFalse(failed["declared_outcome_gate"]["accepted"])

            exact_rss = baseline["resources"]["maximum_worker_rss_bytes"]
            value["minimum_aggregate_curves_per_hour"] = 0.0
            value["maximum_worker_rss_bytes"] = exact_rss
            self._write_json(profile, value)
            self.assertTrue(self._audit(root, profile)["accepted"])
            value["maximum_worker_rss_bytes"] = exact_rss - 1
            self._write_json(profile, value)
            rss_failed = self._audit(root, profile)
            self.assertTrue(rss_failed["structural_integrity"]["accepted"])
            self.assertFalse(
                rss_failed["declared_outcome_gate"]["checks"]["maximum_worker_rss_bytes"])

            value["maximum_worker_rss_bytes"] = exact_rss
            self._write_json(profile, value)
            resource_path = root / "worker-0/resource-usage.txt"
            resource_path.write_text(
                resource_path.read_text().replace("\tSwaps: 0", "\tSwaps: 1"))
            self._checksum(root)
            swaps_failed = self._audit(root, profile)
            self.assertTrue(swaps_failed["structural_integrity"]["accepted"])
            self.assertFalse(
                swaps_failed["declared_outcome_gate"]["checks"]["maximum_swaps_sum"])
            resource_path.write_text(
                resource_path.read_text().replace("\tSwaps: 1", "\tSwaps: 0"))
            self._checksum(root)

            # Restore the trusted one-curve declaration, then validly rehash a
            # zero-progress four-hour-style timeout bundle.
            value["minimum_aggregate_curves_per_hour"] = 0.0
            self._write_json(profile, value)
            (root / "worker-0/progress.jsonl").write_text("")
            log = [json.loads((root / "worker-0/worker.log").read_text().splitlines()[0])]
            self._write_jsonl(root / "worker-0/worker.log", log)
            checkpoint = json.loads((root / "worker-0/checkpoint.json").read_text())
            zeros = {name: 0 for name in COUNTERS}
            identity = {name: checkpoint[name] for name in (
                "prime", "seed", "worker_id", "worker_count", "range_start",
                "range_end", "schedule_sha256", "table_manifest_sha256", "build_id")}
            (root / "worker-0/checkpoint.json").write_text(
                self._checkpoint_text(identity, int(checkpoint["range_start"]), zeros))
            self._checksum(root)
            zero = self._audit(root, profile)
            self.assertTrue(zero["structural_integrity"]["accepted"])
            self.assertFalse(zero["accepted"])
            self.assertFalse(zero["declared_outcome_gate"]["checks"]["minimum_completed_count"])

    def test_first_curve_timeout_without_durable_state_is_authenticated(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root, profile, _ = self._fixture(
                directory, end=101, wall_limit=10,
                worker_attempts=[[124]], worker_segments=[[[]]])
            worker = root / "worker-0"
            (worker / "progress.jsonl").unlink()
            (worker / "checkpoint.json").unlink()
            self._checksum(root)

            result = self._audit(root, profile)
            self.assertTrue(result["accepted"])
            self.assertEqual(result["outcome"]["completed_intervals"], [])
            self.assertEqual(result["outcome"]["remaining_intervals"], [
                {"start": 100, "end": 101},
            ])
            self.assertIsNone(result["workers"][0]["progress_sha256"])
            self.assertIsNone(result["workers"][0]["checkpoint_sha256"])

            (worker / "progress.jsonl").write_text("", encoding="utf-8")
            self._checksum(root)
            with self.assertRaisesRegex(
                search_audit.AuditError, "unexpected file layout|only one of"
            ):
                self._audit(root, profile)

    def test_nonempty_smooth_cohort_histograms_are_balanced(self):
        path = ROOT / (
            "artifacts/runpod/"
            "p125-recovery-1001373-batch15x1024-550815e-20260803a/"
            "ohfo3hbov7ot8v/worker-0/worker.log")
        summary = json.loads(path.read_text().splitlines()[-1])
        batch = summary["smooth_batch"]
        parsed = search_audit._validate_smooth_batch(
            batch, "retained recovery", 1024)
        self.assertEqual(parsed["submitted_orders"], 7590)
        self.assertEqual(parsed["successful_cache_scan_chunks"], 8)

        forged = json.loads(json.dumps(batch))
        forged["cohorts"][0]["submitted_orders"] = "7589"
        with self.assertRaisesRegex(search_audit.AuditError, "total differs"):
            search_audit._validate_smooth_batch(forged, "forged recovery", 1024)

        forged = json.loads(json.dumps(batch))
        forged["cohorts"][0]["successful_scan_chunk_size_histogram"][1][
            "scan_chunks"] = "6"
        with self.assertRaisesRegex(search_audit.AuditError, "does not cover"):
            search_audit._validate_smooth_batch(forged, "forged recovery", 1024)

        forged = json.loads(json.dumps(batch))
        forged["submitted_orders"] = "1"
        forged["cohorts"][0]["submitted_orders"] = "1"
        with self.assertRaisesRegex(search_audit.AuditError, "order total differs"):
            search_audit._validate_smooth_batch(forged, "forged recovery", 1024)

        forged = json.loads(json.dumps(batch))
        forged["max_orders_per_successful_scan_chunk_in_any_cohort"] = "1"
        forged["cohorts"][0]["max_orders_per_successful_scan_chunk"] = "1"
        with self.assertRaisesRegex(search_audit.AuditError, "histogram maximum differs"):
            search_audit._validate_smooth_batch(forged, "forged recovery", 1024)

        forged = json.loads(json.dumps(batch))
        forged["coordinator_batches"] = "999"
        forged["cohorts"][0]["coordinator_batches"] = "999"
        with self.assertRaisesRegex(search_audit.AuditError, "too many batches"):
            search_audit._validate_smooth_batch(forged, "forged recovery", 1024)

        forged = json.loads(json.dumps(batch))
        forged["max_queued_requests_in_any_cohort"] = "999"
        forged["cohorts"][0]["max_queued_requests"] = "999"
        with self.assertRaisesRegex(search_audit.AuditError, "queue maximum"):
            search_audit._validate_smooth_batch(forged, "forged recovery", 1024)

        forged = json.loads(json.dumps(batch))
        forged["max_requests_per_batch_in_any_cohort"] = "999"
        forged["cohorts"][0]["max_requests_per_batch"] = "999"
        with self.assertRaisesRegex(search_audit.AuditError, "batch maximum"):
            search_audit._validate_smooth_batch(forged, "forged recovery", 1024)

        forged = json.loads(json.dumps(batch))
        forged["coordinator_batches"] = "0"
        forged["cohorts"][0]["coordinator_batches"] = "0"
        with self.assertRaisesRegex(search_audit.AuditError, "batch lower bounds"):
            search_audit._validate_smooth_batch(forged, "forged recovery", 1024)

        forged = json.loads(json.dumps(batch))
        forged["max_requests_per_batch_in_any_cohort"] = "0"
        forged["cohorts"][0]["max_requests_per_batch"] = "0"
        with self.assertRaisesRegex(search_audit.AuditError, "batch lower bounds"):
            search_audit._validate_smooth_batch(forged, "forged recovery", 1024)

        forged = json.loads(json.dumps(batch))
        forged["max_queued_requests_in_any_cohort"] = "0"
        forged["cohorts"][0]["max_queued_requests"] = "0"
        with self.assertRaisesRegex(search_audit.AuditError, "queue lower bound"):
            search_audit._validate_smooth_batch(forged, "forged recovery", 1024)

        impossible = json.loads(json.dumps(batch))
        impossible.update({
            "submitted_requests": "2", "completed_requests": "2",
            "coordinator_batches": "2", "submitted_orders": "7590",
            "max_queued_requests_in_any_cohort": "2",
            "max_requests_per_batch_in_any_cohort": "2",
        })
        impossible["cohorts"][0].update({
            "submitted_requests": "2", "completed_requests": "2",
            "coordinator_batches": "2", "submitted_orders": "7590",
            "max_queued_requests": "2", "max_requests_per_batch": "2",
        })
        with self.assertRaisesRegex(search_audit.AuditError, "batch upper bound"):
            search_audit._validate_smooth_batch(
                impossible, "impossible partition", 1024)

        impossible_first_batch = json.loads(json.dumps(batch))
        impossible_first_batch.update({
            "submitted_requests": "2", "completed_requests": "2",
            "submitted_orders": "7590",
            "max_queued_requests_in_any_cohort": "2",
            "max_requests_per_batch_in_any_cohort": "2",
        })
        impossible_first_batch["cohorts"][0].update({
            "submitted_requests": "2", "completed_requests": "2",
            "submitted_orders": "7590", "max_queued_requests": "2",
            "max_requests_per_batch": "2",
        })
        with self.assertRaisesRegex(search_audit.AuditError, "batch lower bounds"):
            search_audit._validate_smooth_batch(
                impossible_first_batch, "impossible first batch", 1024)

        failed_without_orders = json.loads(json.dumps(batch))
        failed_without_orders.update({
            "submitted_requests": "2", "failed_requests": "1",
            "coordinator_batches": "2", "max_queued_requests_in_any_cohort": "2",
        })
        failed_without_orders["cohorts"][0].update({
            "submitted_requests": "2", "failed_requests": "1",
            "coordinator_batches": "2", "max_queued_requests": "2",
        })
        with self.assertRaisesRegex(search_audit.AuditError, "order/request lower bounds"):
            search_audit._validate_smooth_batch(
                failed_without_orders, "failed request without orders", 1024)

        failed_with_one_order = json.loads(json.dumps(failed_without_orders))
        failed_with_one_order["submitted_orders"] = "7591"
        failed_with_one_order["cohorts"][0]["submitted_orders"] = "7591"
        with self.assertRaisesRegex(search_audit.AuditError, "order/request lower bounds"):
            search_audit._validate_smooth_batch(
                failed_with_one_order, "failed request with one order", 1024)

        oversized_chunk = json.loads(json.dumps(batch))
        oversized_chunk["successful_scan_chunk_size_histogram"] = [
            {"orders": "7590", "scan_chunks": "1"}]
        oversized_chunk["successful_cache_scan_chunks"] = "1"
        oversized_chunk["max_orders_per_successful_scan_chunk_in_any_cohort"] = "7590"
        oversized_chunk["cohorts"][0]["successful_scan_chunk_size_histogram"] = [
            {"orders": "7590", "scan_chunks": "1"}]
        oversized_chunk["cohorts"][0]["successful_cache_scan_chunks"] = "1"
        oversized_chunk["cohorts"][0]["max_orders_per_successful_scan_chunk"] = "7590"
        with self.assertRaisesRegex(search_audit.AuditError, "configured smooth batch cap"):
            search_audit._validate_smooth_batch(
                oversized_chunk, "oversized scan chunk", 1024)

        completed_without_scan = json.loads(json.dumps(batch))
        completed_without_scan.update({
            "successful_cache_scan_chunks": "0", "submitted_orders": "0",
            "max_orders_per_successful_scan_chunk_in_any_cohort": "0",
            "successful_scan_chunk_size_histogram": [],
        })
        completed_without_scan["cohorts"][0].update({
            "successful_cache_scan_chunks": "0", "submitted_orders": "0",
            "max_orders_per_successful_scan_chunk": "0",
            "successful_scan_chunk_size_histogram": [],
        })
        with self.assertRaisesRegex(search_audit.AuditError, "order/request lower bounds"):
            search_audit._validate_smooth_batch(
                completed_without_scan, "completed request without scan", 1024)

        too_few_scan_chunks = json.loads(json.dumps(batch))
        too_few_scan_chunks.update({
            "submitted_requests": "3", "completed_requests": "3",
            "coordinator_batches": "2", "successful_cache_scan_chunks": "1",
            "submitted_orders": "6", "max_queued_requests_in_any_cohort": "3",
            "max_requests_per_batch_in_any_cohort": "2",
            "max_orders_per_successful_scan_chunk_in_any_cohort": "6",
            "successful_scan_chunk_size_histogram": [
                {"orders": "6", "scan_chunks": "1"}],
        })
        too_few_scan_chunks["cohorts"][0].update({
            "submitted_requests": "3", "completed_requests": "3",
            "coordinator_batches": "2", "successful_cache_scan_chunks": "1",
            "submitted_orders": "6", "max_queued_requests": "3",
            "max_requests_per_batch": "2",
            "max_orders_per_successful_scan_chunk": "6",
            "successful_scan_chunk_size_histogram": [
                {"orders": "6", "scan_chunks": "1"}],
        })
        with self.assertRaisesRegex(search_audit.AuditError, "too few successful"):
            search_audit._validate_smooth_batch(
                too_few_scan_chunks, "too few successful chunks", 1024)

    def test_certificate_requires_local_pinned_verifier_and_rejects_invalid(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root, profile, _ = self._fixture(
                directory, end=102, worker_segments=[[["verified_certificate"]]])
            with self.assertRaisesRegex(search_audit.AuditError, "canonical-verifier"):
                self._audit(root, profile)
            result = self._audit(root, profile, VERIFIER)
            transcript = result["outcome"]["certificates"][0]["local_canonical_verifier"]
            self.assertTrue(transcript["accepted"])
            self.assertEqual(transcript["verifier_sha256"], search_audit.PINNED_VERIFIER_SHA256)

            bad = "101 35 25 27"
            progress_path = root / "worker-0/progress.jsonl"
            progress = [json.loads(line) for line in progress_path.read_text().splitlines()]
            progress[0]["certificate"]["line"] = bad
            self._write_jsonl(progress_path, progress)
            log = [json.loads(line) for line in (root / "worker-0/worker.log").read_text().splitlines()]
            log[1] = progress[0]
            self._write_jsonl(root / "worker-0/worker.log", log)
            (root / "worker-0/certificate.txt").write_text(bad + "\n")
            metadata_path = root / "worker-0/certificate.txt.meta.json"
            metadata = json.loads(metadata_path.read_text())
            metadata["certificate_line"] = bad
            metadata["certificate_sha256"] = hashlib.sha256((bad + "\n").encode()).hexdigest()
            self._write_json(metadata_path, metadata)
            self._checksum(root)
            with self.assertRaisesRegex(search_audit.AuditError, "canonical verifier rejected"):
                self._audit(root, profile, VERIFIER)

    def test_nonfinite_complete_telemetry_and_resource_wall_bound(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root, profile, _ = self._fixture(directory, wall_limit=10)
            progress_path = root / "worker-0/progress.jsonl"
            progress = [json.loads(line) for line in progress_path.read_text().splitlines()]
            progress[0]["timings_us"]["total"] = "Infinity"
            self._write_jsonl(progress_path, progress)
            log_path = root / "worker-0/worker.log"
            log = [json.loads(line) for line in log_path.read_text().splitlines()]
            log[1] = progress[0]
            self._write_jsonl(log_path, log)
            self._checksum(root)
            with self.assertRaises(search_audit.AuditError):
                self._audit(root, profile)

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root, profile, _ = self._fixture(directory, wall_limit=10)
            path = root / "worker-0/worker.log"
            lines = path.read_text().splitlines()
            first = json.loads(lines[0])
            first["resources"]["curve_threads"] = float("inf")
            lines[0] = json.dumps(first, separators=(",", ":"))
            path.write_text("\n".join(lines) + "\n")
            self._checksum(root)
            with self.assertRaisesRegex(search_audit.AuditError, "non-finite JSON"):
                self._audit(root, profile)

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root, profile, _ = self._fixture(directory, wall_limit=10)
            attempts_path = root / "worker-0/attempts.jsonl"
            attempts = [json.loads(line) for line in attempts_path.read_text().splitlines()]
            attempts[1]["epoch"] = attempts[0]["epoch"] + 20
            attempts[1]["utc"] = self._utc(attempts[1]["epoch"])
            self._write_jsonl(attempts_path, attempts)
            resource = (root / "worker-0/resource-usage.txt").read_text()
            resource = resource.replace("0:05.00", "0:20.00")
            (root / "worker-0/resource-usage.txt").write_text(resource)
            self._checksum(root)
            with self.assertRaisesRegex(search_audit.AuditError, "exceeded its wall"):
                self._audit(root, profile)

    def test_nonregular_symlink_worker_alias_and_partition_gap(self):
        attacks = (
            "fifo", "symlink", "alias", "missing-worker", "worker-file",
            "checksum-alias",
        )
        for attack in attacks:
            with self.subTest(attack=attack), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                root, profile, _ = self._fixture(directory, worker_count=2)
                if attack == "fifo":
                    os.mkfifo(root / "rogue.pipe")
                elif attack == "symlink":
                    (root / "rogue-link").symlink_to(root / "worker-0/manifest.json")
                elif attack == "alias":
                    shutil.copytree(root / "worker-0", root / "worker-00")
                    self._checksum(root)
                elif attack == "missing-worker":
                    shutil.rmtree(root / "worker-1")
                    self._checksum(root)
                elif attack == "worker-file":
                    shutil.rmtree(root / "worker-0")
                    (root / "worker-0").write_text("not a directory\n")
                    self._checksum(root)
                else:
                    checksums = root / "SHA256SUMS"
                    checksums.write_text(
                        checksums.read_text().replace(
                            "  ./worker-0/", "  .//worker-0/", 1))
                with self.assertRaises(search_audit.AuditError):
                    self._audit(root, profile)

    def test_fetch_and_retained_result_semantics_are_not_ignored(self):
        actual = ROOT / "artifacts/runpod/p125-runpod-cpu16-probe-20260802b/ohfo3hbov7ot8v"
        for attack in ("fetch", "result"):
            with self.subTest(attack=attack), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                root = directory / actual.name
                shutil.copytree(actual, root)
                profile = self._actual_profile(directory / "profile-dir", root)
                if attack == "fetch":
                    path = root / "fetch-metadata.json"
                    value = json.loads(path.read_text())
                    value["run_id"] = "forged-run"
                else:
                    path = root / "result.json"
                    value = json.loads(path.read_text())
                    value["outcome"]["curves_attempted"] = 999999
                    value["outcome"]["certificates_found"] = 77
                self._write_json(path, value)
                self._checksum(root)
                with self.assertRaises(search_audit.AuditError):
                    self._audit(root, profile)

    def test_checksum_crc_known_vector_and_canonical_checkpoint(self):
        self.assertEqual(
            search_audit._crc64_ecma(b"123456789"), 0x6C40DF5F0B497347)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root, profile, _ = self._fixture(directory)
            path = root / "worker-0/checkpoint.json"
            path.write_text(path.read_text().replace(
                '"curves_attempted":"4"', '"curves_attempted":"3"'))
            self._checksum(root)
            with self.assertRaisesRegex(search_audit.AuditError, "CRC64"):
                self._audit(root, profile)

    def test_python38_grammar_and_topology_legacy_adapter(self):
        source = (ROOT / "tools/audit_runpod_search.py").read_text()
        ast.parse(source, filename="audit_runpod_search.py", feature_version=(3, 8))
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root, _profile, _ = self._fixture(directory)
            legacy = search_audit.audit(
                root, expected_commit=COMMIT, expected_binary_sha256=BINARY,
                expected_cache_sha256=CACHE, expected_global_start=100,
                expected_global_end=104, expected_worker_count=1)
            self.assertTrue(legacy["accepted"])


class SearchCoverageAuditTests(unittest.TestCase):
    def _ledger(self):
        topology_path = Path(
            "artifacts/runpod/p125-topology-gate-550815e-20260803a/result.json")
        main_path = Path(
            "artifacts/runpod/p125-production-dual8-550815e-20260803a/audit-result.json")
        topology = json.loads((ROOT / topology_path).read_text())
        main = json.loads((ROOT / main_path).read_text())
        identity = {
            name: topology["immutable_identity"][name]
            for name in coverage_audit.IDENTITY_FIELDS
        }
        return {
            "schema": coverage_audit.LEDGER_SCHEMA,
            "identity": identity,
            "curve_index_identity": {
                "schema": coverage_audit.CURVE_IDENTITY_SCHEMA,
                "curve_family": "x1-27",
                "x1_require_point_four": True,
            },
            "contiguous_start": 1000827,
            "expected_first_gap": 1001373,
            "sources": [
                {
                    "label": "topology", "kind": "topology_audit",
                    "path": topology_path.as_posix(),
                    "sha256": hashlib.sha256((ROOT / topology_path).read_bytes()).hexdigest(),
                    "schedule_sha256": topology["immutable_identity"]["schedule_sha256"],
                    "bx_root": "artifacts/runpod/p125-topology-bx-550815e-20260803a/ohfo3hbov7ot8v",
                    "ax_root": "artifacts/runpod/p125-topology-ax-550815e-20260803a/ohfo3hbov7ot8v",
                    "ay_root": "artifacts/runpod/p125-topology-ay-550815e-20260803a/ohfo3hbov7ot8v",
                    "by_root": "artifacts/runpod/p125-topology-by-550815e-20260803a/ohfo3hbov7ot8v",
                    "build_provenance": "artifacts/runpod/p125-topology-gate-550815e-20260803a/build-provenance.json",
                    "binary": "artifacts/runpod/p125-runpod-cpu16-replay-550815e-20260802a/ohfo3hbov7ot8v/binaries/candidate.bin",
                    "source_repo": ".",
                },
                {
                    "label": "main", "kind": "runpod_search_audit",
                    "path": main_path.as_posix(),
                    "sha256": hashlib.sha256((ROOT / main_path).read_bytes()).hexdigest(),
                    "schedule_sha256": main["identity"]["schedule_sha256"],
                    "audit_root": "artifacts/runpod/p125-production-dual8-550815e-20260803a/ohfo3hbov7ot8v",
                    "audit_profile": "artifacts/runpod/p125-production-dual8-550815e-20260803a/audit-profile.json",
                },
            ],
            "intentional_overlaps": [{
                "sources": ["main", "topology"],
                "start": 1000827, "end": 1000955,
                "reason": "production epoch repeated the accepted topology ranges",
            }],
        }

    def _write(self, path, value):
        path.write_text(json.dumps(value, sort_keys=True) + "\n", encoding="utf-8")

    def test_retained_exact_identity_overlap_is_explicit_and_counted_once(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "ledger.json"
            self._write(path, self._ledger())
            result = coverage_audit.audit(path)
            self.assertTrue(result["accepted"])
            self.assertEqual(result["first_gap"], 1001373)
            self.assertEqual(result["total_assigned_count"], 674)
            self.assertEqual(result["unique_completed_count"], 546)
            self.assertEqual(result["duplicate_assignment_count"], 128)
            self.assertEqual(result["sources"][0]["fresh_count"], 128)
            self.assertEqual(result["sources"][1]["fresh_count"], 418)

    def test_declared_exact_recovery_schedule_can_share_coverage_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            value = self._ledger()
            recovery_path = Path(
                "artifacts/runpod/"
                "p125-recovery-1001373-batch7-550815e-20260803a/"
                "audit-result.json")
            recovery = json.loads((ROOT / recovery_path).read_text())
            value["sources"].append({
                "label": "recovery-timeout", "kind": "runpod_search_audit",
                "path": recovery_path.as_posix(),
                "sha256": hashlib.sha256(
                    (ROOT / recovery_path).read_bytes()).hexdigest(),
                "schedule_sha256": recovery["identity"]["schedule_sha256"],
                "audit_root": (
                    "artifacts/runpod/"
                    "p125-recovery-1001373-batch7-550815e-20260803a/"
                    "ohfo3hbov7ot8v"),
                "audit_profile": (
                    "artifacts/runpod/"
                    "p125-recovery-1001373-batch7-550815e-20260803a/"
                    "audit-profile.json"),
            })
            path = Path(temporary) / "ledger.json"
            self._write(path, value)
            result = coverage_audit.audit(path)
            self.assertTrue(result["accepted"])
            self.assertEqual(len(result["schedule_sha256s"]), 2)
            self.assertEqual(result["sources"][-1]["assigned_count"], 0)

    def test_forged_retained_run_result_is_recomputed_and_rejected(self):
        value = self._ledger()
        source = value["sources"][1]
        retained = json.loads((ROOT / source["path"]).read_text())
        retained["outcome"]["completed_intervals"][1]["end"] = 1001551
        with self.assertRaisesRegex(
            coverage_audit.AuditError, "differs from recomputation"
        ):
            coverage_audit._recompute_run_source(source, retained, "forged-main")

    def test_undeclared_overlap_identity_drift_and_hash_drift_fail(self):
        for attack in ("overlap", "identity", "hash", "schedule", "curve"):
            with self.subTest(attack=attack), tempfile.TemporaryDirectory() as temporary:
                value = self._ledger()
                if attack == "overlap":
                    value["intentional_overlaps"] = []
                elif attack == "identity":
                    value["identity"]["seed"] = str(int(value["identity"]["seed"]) + 1)
                elif attack == "curve":
                    value["curve_index_identity"]["curve_family"] = "weber-f"
                else:
                    if attack == "hash":
                        value["sources"][0]["sha256"] = "0" * 64
                    else:
                        value["sources"][0]["schedule_sha256"] = "0" * 64
                path = Path(temporary) / "ledger.json"
                self._write(path, value)
                with self.assertRaises(coverage_audit.AuditError):
                    coverage_audit.audit(path)

    def test_python38_grammar(self):
        source = (ROOT / "tools/audit_search_coverage.py").read_text()
        ast.parse(source, filename="audit_search_coverage.py", feature_version=(3, 8))


if __name__ == "__main__":
    unittest.main()
