#!/usr/bin/env python3
"""Tests for the independent small/medium Weber early-abort audit."""

from __future__ import annotations

import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "oracle"))
import weber_early_abort_audit as audit  # noqa: E402


P = 4294967291
MODULUS = 262147


def record(
    traces: list[int],
    true_trace: int,
    *,
    fallback: bool = False,
    second_pass_incomplete: bool = False,
) -> dict[str, object]:
    if fallback and second_pass_incomplete:
        raise ValueError("synthetic record cannot fail both heuristic passes")
    early = {
        "constraint_modulus": str(MODULUS),
        "effective_residue_classes": [
            str(value) for value in sorted(trace % MODULUS for trace in traces)
        ],
        "trace_count": str(len(traces)),
        "traces": [str(trace) for trace in traces],
        "levels": [{"ell": 5}],
        "fallback_levels": ([{"ell": 3}] if fallback else []),
    }
    native = {
        "schema": "oneshotsea.weber-audit.v1",
        "p": str(P),
        "seed": "1",
        "index": "0",
        "max_level": "193",
        "trace_cap": "16",
        "sea_threads": "1",
        "schoof_fallback": True,
        "smoothness_audited": False,
        "complete": True,
        "final_exact_only": True,
        "rejected_samples": "0",
        "weber_f": "2",
        "j": "3",
        "twist_parameter": "5",
        "curve": {"a": "1", "b": "2"},
        "twist": {"a": "3", "b": "4"},
        "trace_prior": None,
        "early": early,
        "unique_mode": "already_exact_singleton",
        "final": {"status": "complete", "trace_count": "1", "traces": [str(true_trace)]},
        "final_exact_trace": str(true_trace),
    }
    heuristic = copy.deepcopy(native)
    heuristic["schoof_fallback"] = False
    heuristic["complete"] = not (fallback or second_pass_incomplete)
    if fallback or second_pass_incomplete:
        heuristic["final_exact_only"] = False
        heuristic["final_exact_trace"] = None
        heuristic["unique_mode"] = "fresh_cap_one"
        heuristic["final"] = {"status": "level_limit", "trace_count": None, "traces": None}
    if fallback:
        heuristic["early"]["status"] = "level_limit"
        heuristic["early"]["fallback_levels"] = []
        heuristic["early"]["trace_count"] = None
        heuristic["early"]["traces"] = None
    return {
        "schema": audit.RECORD_SCHEMA,
        "ordinal": 0,
        "bucket_ordinal": 0,
        "prime_generation_attempts": 1,
        "requested_bits": 32,
        "native": native,
        "heuristic_fallback_off": heuristic,
        "oracle": {
            "curve": {"order": str(P + 1 - true_trace), "trace": str(true_trace)},
            "twist": {"order": str(P + 1 + true_trace), "trace": str(-true_trace)},
        },
    }


def artifact(root: Path, value: dict[str, object]) -> Path:
    corpus = root / "corpus"
    corpus.mkdir()
    encoded = audit.canonical_json(value) + "\n"
    records = corpus / "records.ndjson"
    records.write_text(encoded, encoding="utf-8")
    manifest = {
        "schema": audit.CORPUS_SCHEMA,
        "status": "complete",
        "configuration": {
            "bit_sizes": [32],
            "command_timeout_seconds": 60,
            "curves_per_size": 1,
            "max_generator_rejections": 4096,
            "max_level": 193,
            "max_output_bytes": 1048576,
            "max_prime_attempts": 100,
            "prime_generation_domain": "oneshotsea.weber-oracle-corpus.v1",
            "schoof_fallback": True,
            "sea_threads": 1,
            "seed": "1",
            "smoothness_audited": False,
            "start_index": "0",
            "trace_cap": 16,
        },
        "identity": {
            "git_commit": "a" * 40,
            "native_sha256": "b" * 64,
            "git_worktree_clean_at_start": True,
            "git_worktree_clean_at_completion": True,
            "validated_at_completion": True,
        },
        "records": {
            "path": records.name,
            "count": 1,
            "sha256": hashlib.sha256(encoded.encode()).hexdigest(),
            "next_curve_index": "1",
        },
    }
    (corpus / "manifest.json").write_text(
        audit.canonical_json(manifest) + "\n", encoding="utf-8"
    )
    return corpus


