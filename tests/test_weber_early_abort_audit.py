#!/usr/bin/env python3
"""Tests for the independent small/medium Weber early-abort audit."""

from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest


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
) -> dict[str, object]:
    return {
        "schema": audit.RECORD_SCHEMA,
        "ordinal": 0,
        "requested_bits": 32,
        "native": {
            "p": str(P),
            "early": {
                "constraint_modulus": str(MODULUS),
                "effective_residue_classes": [
                    str(value) for value in sorted(trace % MODULUS for trace in traces)
                ],
                "trace_count": str(len(traces)),
                "traces": [str(trace) for trace in traces],
                "levels": [{"ell": 5}],
                "fallback_levels": ([{"ell": 3}] if fallback else []),
            },
        },
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
        "configuration": {"bit_sizes": [32]},
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

    def test_heuristic_false_negative_is_labelled_separately(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            corpus = artifact(
                Path(temporary), record([-131055, 0, 100], 100, fallback=True)
            )
            report = audit.audit_corpus(corpus)
        self.assertEqual(report["results"]["heuristic_rejections"], 1)
        self.assertEqual(report["results"]["heuristic_false_negatives"], 1)
        self.assertEqual(report["results"]["sound_false_negatives"], 0)

    def test_record_digest_drift_is_detected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            corpus = artifact(Path(temporary), record([-131055], -131055))
            with (corpus / "records.ndjson").open("a", encoding="utf-8") as stream:
                stream.write("\n")
            with self.assertRaisesRegex(audit.AuditError, "digest disagrees"):
                audit.audit_corpus(corpus)


if __name__ == "__main__":
    unittest.main()
