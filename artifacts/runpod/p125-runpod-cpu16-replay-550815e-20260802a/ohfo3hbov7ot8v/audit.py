#!/usr/bin/env python3
"""Recompute the exact same-30 RunPod production-path promotion gate."""

import copy
import hashlib
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parent
BASELINE = (
    ROOT.parent.parent
    / "p125-runpod-cpu16-probe-20260802b"
    / "ohfo3hbov7ot8v"
)


def fail(message):
    raise SystemExit("error: " + message)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def audit_checksums(root):
    manifest = root / "SHA256SUMS"
    listed = set()
    for line in manifest.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  \./(.+)", line)
        if match is None:
            fail("malformed checksum line in " + str(manifest))
        wanted, relative = match.groups()
        if relative in listed:
            fail("duplicate checksum path: " + relative)
        listed.add(relative)
        path = root / relative
        if not path.is_file() or path.is_symlink():
            fail("checksum path is missing or not a regular file: " + str(path))
        if sha256(path) != wanted:
            fail("checksum mismatch: " + str(path))
    actual = {
        str(path.relative_to(root))
        for path in root.rglob("*")
        if path.is_file() and path.name != "SHA256SUMS"
    }
    if listed != actual:
        fail("checksum manifest does not cover the exact file set")


def json_lines(path):
    values = []
    for line_number, line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), 1
    ):
        try:
            value = json.loads(line)
        except json.JSONDecodeError as error:
            fail("invalid JSON at {}:{}: {}".format(path, line_number, error))
        if not isinstance(value, dict):
            fail("JSONL record is not an object at {}:{}".format(path, line_number))
        values.append(value)
    return values


def projection(record):
    value = copy.deepcopy(record)
    value.pop("peak_rss_bytes", None)
    value.pop("timings_us", None)
    state = value.get("state")
    if isinstance(state, dict):
        state.pop("build_id", None)
    return value