class WeberEarlyAbortAuditTests(unittest.TestCase):
    def test_loaded_module_source_digest_is_reproducible(self):
        source = audit.TOOL_SOURCE.read_bytes()
        loaded = audit.loaded_module_code_digest()
        self.assertEqual(audit.source_code_digest(source), loaded)
        self.assertEqual(
            {audit.source_code_digest(source) for _ in range(8)},
            {loaded},
        )

    def test_frozenset_constant_encoding_is_hash_seed_independent(self):
        script = (
            "import marshal, sys\n"
            f"sys.path.insert(0, {str(ROOT / 'oracle')!r})\n"
            "import weber_early_abort_audit as audit\n"
            "value = frozenset(('alpha', 'bravo', 'charlie', 'delta'))\n"
            "print(marshal.dumps(audit.stable_code_constant(value), 2).hex())\n"
        )
        encodings = set()
        for seed in range(1, 9):
            environment = os.environ.copy()
            environment["PYTHONHASHSEED"] = str(seed)
            result = subprocess.run(
                [sys.executable, "-B", "-c", script],
                check=True,
                capture_output=True,
                text=True,
                env=environment,
            )
            encodings.add(result.stdout.strip())
        self.assertEqual(len(encodings), 1)

    def test_true_trace_may_occupy_least_favorable_last_position(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            corpus = artifact(Path(temporary), record([-131055, 0, 100], 100))
            report = audit.audit_corpus(corpus)
        self.assertEqual(report["results"]["sound_false_negatives"], 0)
        self.assertEqual(report["results"]["order_evaluations"], 6)

    def test_corrupt_effective_residue_is_detected(self) -> None:
        value = record([-131055, 0, 100], 100)
        value["native"]["early"]["effective_residue_classes"][0] = "1"
        value["native"]["early"]["effective_residue_classes"].sort(key=int)
        with tempfile.TemporaryDirectory() as temporary:
            corpus = artifact(Path(temporary), value)
            with self.assertRaisesRegex(audit.AuditError, "do not reproduce"):
                audit.audit_corpus(corpus)

    def test_missing_true_trace_is_detected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            corpus = artifact(Path(temporary), record([-131055, 0, 100], 101))
            with self.assertRaisesRegex(audit.AuditError, "true trace is absent"):
                audit.audit_corpus(corpus)

    def test_exact_sound_rejection_has_no_smooth_opportunity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            corpus = artifact(Path(temporary), record([-131055], -131055))
            report = audit.audit_corpus(corpus)
        self.assertEqual(report["results"]["sound_rejections"], 1)
        self.assertEqual(report["results"]["smooth_opportunity_curves"], 0)
        self.assertEqual(report["results"]["sound_false_negatives"], 0)

    def test_first_pass_heuristic_false_negative_is_labelled_separately(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            corpus = artifact(
                Path(temporary), record([-131055, 0, 100], 100, fallback=True)
            )
            report = audit.audit_corpus(corpus)
        self.assertEqual(report["results"]["heuristic_rejections"], 1)
        self.assertEqual(report["results"]["heuristic_first_pass_rejections"], 1)
        self.assertEqual(report["results"]["heuristic_false_negatives"], 1)
        self.assertEqual(report["results"]["sound_false_negatives"], 0)

    def test_second_pass_heuristic_false_negative_is_labelled_separately(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            corpus = artifact(
                Path(temporary),
                record(
                    [-131055, 0, 100],
                    100,
                    second_pass_incomplete=True,
                ),
            )
            report = audit.audit_corpus(corpus)
        self.assertEqual(report["results"]["heuristic_rejections"], 1)
        self.assertEqual(report["results"]["heuristic_second_pass_rejections"], 1)
        self.assertEqual(report["results"]["heuristic_false_negatives"], 1)
        self.assertEqual(report["results"]["sound_false_negatives"], 0)

    def test_counterfactual_completion_label_must_match_final_evidence(self) -> None:
        value = record([-131055, 0, 100], 100)
        value["heuristic_fallback_off"]["complete"] = False
        with tempfile.TemporaryDirectory() as temporary:
            corpus = artifact(Path(temporary), value)
            with self.assertRaisesRegex(audit.AuditError, "claims completion"):
                audit.audit_corpus(corpus)

    def test_record_digest_drift_is_detected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            value = record([-131055], -131055)
            corpus = artifact(Path(temporary), value)
            value["prime_generation_attempts"] = 2
            (corpus / "records.ndjson").write_text(
                audit.canonical_json(value) + "\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(audit.AuditError, "digest disagrees"):
                audit.audit_corpus(corpus)

    def test_mid_audit_record_replacement_is_detected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            value = record([-131055], -131055)
            corpus = artifact(Path(temporary), value)
            records = corpus / "records.ndjson"
            original = audit.audit_record

            def replace(*args: object, **kwargs: object) -> dict[str, object]:
                changed = copy.deepcopy(value)
                changed["prime_generation_attempts"] = 2
                records.write_text(
                    audit.canonical_json(changed) + "\n", encoding="utf-8"
                )
                return original(*args, **kwargs)

            with mock.patch.object(audit, "audit_record", side_effect=replace):
                with self.assertRaisesRegex(audit.AuditError, "changed"):
                    audit.audit_corpus(corpus)

    def test_manifest_bucket_claim_is_bound_to_records(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            corpus = artifact(Path(temporary), record([-131055], -131055))
            path = corpus / "manifest.json"
            manifest = json.loads(path.read_text(encoding="utf-8"))
            manifest["configuration"]["bit_sizes"] = [16]
            path.write_text(audit.canonical_json(manifest) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(audit.AuditError, "bit bucket"):
                audit.audit_corpus(corpus)


if __name__ == "__main__":
    unittest.main()
