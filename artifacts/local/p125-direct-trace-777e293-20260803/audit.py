#!/usr/bin/env python3
import hashlib
import json
import math
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
PRIME = 10**125 + 237
TRACE = -534284869337319737295513917655253909609824180266230842767530862
SCHEDULE = [
    5, 23, 29, 31, 37, 41, 43, 53, 67, 71, 73, 101, 127, 137, 139,
    151, 157, 179, 197, 199, 211, 223, 229, 233, 239, 241, 251, 263,
    269, 271,
]
SOURCE_COMMIT = "777e293786ace30a3b8fec025d90875267f98ea4"
SOURCE_TREE = "b964a59a0744236574c00c94088e286be16be3a1"
VALIDATOR_SOURCE_SHA256 = (
    "140228f65a2bbc5e4d69397dc2380507d3e84855aece7d8f11eda137058bb8ae"
)
BINARY_SHA256 = (
    "7d952d9c2de84132dc089a2d6e93a8e2d026bf3b8aa9ae2abd952c9a83036f43"
)
GNU_TIME_SOURCE_SHA256 = (
    "fbacf0c81e62429df3e33bda4cee38756604f18e01d977338e23306a3e3b521e"
)
GNU_TIME_BINARY_SHA256 = (
    "166de2ff56f49a450204769205061bdc66fcb97ba13f8ea706112acfe03cdc67"
)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def candidate_count(modulus, residue):
    radius = math.isqrt(4 * PRIME)
    first_lift = -((radius + residue) // modulus)
    last_lift = (radius - residue) // modulus
    return max(0, last_lift - first_lift + 1)


def load_trace():
    raw = (ROOT / "raw/trace.ndjson").read_bytes()
    require(raw.endswith(b"\n"), "trace NDJSON lacks its final newline")
    rows = [json.loads(line) for line in raw.splitlines()]
    require(len(rows) == 31, "trace must contain 30 levels and one summary")
    return rows[:-1], rows[-1]


def parse_gnu_time():
    values = {}
    for line in (ROOT / "raw/trace.time").read_text().splitlines():
        key, value = line.split("=", 1)
        require(key not in values, f"duplicate GNU time field: {key}")
        values[key] = value
    required = {
        "command", "exit_status", "elapsed_seconds", "user_seconds",
        "system_seconds", "max_rss_kib", "average_rss_kib",
        "minor_page_faults", "major_page_faults",
        "voluntary_context_switches", "involuntary_context_switches",
        "filesystem_inputs", "filesystem_outputs",
    }
    require(set(values) == required, "GNU time field set differs")
    require(values["exit_status"] == "0", "validator process did not exit 0")
    return values


def validate_levels(levels, summary):
    exact_modulus = 432
    preparation_us = 0
    evaluation_us = 0
    maximum_payload = 0
    for index, (ell, row) in enumerate(zip(SCHEDULE, levels)):
        require(
            row["schema"] == "oneshotsea.p125-direct-trace-level.v1",
            f"level {ell} schema differs",
        )
        require(row["index"] == str(index), f"level {ell} index differs")
        require(row["ell"] == str(ell), f"level {ell} identity differs")
        require(row["exact"] is True, f"level {ell} is not exact")
        residue = str(TRACE % ell)
        require(row["trace_residue"] == residue, f"level {ell} residue differs")
        require(row["oracle_residue"] == residue, f"level {ell} oracle differs")
        require(row["oracle_accepted"] is True, f"level {ell} was rejected")
        require(row["atkin_projective_order"] is None, f"level {ell} is Atkin")
        require(row["atkin_residue_count"] == "0", f"level {ell} has Atkin residues")
        exact_modulus *= ell
        require(
            int(row["exact_modulus"]) == exact_modulus,
            f"level {ell} exact modulus differs",
        )
        require(
            int(row["exact_trace_candidate_count"])
            == candidate_count(exact_modulus, TRACE % exact_modulus),
            f"level {ell} Hasse candidate count differs",
        )
        preparation_us += int(row["preparation_us"])
        evaluation_us += int(row["evaluation_us"])
        maximum_payload = max(maximum_payload, int(row["matrix_payload_bytes"]))

    require(
        levels[-2]["exact_trace_candidate_count"] == "226",
        "level 269 must retain 226 exact Hasse candidates",
    )
    require(summary["schema"] == "oneshotsea.p125-direct-trace-summary.v1",
            "summary schema differs")
    require(summary["complete"] is True, "trace did not complete")
    require(summary["oracle_match"] is True, "final trace misses oracle")
    require(summary["retained_levels"] == "30", "retained level count differs")
    require(int(summary["exact_modulus"]) == exact_modulus,
            "summary exact modulus differs")
    require(summary["exact_trace_candidate_count"] == "1",
            "summary is not unique")
    require(int(summary["trace"]) == TRACE, "summary trace differs")
    return {
        "schema": "oneshotsea.p125-direct-trace-retained-result.v1",
        "status": "pass",
        "source_commit": SOURCE_COMMIT,
        "binary_sha256": BINARY_SHA256,
        "level_count": len(levels),
        "schedule": SCHEDULE,
        "all_levels_exact": True,
        "all_oracle_accepted": True,
        "trace": str(TRACE),
        "exact_modulus": str(exact_modulus),
        "candidates_after_level_269": 226,
        "final_exact_trace_candidate_count": 1,
        "summed_preparation_us": preparation_us,
        "summed_evaluation_us": evaluation_us,
        "maximum_matrix_payload_bytes": maximum_payload,
    }


def validate_identity():
    identity = json.loads((ROOT / "identity.json").read_text())
    require(identity["source"]["commit"] == SOURCE_COMMIT,
            "source commit identity differs")
    require(identity["source"]["tree"] == SOURCE_TREE,
            "source tree identity differs")
    require(identity["source"]["validator_source_sha256"]
            == VALIDATOR_SOURCE_SHA256, "validator source identity differs")
    require(identity["binary"]["sha256"] == BINARY_SHA256,
            "validator binary identity differs")
    require(identity["gnu_time"]["source_sha256"] == GNU_TIME_SOURCE_SHA256,
            "GNU time source identity differs")
    require(identity["gnu_time"]["binary_sha256"] == GNU_TIME_BINARY_SHA256,
            "GNU time binary identity differs")
    require((ROOT / "raw/source-commit.txt").read_text().strip()
            == SOURCE_COMMIT, "raw source commit differs")
    require((ROOT / "raw/source-tree.txt").read_text().strip()
            == SOURCE_TREE, "raw source tree differs")
    binary_digest = (ROOT / "raw/validator-binary.sha256").read_text().split()[0]
    require(binary_digest == BINARY_SHA256, "raw binary digest differs")
    require(
        (ROOT / "raw/gnu-time-version.txt").read_text().splitlines()[0]
        == "time (GNU Time) 1.9",
        "raw GNU time version differs",
    )


def validate_checksums():
    checksum_path = ROOT / "SHA256SUMS"
    entries = checksum_path.read_text().splitlines()
    require(entries, "checksum manifest is empty")
    covered = set()
    for line in entries:
        digest, encoded = line.split(maxsplit=1)
        relative = encoded.lstrip("* ")
        if relative.startswith("./"):
            relative = relative[2:]
        require(relative != "SHA256SUMS", "checksum manifest is self-referential")
        path = ROOT / relative
        require(path.is_file(), f"checksummed file is missing: {relative}")
        require(hashlib.sha256(path.read_bytes()).hexdigest() == digest,
                f"checksum differs: {relative}")
        covered.add(relative)
    actual = {
        str(path.relative_to(ROOT))
        for path in ROOT.rglob("*")
        if path.is_file() and path.name != "SHA256SUMS"
    }
    require(covered == actual, "checksum coverage differs from bundle files")


def main():
    levels, summary = load_trace()
    result = validate_levels(levels, summary)
    timing = parse_gnu_time()
    require((ROOT / "raw/build.stderr").read_bytes() == b"",
            "validator build stderr is not empty")
    require((ROOT / "raw/trace.stderr").read_bytes() == b"",
            "validator stderr is not empty")
    result["gnu_time"] = {
        key: timing[key]
        for key in (
            "elapsed_seconds", "user_seconds", "system_seconds", "max_rss_kib"
        )
    }
    if sys.argv[1:] == ["--emit-result"]:
        print(json.dumps(result, indent=2, sort_keys=True))
        return
    require(not sys.argv[1:], "usage: audit.py [--emit-result]")
    validate_identity()
    require(json.loads((ROOT / "result.json").read_text()) == result,
            "saved result differs from raw recomputation")
    validate_checksums()
    print("p125 direct trace retained evidence audit ok")


if __name__ == "__main__":
    main()