def projection_digest(values):
    encoded = json.dumps(
        values, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def time_record(path):
    text = path.read_text(encoding="utf-8")

    def last(pattern, label):
        matches = re.findall(pattern, text)
        if len(matches) != 1:
            fail("expected one {} record in {}".format(label, path))
        return matches[0]

    elapsed_text = last(
        r"Elapsed \(wall clock\) time \(h:mm:ss or m:ss\): ([^\n]+)",
        "elapsed-time",
    )
    elapsed = sum(
        float(value) * 60 ** position
        for position, value in enumerate(reversed(elapsed_text.split(":")))
    )
    return {
        "elapsed_seconds": elapsed,
        "average_cpu_percent": int(last(r"Percent of CPU this job got: (\d+)%", "CPU")),
        "maximum_resident_set_kbytes": int(
            last(r"Maximum resident set size \(kbytes\): (\d+)", "RSS")
        ),
        "swaps": int(last(r"Swaps: (\d+)", "swap")),
        "exit_status": int(last(r"Exit status: (\d+)", "exit-status")),
    }


def timing_totals(records):
    names = (
        "generation",
        "sea",
        "smoothness",
        "candidate",
        "assembly",
        "verifier",
        "total",
    )
    return {
        name: sum(int(record["timings_us"][name]) for record in records)
        for name in names
    }


def close(actual, wanted, label):
    if abs(actual - wanted) > 1e-12 * max(1.0, abs(actual), abs(wanted)):
        fail("{} mismatch: computed {}, retained {}".format(label, actual, wanted))


def main():
    audit_checksums(BASELINE)
    audit_checksums(ROOT)
    result = json.loads((ROOT / "result.json").read_text(encoding="utf-8"))
    if result.get("schema") != "oneshotsea.runpod-p125-replay.v1":
        fail("unexpected result schema")

    baseline = json_lines(BASELINE / "worker-0" / "progress.jsonl")
    candidate = json_lines(ROOT / "worker-0" / "progress.jsonl")
    if len(baseline) != 30 or len(candidate) != 30:
        fail("the replay must contain exactly 30 records per side")
    wanted_indices = [str(index) for index in range(1000000, 1000030)]
    if [row.get("index") for row in baseline] != wanted_indices:
        fail("baseline indices are not the exact contiguous replay range")
    if [row.get("index") for row in candidate] != wanted_indices:
        fail("candidate indices are not the exact contiguous replay range")

    baseline_projection = [projection(record) for record in baseline]
    candidate_projection = [projection(record) for record in candidate]
    if baseline_projection != candidate_projection:
        fail("candidate semantic records differ from the baseline")
    digest = projection_digest(baseline_projection)
    semantic = result["semantic_gate"]
    if semantic["baseline_projection_sha256"] != digest:
        fail("retained baseline projection digest is wrong")
    if semantic["candidate_projection_sha256"] != digest:
        fail("retained candidate projection digest is wrong")
    if not semantic["records_identical"]:
        fail("retained result contradicts identical projections")

    aggregate_fields = {
        "generator_rejections": "generator_rejections",
        "sea_levels": "sea_levels",
        "exact_sea_levels": "exact_sea_levels",
        "atkin_sea_levels": "atkin_sea_levels",
    }
    for retained, source in aggregate_fields.items():
        computed = sum(int(record[source]) for record in candidate)
        if semantic[retained] != computed:
            fail("retained aggregate is wrong: " + retained)

    baseline_time = time_record(BASELINE / "worker-0" / "resource-usage.txt")
    candidate_time = time_record(ROOT / "worker-0" / "resource-usage.txt")
    for name, computed in baseline_time.items():
        retained = result["baseline"][name]
        close(computed, retained, "baseline." + name)
    for name, computed in candidate_time.items():
        retained = result["candidate"][name]
        close(computed, retained, "candidate." + name)

    baseline_totals = timing_totals(baseline)
    candidate_totals = timing_totals(candidate)
    if result["timing_totals_us"]["baseline"] != baseline_totals:
        fail("retained baseline timing totals are wrong")
    if result["timing_totals_us"]["candidate"] != candidate_totals:
        fail("retained candidate timing totals are wrong")

    gate = result["promotion_gate"]
    wall_speedup = baseline_time["elapsed_seconds"] / candidate_time["elapsed_seconds"]
    total_speedup = baseline_totals["total"] / candidate_totals["total"]
    sea_speedup = baseline_totals["sea"] / candidate_totals["sea"]
    rss_ratio = (
        candidate_time["maximum_resident_set_kbytes"]
        / baseline_time["maximum_resident_set_kbytes"]
    )
    close(wall_speedup, gate["wall_speedup"], "wall speedup")
    close(total_speedup, gate["embedded_total_speedup"], "total speedup")
    close(sea_speedup, gate["embedded_sea_speedup"], "SEA speedup")
    close(rss_ratio, gate["rss_ratio"], "RSS ratio")
    accepted = (
        wall_speedup > gate["minimum_exclusive_wall_speedup"]
        and rss_ratio <= gate["maximum_inclusive_rss_ratio"]
        and baseline_time["swaps"] == candidate_time["swaps"] == 0
        and baseline_time["exit_status"] == candidate_time["exit_status"] == 0
    )
    if accepted is not True or gate["accepted"] is not True:
        fail("promotion gate did not pass")

    manifest = json.loads(
        (ROOT / "worker-0" / "manifest.json").read_text(encoding="utf-8")
    )
    if manifest.get("deployment_commit") != result["candidate"]["deployment_commit"]:
        fail("candidate deployment commit mismatch")
    if manifest.get("binary_sha256") != result["candidate"]["binary_sha256"]:
        fail("candidate binary digest mismatch")
    checkpoint = json.loads(
        (ROOT / "worker-0" / "checkpoint.json").read_text(encoding="utf-8")
    )
    if checkpoint.get("next_index") != "1000030":
        fail("candidate checkpoint did not exhaust the range")
    if int(checkpoint["counters"]["curves_attempted"]) != 30:
        fail("candidate checkpoint curve count is wrong")
    if (ROOT / "worker-0" / "certificate.txt").exists():
        fail("unexpected candidate certificate file")

    print(
        "p125 same-30 replay audit ok: semantic SHA-256 {}, wall {:.6f}x, RSS {:.6f}x".format(
            digest, wall_speedup, rss_ratio
        )
    )


if __name__ == "__main__":
    main()
