#!/usr/bin/env python3
"""Execute a snapshotted deterministic Magma/native Schoof corpus."""

from __future__ import annotations

import argparse
import math
import os
from pathlib import Path
import sys
from typing import Any

import audit_common
from audit_common import (
    AuditError,
    MAGMA_ENVIRONMENT_KEYS,
    MAX_OUTPUT_CAP_BYTES,
    MAX_U64,
    canonical_decimal,
    canonical_json,
    deterministic_prime,
    digest,
    directory_path,
    executable_dependency_identity,
    exact_integer,
    executable_path,
    git_identity,
    host_identity,
    integer_list,
    loaded_module_code_digest,
    magma_dependency_identity,
    magma_count_curve,
    magma_runtime_identity,
    nonnegative_integer,
    positive_integer,
    probably_prime,
    run_json,
    snapshot_file,
    source_module_code_digest,
    utc_now,
    verify_file_identities,
    write_manifest,
)

ROOT = Path(
    os.environ.get(
        "ONESHOTSEA_AUDIT_REPOSITORY_ROOT", Path(__file__).resolve().parents[1]
    )
).resolve()
POINT_COUNT_SCRIPT = Path(__file__).with_name("point_count.m")
PRIME_CHECK_SCRIPT = Path(__file__).with_name("prime_check.m")
ORIGINAL_BOOTSTRAP = Path(
    os.environ.get("ONESHOTSEA_AUDIT_ORIGINAL_BOOTSTRAP", __file__)
).resolve()
ORIGINAL_DRIVER = Path(
    os.environ.get("ONESHOTSEA_AUDIT_ORIGINAL_DRIVER", __file__)
).resolve()
ORIGINAL_POINT_COUNT = Path(
    os.environ.get("ONESHOTSEA_AUDIT_ORIGINAL_POINT_COUNT", POINT_COUNT_SCRIPT)
).resolve()
ORIGINAL_PRIME_CHECK = Path(
    os.environ.get("ONESHOTSEA_AUDIT_ORIGINAL_PRIME_CHECK", PRIME_CHECK_SCRIPT)
).resolve()
ORIGINAL_COMMON = Path(
    os.environ.get("ONESHOTSEA_AUDIT_ORIGINAL_COMMON", audit_common.__file__)
).resolve()
SCHEMA = "oneshotsea.oracle-corpus.v1"
RECORD_SCHEMA = "oneshotsea.oracle-curve.v1"


def require_bootstrap_context() -> str:
    required = (
        "ONESHOTSEA_AUDIT_ORIGINAL_BOOTSTRAP",
        "ONESHOTSEA_AUDIT_ORIGINAL_DRIVER",
        "ONESHOTSEA_AUDIT_ORIGINAL_COMMON",
        "ONESHOTSEA_AUDIT_ORIGINAL_POINT_COUNT",
        "ONESHOTSEA_AUDIT_ORIGINAL_PRIME_CHECK",
        "ONESHOTSEA_AUDIT_EXECUTION_SNAPSHOT_DIR",
        "ONESHOTSEA_AUDIT_LOADED_BOOTSTRAP_CODE_SHA256",
    )
    missing = [name for name in required if not os.environ.get(name)]
    if missing:
        raise AuditError(
            "oracle corpus driver requires the pre-import bootstrap; missing "
            + ", ".join(missing)
        )
    snapshot_directory = Path(
        os.environ["ONESHOTSEA_AUDIT_EXECUTION_SNAPSHOT_DIR"]
    ).resolve()
    if Path(__file__).resolve().parent != snapshot_directory:
        raise AuditError("oracle corpus driver is not executing from its bootstrap snapshot")
    for filename in (
        "corpus_audit_driver.py",
        "audit_common.py",
        "point_count.m",
        "prime_check.m",
    ):
        if not (snapshot_directory / filename).is_file():
            raise AuditError(f"bootstrap execution snapshot is incomplete: {filename}")
    loaded_digest = os.environ["ONESHOTSEA_AUDIT_LOADED_BOOTSTRAP_CODE_SHA256"]
    if (
        len(loaded_digest) != 64
        or any(character not in "0123456789abcdef" for character in loaded_digest)
        or source_module_code_digest(
            ORIGINAL_BOOTSTRAP, "oneshotsea_reference_bootstrap_probe"
        )
        != loaded_digest
    ):
        raise AuditError("loaded oracle corpus bootstrap differs from its source bytes")
    return loaded_digest


