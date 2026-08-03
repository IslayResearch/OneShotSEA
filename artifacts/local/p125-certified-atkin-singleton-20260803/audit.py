#!/usr/bin/env python3
"""Authenticate and rederive the p125 certified-singleton checkpoint."""

import hashlib
import json
import math
import re
import subprocess
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parent
REPOSITORY = ROOT.parents[2]
EXPECTED_FILES = {"README.md", "audit.py", "result.json"}


class AuditError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise AuditError(message)


def equal(actual, expected, label):
    require(actual == expected,
            f"{label}: expected {expected!r}, observed {actual!r}")


def authenticate():
    entries = {}
    manifest = (ROOT / "SHA256SUMS").read_text(encoding="utf-8").splitlines()
    for number, line in enumerate(manifest, 1):
        fields = line.split("  ", 1)
        equal(len(fields), 2, f"SHA256SUMS line {number} format")
        digest, relative = fields
        require(re.fullmatch(r"[0-9a-f]{64}", digest) is not None,
                f"SHA256SUMS line {number}: invalid digest")
        path = PurePosixPath(relative)
        require(not path.is_absolute() and ".." not in path.parts,
                f"SHA256SUMS line {number}: unsafe path")
        require(relative not in entries,
                f"SHA256SUMS line {number}: duplicate path")
        entries[relative] = digest
    equal(set(entries), EXPECTED_FILES, "manifest file set")
    actual = {
        path.relative_to(ROOT).as_posix()
        for path in ROOT.rglob("*")
        if path.is_file() and path.name != "SHA256SUMS" and
        "__pycache__" not in path.parts
    }
    equal(actual, EXPECTED_FILES, "artifact file set")
    for relative, digest in entries.items():
        observed = hashlib.sha256((ROOT / relative).read_bytes()).hexdigest()
        equal(observed, digest, f"SHA-256 {relative}")


def parse_pari_line(line):
    fields = {}
    for token in line.split():
        require("=" in token, f"malformed PARI token {token!r}")
        key, value = token.split("=", 1)
        require(key and key not in fields,
                f"empty or duplicate PARI key {key!r}")
        fields[key] = value
    return fields


def validate_source(result):
    implementation = result["implementation"]
    completed = subprocess.run(
        ["git", "rev-parse", f'{implementation["commit"]}^{{tree}}'],
        cwd=REPOSITORY, check=False, capture_output=True, text=True)
    require(completed.returncode == 0,
            f"cannot resolve implementation commit: {completed.stderr.strip()}")
    equal(completed.stdout.strip(), implementation["tree"],
          "implementation source tree")


def validate_inputs(result):
    target = result["target"]
    prime = int(target["prime"])
    equal(prime, 10**125 + 237, "target prime")
    equal(prime.bit_length(), target["bits"], "target bits")
    equal(target["index"], "2000004", "curve index")
    equal(target["trace_cap"], 1, "trace cap")
    equal(result["authenticated_inputs"]["direct_levels"],
          [7, 5, 11, 13, 19, 17, 23, 29, 31, 37, 41, 43, 47,
           53, 67, 71, 79, 61, 73, 59], "direct schedule")
    for label, value in result["authenticated_inputs"].items():
        if label.endswith("sha256"):
            require(re.fullmatch(r"[0-9a-f]{64}", value) is not None,
                    f"invalid {label}")
    return prime


