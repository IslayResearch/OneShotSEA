#!/usr/bin/env python3
"""Authenticate the retained compressed 10,000-curve Weber/Magma corpus."""

import gzip
import hashlib
import json
from pathlib import Path
from typing import Any, Dict


ROOT = Path(__file__).resolve().parent
RAW = ROOT / "raw"
EXPECTED_COMPRESSED_SHA256 = (
    "55459b1b7893adfa8ea0d8a3d7d0dd7c2fc5134195c71af340d38f045ec32471"
)


def fail(message: str) -> None:
    raise SystemExit("oracle-corpus audit failed: " + message)


def regular(path: Path) -> None:
    if not path.is_file() or path.is_symlink():
        fail("missing or non-regular file: " + str(path))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_json(path: Path) -> Dict[str, Any]:
    regular(path)
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        fail("invalid JSON {}: {}".format(path, error))
    if not isinstance(value, dict):
        fail("top-level JSON is not an object: " + str(path))
    return value


def main() -> None:
    result_path = ROOT / "result.json"
    manifest_path = RAW / "manifest.json"
    report_path = RAW / "early-abort-report.json"
    records_path = RAW / "records.ndjson.gz"
    result = load_json(result_path)
    manifest = load_json(manifest_path)
    report = load_json(report_path)
    regular(records_path)

    corpus = result.get("corpus")
    offline = result.get("offline_audit")
    if not isinstance(corpus, dict) or not isinstance(offline, dict):
        fail("result lacks corpus/offline-audit objects")
    if sha256_file(manifest_path) != corpus.get("manifest_sha256"):
        fail("manifest SHA-256 differs from result")
    if manifest_path.stat().st_size != corpus.get("manifest_bytes"):
        fail("manifest byte count differs from result")
    if sha256_file(report_path) != offline.get("sha256"):
        fail("early-abort report SHA-256 differs from result")
    if report_path.stat().st_size != offline.get("bytes"):
        fail("early-abort report byte count differs from result")
    if sha256_file(records_path) != EXPECTED_COMPRESSED_SHA256:
        fail("compressed record SHA-256 differs from retained identity")

    digest = hashlib.sha256()
    raw_bytes = 0
    record_count = 0
    try:
        with gzip.open(records_path, "rb") as handle:
            for line in handle:
                digest.update(line)
                raw_bytes += len(line)
                try:
                    record = json.loads(line)
                except (UnicodeDecodeError, json.JSONDecodeError) as error:
                    fail("invalid compressed record {}: {}".format(record_count, error))
                if not isinstance(record, dict):
                    fail("compressed record is not an object")
                if record.get("schema") != "oneshotsea.weber-oracle-curve.v2":
                    fail("compressed record schema drift")
                if record.get("ordinal") != record_count:
                    fail("compressed record ordinals are not contiguous")
                record_count += 1
    except (OSError, EOFError) as error:
        fail("cannot decompress retained records: " + str(error))

    records = manifest.get("records")
    if not isinstance(records, dict):
        fail("manifest lacks records object")
    if digest.hexdigest() != corpus.get("records_sha256"):
        fail("decompressed record SHA-256 differs from result")
    if digest.hexdigest() != records.get("sha256"):
        fail("decompressed record SHA-256 differs from manifest")
    if raw_bytes != corpus.get("records_bytes"):
        fail("decompressed byte count differs from result")
    if record_count != corpus.get("record_count") or record_count != records.get("count"):
        fail("decompressed record count differs from retained identities")

    report_corpus = report.get("corpus")
    report_results = report.get("results")
    if not isinstance(report_corpus, dict) or not isinstance(report_results, dict):
        fail("early-abort report lacks corpus/results objects")
    if report_corpus.get("records_sha256") != digest.hexdigest():
        fail("early-abort report is not bound to retained records")
    if report_corpus.get("manifest_sha256") != corpus.get("manifest_sha256"):
        fail("early-abort report is not bound to retained manifest")
    for name in (
        "order_evaluations",
        "smooth_opportunity_curves",
        "sound_rejections",
        "sound_rejections_before_unique_trace",
        "sound_saved_full_point_counts",
        "sound_false_negatives",
        "heuristic_rejections",
        "heuristic_false_negatives",
    ):
        if report_results.get(name) != offline.get(name):
            fail("offline result field {} differs".format(name))
    print(
        "oracle-corpus audit PASS: {} records, {} bytes, sha256={}".format(
            record_count, raw_bytes, digest.hexdigest()
        )
    )


if __name__ == "__main__":
    main()