def native_schoof_residue(
    native: Path,
    *,
    prime: int,
    a: int,
    b: int,
    ell: int,
    oracle_trace: int,
    global_ordinal: int,
    timeout_seconds: int,
    max_output_bytes: int,
) -> dict[str, int]:
    result = run_json(
        [
            str(native),
            "schoof-residue",
            "--p",
            str(prime),
            "--a",
            str(a),
            "--b",
            str(b),
            "--ell",
            str(ell),
        ],
        f"native Schoof residue ell={ell}",
        timeout_seconds=timeout_seconds,
        max_output_bytes=max_output_bytes,
    )
    expected_keys = {"p", "a", "b", "ell", "trace_residue"}
    if set(result) != expected_keys:
        raise AuditError(f"native Schoof residue ell={ell} returned an unexpected schema")
    result_p = canonical_decimal(result["p"], f"native residue ell={ell} p")
    result_a = canonical_decimal(result["a"], f"native residue ell={ell} a")
    result_b = canonical_decimal(result["b"], f"native residue ell={ell} b")
    result_ell = exact_integer(result["ell"], f"native residue ell={ell} level")
    observed = exact_integer(
        result["trace_residue"], f"native residue ell={ell} trace residue"
    )
    if (result_p, result_a, result_b, result_ell) != (prime, a, b, ell):
        raise AuditError(f"native Schoof residue ell={ell} returned mismatched inputs")
    if not 0 <= observed < ell:
        raise AuditError(f"native Schoof residue ell={ell} is noncanonical")
    expected = oracle_trace % ell
    if observed != expected:
        raise AuditError(
            f"Schoof residue mismatch at curve {global_ordinal}, ell={ell}: "
            f"observed {observed}, expected {expected}"
        )
    return {"ell": ell, "trace_residue": observed, "oracle_trace_residue": expected}