def validate_runs(result, prime):
    baseline = result["baseline"]
    candidate = result["candidate"]
    equal(baseline["status"], "sea_level_limit", "baseline status")
    equal((baseline["full_point_count"], baseline["reached_smoothness"]),
          (False, False), "baseline completion flags")
    equal((baseline["ell_379"]["exact_trace_candidates"],
           baseline["ell_379"]["effective_trace_candidates"]),
          (221262, 1), "baseline ell-379 counts")
    equal(baseline["ell_379"]["trace_residue"], 71,
          "baseline ell-379 exact residue")
    equal(baseline["post_singleton_levels"], [383, 389, 397, 401],
          "baseline post-singleton levels")
    avoided_work = sum(baseline["post_singleton_level_work_us"])
    equal(avoided_work, result["comparison"]["baseline_post_singleton_work_us"],
          "post-singleton level work")
    equal((baseline["next_index"], baseline["complete"]),
          ("2000004", False), "baseline cursor")

    equal(candidate["status"], "sound_smoothness_reject", "candidate status")
    equal(candidate["outcome_class"], "sound_rejection",
          "candidate outcome class")
    equal((candidate["sound_early_abort"], candidate["full_point_count"],
           candidate["reached_smoothness"]),
          (True, True, True), "candidate completion flags")
    equal((candidate["final_exact_trace_candidates"],
           candidate["final_effective_trace_candidates"]),
          (221262, 1), "candidate final counts")
    equal(candidate["last_levels"][-1], 379, "candidate stopping level")
    equal(candidate["last_exact_residues"], {"373": 13, "379": 71},
          "candidate tail residues")
    trace = int(candidate["trace"])
    require(abs(trace) <= math.isqrt(4 * prime),
            "candidate trace lies outside the Hasse interval")
    equal(trace % 373, 13, "candidate trace modulo 373")
    equal(trace % 379, 71, "candidate trace modulo 379")
    equal(int(candidate["curve_order"]), prime + 1 - trace,
          "candidate curve order")
    equal(int(candidate["twist_order"]), prime + 1 + trace,
          "candidate twist order")
    equal((candidate["next_index"], candidate["complete"]),
          ("2000005", True), "candidate cursor")
    equal(baseline["sea_levels"] - candidate["sea_levels"],
          result["comparison"]["avoided_weber_levels"],
          "avoided Weber levels")
    equal(result["comparison"]["controlled_timing_speedup_claimed"], False,
          "controlled timing nonclaim")
    return trace


def validate_independent_point_count(result, prime, trace):
    evidence = result["independent_point_count"]
    path = REPOSITORY / evidence["path"]
    require(path.is_file(), f"missing independent point count {path}")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    equal(digest, evidence["sha256"], "independent point-count SHA-256")
    lines = path.read_text(encoding="utf-8").splitlines()
    require(1 <= evidence["line"] <= len(lines),
            "independent point-count line is out of range")
    fields = parse_pari_line(lines[evidence["line"] - 1])
    equal(fields["index"], evidence["index"], "PARI curve index")
    equal(fields["order"], evidence["order"], "PARI curve order text")
    equal(fields["trace"], evidence["trace"], "PARI trace text")
    equal(int(fields["trace"]), trace, "PARI/native trace")
    equal(int(fields["order"]), prime + 1 - trace, "PARI/native order")


def main():
    authenticate()
    result = json.loads((ROOT / "result.json").read_text(encoding="utf-8"))
    equal(result["schema"], "oneshotsea.p125-certified-atkin-singleton.v1",
          "result schema")
    validate_source(result)
    prime = validate_inputs(result)
    trace = validate_runs(result, prime)
    validate_independent_point_count(result, prime, trace)
    claims = result["claims"]
    equal(claims,
          {"fixed_curve_correctness_and_coverage": True,
           "certificate_found": False,
           "yield_measured": False,
           "cm_crossover_established": False,
           "asymptotic_exponent_changed": False},
          "claim scope")
    print(json.dumps({
        "schema": "oneshotsea.p125-certified-atkin-singleton-audit.v1",
        "ok": True,
        "implementation_commit": result["implementation"]["commit"],
        "trace": str(trace),
        "baseline_status": result["baseline"]["status"],
        "candidate_status": result["candidate"]["status"],
        "avoided_weber_levels": result["comparison"]["avoided_weber_levels"],
        "independent_system": result["independent_point_count"]["system"],
    }, sort_keys=True))


if __name__ == "__main__":
    main()