def curve_record(
    native: Path,
    magma: Path,
    magma_root: Path,
    point_count_script: Path,
    prime_check_script: Path,
    *,
    seed: int,
    bits: int,
    bucket_ordinal: int,
    global_ordinal: int,
    curve_index: int,
    complete_count_max_bits: int,
    max_ell: int,
    residue_levels: tuple[int, ...],
    timeout_seconds: int,
    max_output_bytes: int,
    max_prime_attempts: int,
) -> tuple[dict[str, Any], int | None]:
    if curve_index > MAX_U64:
        raise AuditError("the next curve index exceeds UINT64_MAX")
    prime, prime_attempts = deterministic_prime(
        magma,
        magma_root,
        prime_check_script,
        seed,
        bits,
        bucket_ordinal,
        domain=SCHEMA,
        timeout_seconds=timeout_seconds,
        max_output_bytes=max_output_bytes,
        max_attempts=max_prime_attempts,
    )
    singular_curves_skipped = 0
    while True:
        curve = run_json(
            [
                str(native),
                "curve",
                "--p",
                str(prime),
                "--seed",
                str(seed),
                "--index",
                str(curve_index),
            ],
            "native curve generator",
            timeout_seconds=timeout_seconds,
            max_output_bytes=max_output_bytes,
        )
        singular = curve.get("singular")
        expected_curve_keys = {"p", "seed", "index", "a", "b", "singular"}
        if singular is False:
            expected_curve_keys.add("j")
        if set(curve) != expected_curve_keys or type(singular) is not bool:
            raise AuditError("native curve generator returned an unexpected schema")
        curve_p = canonical_decimal(curve["p"], "native curve p")
        curve_seed = exact_integer(curve["seed"], "native curve seed")
        returned_index = exact_integer(curve["index"], "native curve index")
        a = canonical_decimal(curve["a"], "native curve a")
        b = canonical_decimal(curve["b"], "native curve b")
        if curve_p != prime or curve_seed != seed or returned_index != curve_index:
            raise AuditError("native curve generator returned mismatched inputs")
        if not (0 <= a < prime and 0 <= b < prime):
            raise AuditError("native curve generator returned noncanonical coefficients")
        discriminant_factor = (4 * pow(a, 3, prime) + 27 * pow(b, 2, prime)) % prime
        if (discriminant_factor == 0) != singular:
            raise AuditError("native curve generator returned inconsistent singular metadata")
        if singular is False:
            j = canonical_decimal(curve["j"], "native curve j")
            if not 0 <= j < prime:
                raise AuditError("native curve generator returned a noncanonical j-invariant")
            expected_j = (
                1728
                * 4
                * pow(a, 3, prime)
                * pow(discriminant_factor, -1, prime)
            ) % prime
            if j != expected_j:
                raise AuditError("native curve generator returned an inconsistent j-invariant")
            break
        singular_curves_skipped += 1
        if singular_curves_skipped > 64:
            raise AuditError("native curve generator returned 65 consecutive singular curves")
        if curve_index == MAX_U64:
            raise AuditError("singular-curve retry would exceed UINT64_MAX")
        curve_index += 1
    oracle = magma_count_curve(
        magma,
        magma_root,
        point_count_script,
        prime,
        a,
        b,
        timeout_seconds=timeout_seconds,
        max_output_bytes=max_output_bytes,
    )
    trace = oracle["trace"]
    residue_records: list[dict[str, int]] = []
    for ell in residue_levels:
        if ell == prime:
            continue
        residue_records.append(
            native_schoof_residue(
                native,
                prime=prime,
                a=a,
                b=b,
                ell=ell,
                oracle_trace=trace,
                global_ordinal=global_ordinal,
                timeout_seconds=timeout_seconds,
                max_output_bytes=max_output_bytes,
            )
        )
    complete_count: dict[str, Any] | None = None
    if bits <= complete_count_max_bits:
        result = run_json(
            [
                str(native),
                "schoof-count",
                "--p",
                str(prime),
                "--a",
                str(a),
                "--b",
                str(b),
                "--max-ell",
                str(max_ell),
            ],
            "native complete Schoof count",
            timeout_seconds=timeout_seconds,
            max_output_bytes=max_output_bytes,
        )
        expected_count_keys = {
            "p",
            "a",
            "b",
            "order",
            "trace",
            "residue_modulus",
            "levels",
        }
        if set(result) != expected_count_keys:
            raise AuditError("native complete Schoof count returned an unexpected schema")
        result_p = canonical_decimal(result["p"], "native complete count p")
        result_a = canonical_decimal(result["a"], "native complete count a")
        result_b = canonical_decimal(result["b"], "native complete count b")
        observed_order = canonical_decimal(result["order"], "native complete count order")
        observed_trace = canonical_decimal(
            result["trace"], "native complete count trace", signed=True
        )
        residue_modulus = canonical_decimal(
            result["residue_modulus"], "native complete count residue modulus"
        )
        levels_value = result["levels"]
        if type(levels_value) is not list or any(
            type(level) is not int for level in levels_value
        ):
            raise AuditError("native complete Schoof count returned invalid levels")
        levels = list(levels_value)
        if (result_p, result_a, result_b) != (prime, a, b):
            raise AuditError("native complete Schoof count returned mismatched inputs")
        admissible_levels = [
            level
            for level in range(3, max_ell + 1, 2)
            if probably_prime(level) and level != prime
        ]
        if (
            not levels
            or levels != admissible_levels[: len(levels)]
            or any(
                level < 3
                or level > max_ell
                or level % 2 == 0
                or not probably_prime(level)
                or level == prime
                for level in levels
            )
        ):
            raise AuditError("native complete Schoof count returned invalid levels")
        hasse_radius = math.isqrt(4 * prime)
        if math.prod(levels) != residue_modulus or residue_modulus <= 2 * hasse_radius:
            raise AuditError(
                "native complete Schoof count returned invalid completion metadata"
            )
        audited_levels = {record["ell"] for record in residue_records}
        for level in levels:
            if level in audited_levels:
                continue
            residue_records.append(
                native_schoof_residue(
                    native,
                    prime=prime,
                    a=a,
                    b=b,
                    ell=level,
                    oracle_trace=trace,
                    global_ordinal=global_ordinal,
                    timeout_seconds=timeout_seconds,
                    max_output_bytes=max_output_bytes,
                )
            )
            audited_levels.add(level)
        if observed_trace != prime + 1 - observed_order or abs(observed_trace) > hasse_radius:
            raise AuditError(
                "native complete Schoof count returned an inconsistent order and trace"
            )
        if observed_order != oracle["order"] or observed_trace != trace:
            raise AuditError(
                f"complete Schoof mismatch at curve {global_ordinal}: "
                f"order={observed_order}, trace={observed_trace}, oracle={oracle!r}"
            )
        complete_count = {
            "order": str(observed_order),
            "trace": str(observed_trace),
            "residue_modulus": str(residue_modulus),
            "levels": levels,
        }
    residue_records.sort(key=lambda record: record["ell"])
    record = {
        "schema": RECORD_SCHEMA,
        "ordinal": global_ordinal,
        "bucket_ordinal": bucket_ordinal,
        "requested_bits": bits,
        "prime": str(prime),
        "prime_generation_attempts": prime_attempts,
        "curve_seed": str(seed),
        "curve_index": str(curve_index),
        "singular_curves_skipped": singular_curves_skipped,
        "a": str(a),
        "b": str(b),
        "j": str(j),
        "oracle": {"order": str(oracle["order"]), "trace": str(trace)},
        "schoof_residues": residue_records,
        "complete_schoof_count": complete_count,
    }
    return record, None if curve_index == MAX_U64 else curve_index + 1


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Stream a deterministic Magma/native Schoof differential corpus"
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--native", default=str(ROOT / "build" / "oneshotsea"))
    parser.add_argument(
        "--magma-runtime",
        required=True,
        help="actual Magma executable to invoke directly",
    )
    parser.add_argument(
        "--magma-root",
        required=True,
        help="Magma installation root containing package/, libs/, and the passfile",
    )
    parser.add_argument("--seed", type=nonnegative_integer, required=True)
    parser.add_argument("--bit-sizes", default="16,32,64,128,256,416")
    parser.add_argument("--curves-per-size", type=positive_integer, default=1)
    parser.add_argument("--start-index", type=nonnegative_integer, default=0)
    parser.add_argument("--complete-count-max-bits", type=nonnegative_integer, default=32)
    parser.add_argument("--max-ell", type=positive_integer, default=19)
    parser.add_argument("--residue-levels", default="3,5,7")
    parser.add_argument("--command-timeout-seconds", type=positive_integer, default=3600)
    parser.add_argument("--max-output-bytes", type=positive_integer, default=1024 * 1024)
    parser.add_argument("--max-prime-attempts", type=positive_integer, default=1_000_000)
    args = parser.parse_args(argv)
    args.bit_sizes = integer_list(args.bit_sizes, minimum=4, label="bit-sizes")
    args.residue_levels = integer_list(
        args.residue_levels, minimum=3, label="residue-levels"
    )
    if args.seed > MAX_U64:
        parser.error("seed exceeds UINT64_MAX")
    if args.start_index > MAX_U64:
        parser.error("start-index exceeds UINT64_MAX")
    total_curves = len(args.bit_sizes) * args.curves_per_size
    if total_curves > MAX_U64 - args.start_index + 1:
        parser.error("the curve-index range exceeds UINT64_MAX")
    if args.max_ell > 37:
        parser.error("max-ell exceeds the native reference limit of 37")
    if args.max_output_bytes > MAX_OUTPUT_CAP_BYTES:
        parser.error("max-output-bytes may not exceed 67108864")
    if any(bits > 4096 for bits in args.bit_sizes):
        parser.error("bit-sizes may not exceed 4096")
    if any(
        level > 37 or level % 2 == 0 or not probably_prime(level)
        for level in args.residue_levels
    ):
        parser.error("residue-levels must be odd primes not exceeding 37")
    return args


def audit(args: argparse.Namespace, invocation: list[str]) -> dict[str, Any]:
    source = git_identity()
    output = args.output_dir.expanduser().resolve()
    output.mkdir(parents=True, exist_ok=False)
    manifest_path = output / "manifest.json"
    records_path = output / "records.ndjson"
    base: dict[str, Any] = {
        "schema": SCHEMA,
        "status": "running",
        "started_utc": utc_now(),
        "invocation_argv": invocation,
        "configuration": {
            "seed": str(args.seed),
            "bit_sizes": list(args.bit_sizes),
            "curves_per_size": args.curves_per_size,
            "start_index": str(args.start_index),
            "complete_count_max_bits": args.complete_count_max_bits,
            "max_ell": args.max_ell,
            "residue_levels": list(args.residue_levels),
            "command_timeout_seconds": args.command_timeout_seconds,
            "max_output_bytes": args.max_output_bytes,
            "max_prime_attempts": args.max_prime_attempts,
        },
    }
    write_manifest(manifest_path, base)
    tracked_inputs: dict[str, tuple[Path, str]] = {}
    native_dependencies_start: dict[str, Any] | None = None
    magma_dependencies_start: dict[str, Any] | None = None
    native_for_identity: Path | None = None
    magma_for_identity: Path | None = None
    magma_root_for_identity: Path | None = None
    try:
        native_source = executable_path(args.native, "native")
        magma = executable_path(args.magma_runtime, "Magma runtime")
        magma_root = directory_path(args.magma_root, "Magma root")
        magma_system_spec = magma_root / "package" / "spec"
        if not magma_system_spec.is_file():
            raise AuditError(f"Magma system spec is missing: {magma_system_spec}")
        driver_source = Path(__file__).resolve()
        common_source = Path(audit_common.__file__).resolve()
        source_inputs = {
            "native source": native_source,
            "Magma runtime executable": magma,
            "Magma system spec": magma_system_spec,
            "original corpus bootstrap": ORIGINAL_BOOTSTRAP,
            "original corpus driver": ORIGINAL_DRIVER,
            "original audit common": ORIGINAL_COMMON,
            "original point-count script": ORIGINAL_POINT_COUNT,
            "original prime-check script": ORIGINAL_PRIME_CHECK,
            "executing corpus driver": driver_source,
            "executing audit common": common_source,
            "executing point-count script": POINT_COUNT_SCRIPT,
            "executing prime-check script": PRIME_CHECK_SCRIPT,
        }
        tracked_inputs = {
            label: (path, digest(path)) for label, path in source_inputs.items()
        }
        if digest(ORIGINAL_DRIVER) != digest(driver_source):
            raise AuditError("executing corpus driver differs from its source snapshot")
        if digest(ORIGINAL_COMMON) != digest(common_source):
            raise AuditError("executing audit common differs from its source snapshot")
        if digest(ORIGINAL_POINT_COUNT) != digest(POINT_COUNT_SCRIPT):
            raise AuditError("executing point-count script differs from its source snapshot")
        if digest(ORIGINAL_PRIME_CHECK) != digest(PRIME_CHECK_SCRIPT):
            raise AuditError("executing prime-check script differs from its source snapshot")
        inputs_directory = output / "inputs"
        inputs_directory.mkdir()
        native = snapshot_file(native_source, inputs_directory / "oneshotsea")
        point_count_script = snapshot_file(
            POINT_COUNT_SCRIPT, inputs_directory / "point_count.m"
        )
        prime_check_script = snapshot_file(
            PRIME_CHECK_SCRIPT, inputs_directory / "prime_check.m"
        )
        bootstrap_snapshot = snapshot_file(
            ORIGINAL_BOOTSTRAP, inputs_directory / "corpus_audit.py"
        )
        driver_snapshot = snapshot_file(
            driver_source, inputs_directory / "corpus_audit_driver.py"
        )
        common_snapshot = snapshot_file(
            common_source, inputs_directory / "audit_common.py"
        )
        snapshot_inputs = {
            "native snapshot": native,
            "point-count script snapshot": point_count_script,
            "prime-check script snapshot": prime_check_script,
            "corpus bootstrap snapshot": bootstrap_snapshot,
            "corpus driver snapshot": driver_snapshot,
            "audit common snapshot": common_snapshot,
        }
        tracked_inputs.update(
            {
                label: (path, digest(path))
                for label, path in snapshot_inputs.items()
            }
        )
        native_for_identity = native
        magma_for_identity = magma
        magma_root_for_identity = magma_root
        native_dependencies_start = executable_dependency_identity(native)
        magma_dependencies_start = magma_dependency_identity(magma_root, magma)
        magma_version_identity = magma_runtime_identity(
            magma,
            magma_root,
            timeout_seconds=args.command_timeout_seconds,
            max_output_bytes=args.max_output_bytes,
        )
        base["identity"] = {
            "git_commit": source["commit"],
            "git_worktree_clean": source["worktree_clean"],
            "git_worktree_clean_at_start": source["worktree_clean"],
            "native_source_path": str(native_source),
            "native_path": str(native),
            "native_sha256": digest(native),
            "native_dynamic_dependencies": native_dependencies_start,
            "magma_root": str(magma_root),
            "magma_runtime_path": str(magma),
            "magma_runtime_sha256": digest(magma),
            "magma_system_spec_sha256": digest(magma_system_spec),
            "magma_runtime": magma_version_identity,
            "magma_dependencies": magma_dependencies_start,
            "magma_environment_policy": {
                "cleared": list(MAGMA_ENVIRONMENT_KEYS),
                "mode": "direct-runtime-controlled-root",
                "startup_file": os.devnull,
                "numeric_library_threads": "1",
            },
            "loaded_corpus_code_sha256": loaded_module_code_digest(
                sys.modules[__name__]
            ),
            "loaded_bootstrap_code_sha256": os.environ[
                "ONESHOTSEA_AUDIT_LOADED_BOOTSTRAP_CODE_SHA256"
            ],
            "loaded_audit_common_code_sha256": loaded_module_code_digest(
                audit_common
            ),
            "point_count_script_sha256": digest(point_count_script),
            "prime_check_script_sha256": digest(prime_check_script),
            "corpus_bootstrap_sha256": digest(bootstrap_snapshot),
            "corpus_driver_sha256": digest(driver_snapshot),
            "audit_common_original_sha256": digest(ORIGINAL_COMMON),
            "audit_common_executing_sha256": digest(common_source),
            "audit_common_artifact_sha256": digest(common_snapshot),
            "snapshots_directory": inputs_directory.name,
            "host": host_identity(),
        }
        write_manifest(manifest_path, base)
        count = 0
        curve_cursor: int | None = args.start_index
        with records_path.open("x", encoding="utf-8") as stream:
            for bits in args.bit_sizes:
                for bucket_ordinal in range(args.curves_per_size):
                    if curve_cursor is None:
                        raise AuditError(
                            "curve-index space exhausted before the requested corpus completed"
                        )
                    record, curve_cursor = curve_record(
                        native,
                        magma,
                        magma_root,
                        point_count_script,
                        prime_check_script,
                        seed=args.seed,
                        bits=bits,
                        bucket_ordinal=bucket_ordinal,
                        global_ordinal=count,
                        curve_index=curve_cursor,
                        complete_count_max_bits=args.complete_count_max_bits,
                        max_ell=args.max_ell,
                        residue_levels=args.residue_levels,
                        timeout_seconds=args.command_timeout_seconds,
                        max_output_bytes=args.max_output_bytes,
                        max_prime_attempts=args.max_prime_attempts,
                    )
                    stream.write(canonical_json(record) + "\n")
                    stream.flush()
                    os.fsync(stream.fileno())
                    count += 1
        verify_file_identities(tracked_inputs)
        if executable_dependency_identity(native) != native_dependencies_start:
            raise AuditError("native executable dependency identity changed during the corpus run")
        if magma_dependency_identity(magma_root, magma) != magma_dependencies_start:
            raise AuditError("Magma dependency identity changed during the corpus run")
        if magma_runtime_identity(
            magma,
            magma_root,
            timeout_seconds=args.command_timeout_seconds,
            max_output_bytes=args.max_output_bytes,
        ) != magma_version_identity:
            raise AuditError("Magma runtime identity changed during the corpus run")
        final_source = git_identity()
        if final_source["commit"] != source["commit"]:
            raise AuditError("Git commit changed during the corpus run")
        base["identity"]["git_worktree_clean_at_completion"] = final_source[
            "worktree_clean"
        ]
        base["identity"]["validated_at_completion"] = True
        base.update(
            {
                "status": "complete",
                "completed_utc": utc_now(),
                "records": {
                    "path": records_path.name,
                    "count": count,
                    "sha256": digest(records_path),
                    "next_curve_index": (
                        None if curve_cursor is None else str(curve_cursor)
                    ),
                },
            }
        )
        write_manifest(manifest_path, base)
        return base
    except BaseException as exc:
        base.update(
            {
                "status": "interrupted" if isinstance(exc, KeyboardInterrupt) else "failed",
                "completed_utc": utc_now(),
                "error": f"{type(exc).__name__}: {exc}",
            }
        )
        if records_path.exists():
            with records_path.open(encoding="utf-8") as partial_stream:
                partial_lines = sum(1 for _ in partial_stream)
            base["partial_records"] = {
                "path": records_path.name,
                "sha256": digest(records_path),
                "lines": partial_lines,
            }
        if tracked_inputs:
            try:
                verify_file_identities(tracked_inputs)
            except BaseException as identity_exc:
                base["identity_verification_error"] = (
                    f"{type(identity_exc).__name__}: {identity_exc}"
                )
        for label, start, probe in (
            (
                "native dependency",
                native_dependencies_start,
                lambda: executable_dependency_identity(native_for_identity),
            ),
            (
                "Magma dependency",
                magma_dependencies_start,
                lambda: magma_dependency_identity(
                    magma_root_for_identity, magma_for_identity
                ),
            ),
        ):
            if start is None:
                continue
            try:
                if probe() != start:
                    base[f"{label.replace(' ', '_')}_verification_error"] = (
                        f"{label} identity changed during the corpus run"
                    )
            except BaseException as identity_exc:
                base[f"{label.replace(' ', '_')}_verification_error"] = (
                    f"{type(identity_exc).__name__}: {identity_exc}"
                )
        write_manifest(manifest_path, base)
        raise


def main(argv: list[str] | None = None) -> int:
    actual_argv = sys.argv[1:] if argv is None else argv
    try:
        require_bootstrap_context()
        args = parse_args(actual_argv)
        result = audit(args, [str(ORIGINAL_BOOTSTRAP), *actual_argv])
    except (AuditError, RuntimeError, OSError, ValueError) as exc:
        print(f"oracle corpus: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("oracle corpus: interrupted", file=sys.stderr)
        return 130
    print(canonical_json(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
