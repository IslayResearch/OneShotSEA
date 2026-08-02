#!/usr/bin/env python3
"""Execute a snapshotted deterministic Magma/native Schoof corpus."""

from __future__ import annotations

import argparse
from contextlib import ExitStack
from datetime import datetime, timezone
import hashlib
import json
import marshal
import math
import os
from pathlib import Path
import platform
import re
import signal
import shutil
import subprocess
import sys
import tempfile
import time
import types
from typing import Any

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
SCHEMA = "oneshotsea.oracle-corpus.v1"
RECORD_SCHEMA = "oneshotsea.oracle-curve.v1"
MAX_U64 = (1 << 64) - 1
MAX_OUTPUT_CAP_BYTES = 64 * 1024 * 1024
MAGMA_ENVIRONMENT_KEYS = (
    "MAGMA_CMD",
    "MAGMAPASSFILE",
    "MAGMA_SYSTEM_SPEC",
    "MAGMA_SYSTEM_PACKAGE_ROOT",
    "MAGMA_LIBRARY_ROOT",
    "MAGMA_LIBRARIES",
    "MAGMA_HELP_DIR",
    "MAGMA_HTML_DIR",
    "MAGMA_STARTUP_FILE",
)
SMALL_PRIMES = (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47)
MILLER_RABIN_BASES = (
    2,
    3,
    5,
    7,
    11,
    13,
    17,
    19,
    23,
    29,
    31,
    37,
    41,
    43,
    47,
    53,
    59,
    61,
    67,
    71,
    73,
    79,
    83,
    89,
    97,
)


class AuditError(RuntimeError):
    """A fail-closed corpus or identity error."""


def utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def positive_integer(value: str) -> int:
    try:
        result = int(value, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"not a base-10 integer: {value!r}") from exc
    if result <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return result


def nonnegative_integer(value: str) -> int:
    try:
        result = int(value, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"not a base-10 integer: {value!r}") from exc
    if result < 0:
        raise argparse.ArgumentTypeError("value must be nonnegative")
    return result


def integer_list(value: str, *, minimum: int, label: str) -> tuple[int, ...]:
    try:
        result = tuple(int(item, 10) for item in value.split(","))
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"{label} must be comma-separated integers") from exc
    if not result or any(item < minimum for item in result) or len(set(result)) != len(result):
        raise argparse.ArgumentTypeError(
            f"{label} must contain distinct integers greater than or equal to {minimum}"
        )
    return result


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def loaded_module_code_digest(module: types.ModuleType) -> str:
    value = hashlib.sha256()
    functions = [
        (name, candidate)
        for name, candidate in vars(module).items()
        if isinstance(candidate, types.FunctionType)
        and candidate.__module__ == module.__name__
    ]
    for name, function in sorted(functions):
        value.update(name.encode("utf-8"))
        value.update(b"\0")
        value.update(marshal.dumps(function.__code__))
    return value.hexdigest()


def magma_environment(magma_root: Path, magma_runtime: Path) -> dict[str, str]:
    environment = os.environ.copy()
    for key in MAGMA_ENVIRONMENT_KEYS:
        environment.pop(key, None)
    passfile = magma_root / "magmapassfile"
    if not passfile.is_file() and passfile.with_suffix(".txt").is_file():
        passfile = passfile.with_suffix(".txt")
    environment.update(
        {
            "MAGMA_CMD": str(magma_runtime),
            "MAGMAPASSFILE": str(passfile),
            "MAGMA_SYSTEM_SPEC": str(magma_root / "package" / "spec"),
            "MAGMA_SYSTEM_PACKAGE_ROOT": str(magma_root / "package"),
            "MAGMA_LIBRARY_ROOT": str(magma_root / "libs"),
            "MAGMA_LIBRARIES": (
                "c9lattices:examples:galpols:intro:isolgps:matgps:"
                "pergps:simgps:solgps"
            ),
            "MAGMA_HELP_DIR": str(magma_root / "InternalHelp"),
            "MAGMA_HTML_DIR": str(magma_root / "doc" / "html"),
            "MAGMA_STARTUP_FILE": os.devnull,
            "MKL_SERIAL": "YES",
            "OMP_NUM_THREADS": "1",
            "OPENBLAS_NUM_THREADS": "1",
        }
    )
    return environment


def snapshot_file(source: Path, destination: Path) -> Path:
    shutil.copy2(source, destination)
    if digest(source) != digest(destination):
        raise AuditError(f"snapshot digest mismatch for {source}")
    return destination


def verify_file_identities(expected: dict[str, tuple[Path, str]]) -> None:
    for label, (path, expected_digest) in expected.items():
        try:
            observed_digest = digest(path)
        except OSError as exc:
            raise AuditError(f"{label} identity path became unreadable: {path}") from exc
        if observed_digest != expected_digest:
            raise AuditError(f"{label} identity changed during the corpus run")


def executable_path(value: str, label: str) -> Path:
    candidate = Path(value).expanduser()
    if candidate.parent == Path("."):
        resolved = shutil.which(value)
        if resolved:
            candidate = Path(resolved)
    candidate = candidate.resolve()
    if not candidate.is_file() or not os.access(candidate, os.X_OK):
        raise AuditError(f"{label} executable is missing or not executable: {candidate}")
    return candidate


def directory_path(value: str, label: str) -> Path:
    candidate = Path(value).expanduser().resolve()
    if not candidate.is_dir():
        raise AuditError(f"{label} directory is missing: {candidate}")
    return candidate


def kill_process_group(process: subprocess.Popen[Any]) -> None:
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def run_bounded(
    argv: list[str],
    label: str,
    *,
    timeout_seconds: int,
    max_output_bytes: int,
    environment: dict[str, str] | None = None,
    standard_input: bytes | None = None,
) -> subprocess.CompletedProcess[str]:
    with ExitStack() as stack:
        stdout_file = stack.enter_context(tempfile.TemporaryFile())
        stderr_file = stack.enter_context(tempfile.TemporaryFile())
        standard_input_file = None
        if standard_input is not None:
            standard_input_file = stack.enter_context(tempfile.TemporaryFile())
            standard_input_file.write(standard_input)
            standard_input_file.seek(0)
        process = subprocess.Popen(
            argv,
            cwd=ROOT,
            stdin=(
                subprocess.DEVNULL
                if standard_input_file is None
                else standard_input_file
            ),
            stdout=stdout_file,
            stderr=stderr_file,
            env=environment,
            start_new_session=True,
        )
        try:
            deadline = time.monotonic() + timeout_seconds
            failure = ""
            while process.poll() is None:
                stdout_size = os.fstat(stdout_file.fileno()).st_size
                stderr_size = os.fstat(stderr_file.fileno()).st_size
                if stdout_size > max_output_bytes or stderr_size > max_output_bytes:
                    failure = f"output exceeded {max_output_bytes} bytes"
                    break
                if time.monotonic() >= deadline:
                    failure = f"timed out after {timeout_seconds} seconds"
                    break
                time.sleep(0.02)
            if failure:
                kill_process_group(process)
            stdout_size = os.fstat(stdout_file.fileno()).st_size
            stderr_size = os.fstat(stderr_file.fileno()).st_size
            if not failure and (
                stdout_size > max_output_bytes or stderr_size > max_output_bytes
            ):
                failure = f"output exceeded {max_output_bytes} bytes"
            stdout_file.seek(0)
            stderr_file.seek(0)
            stdout = stdout_file.read(max_output_bytes + 1).decode(
                "utf-8", errors="replace"
            )
            stderr = stderr_file.read(max_output_bytes + 1).decode(
                "utf-8", errors="replace"
            )
            if failure:
                raise AuditError(f"{label} {failure}")
            return subprocess.CompletedProcess(argv, process.returncode, stdout, stderr)
        except BaseException:
            kill_process_group(process)
            raise


def run_json(
    argv: list[str],
    label: str,
    *,
    timeout_seconds: int,
    max_output_bytes: int,
    environment: dict[str, str] | None = None,
) -> dict[str, Any]:
    completed = run_bounded(
        argv,
        label,
        timeout_seconds=timeout_seconds,
        max_output_bytes=max_output_bytes,
        environment=environment,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip() or "no diagnostic"
        raise AuditError(f"{label} failed with status {completed.returncode}: {detail}")
    try:
        result = json.loads(completed.stdout, object_pairs_hook=unique_json_object)
    except json.JSONDecodeError as exc:
        raise AuditError(f"{label} returned invalid JSON") from exc
    if not isinstance(result, dict):
        raise AuditError(f"{label} did not return a JSON object")
    return result


def unique_json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise AuditError(f"JSON response contains duplicate key {key!r}")
        result[key] = value
    return result


def probably_prime(value: int) -> bool:
    if value < 2:
        return False
    for prime in SMALL_PRIMES:
        if value == prime:
            return True
        if value % prime == 0:
            return False
    exponent = value - 1
    shifts = 0
    while exponent % 2 == 0:
        exponent //= 2
        shifts += 1
    for base in MILLER_RABIN_BASES:
        if base >= value:
            continue
        residue = pow(base, exponent, value)
        if residue in (1, value - 1):
            continue
        for _ in range(shifts - 1):
            residue = residue * residue % value
            if residue == value - 1:
                break
        else:
            return False
    return True


def deterministic_prime(
    magma: Path,
    magma_root: Path,
    prime_check_script: Path,
    seed: int,
    bits: int,
    bucket_ordinal: int,
    *,
    timeout_seconds: int,
    max_output_bytes: int,
    max_attempts: int,
) -> tuple[int, int]:
    byte_count = (bits + 7) // 8
    attempt = 0
    while attempt < max_attempts:
        material = f"{SCHEMA}:{seed}:{bits}:{bucket_ordinal}:{attempt}".encode()
        block = hashlib.shake_256(material).digest(byte_count)
        candidate = int.from_bytes(block, "big")
        candidate &= (1 << bits) - 1
        candidate |= (1 << (bits - 1)) | 1
        if probably_prime(candidate):
            environment = magma_environment(magma_root, magma)
            environment["ONESHOT_SEA_ORACLE_P"] = str(candidate)
            result = run_json(
                [str(magma), "-b", str(prime_check_script)],
                "Magma prime validation",
                timeout_seconds=timeout_seconds,
                max_output_bytes=max_output_bytes,
                environment=environment,
            )
            if set(result) != {"p", "is_prime"}:
                raise AuditError("Magma prime validation returned an unexpected schema")
            if type(result["p"]) is not int or type(result["is_prime"]) is not bool:
                raise AuditError("Magma prime validation returned invalid field types")
            if result["p"] != candidate:
                raise AuditError("Magma prime validation returned a mismatched input")
            if result["is_prime"]:
                return candidate, attempt + 1
        attempt += 1
    raise AuditError(f"prime generation exhausted {max_attempts} candidates")


def magma_count_curve(
    magma: Path,
    magma_root: Path,
    point_count_script: Path,
    p: int,
    a: int,
    b: int,
    *,
    timeout_seconds: int,
    max_output_bytes: int,
) -> dict[str, int]:
    environment = magma_environment(magma_root, magma)
    environment.update(
        {
            "ONESHOT_SEA_ORACLE_P": str(p),
            "ONESHOT_SEA_ORACLE_A": str(a),
            "ONESHOT_SEA_ORACLE_B": str(b),
        }
    )
    result = run_json(
        [str(magma), "-b", str(point_count_script)],
        "Magma point count",
        timeout_seconds=timeout_seconds,
        max_output_bytes=max_output_bytes,
        environment=environment,
    )
    expected_keys = {"p", "a", "b", "order", "trace"}
    if set(result) != expected_keys or any(type(result[key]) is not int for key in expected_keys):
        raise AuditError("Magma point count returned an unexpected schema or field type")
    if result["p"] != p or result["a"] != a % p or result["b"] != b % p:
        raise AuditError("Magma point count returned mismatched inputs")
    if result["trace"] != p + 1 - result["order"]:
        raise AuditError("Magma point count returned an inconsistent order and trace")
    if result["order"] <= 0 or abs(result["trace"]) > math.isqrt(4 * p):
        raise AuditError("Magma point count violated the Hasse bound")
    return result


def magma_runtime_identity(
    magma: Path,
    magma_root: Path,
    *,
    timeout_seconds: int,
    max_output_bytes: int,
) -> dict[str, str]:
    environment = magma_environment(magma_root, magma)
    completed = run_bounded(
        [str(magma)],
        "Magma runtime identity",
        timeout_seconds=timeout_seconds,
        max_output_bytes=max_output_bytes,
        environment=environment,
        standard_input=b"quit;\n",
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip() or "no diagnostic"
        raise AuditError(
            f"Magma runtime identity failed with status {completed.returncode}: {detail}"
        )
    transcript = completed.stdout + completed.stderr
    match = re.search(r"\bMagma V[0-9][A-Za-z0-9_.-]*", transcript)
    if match is None:
        raise AuditError("Magma runtime identity did not report a version")
    return {"version": match.group(0)}


def canonical_json(value: object) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def canonical_decimal(value: object, label: str, *, signed: bool = False) -> int:
    if type(value) is not str or not value:
        raise AuditError(f"{label} is not a canonical decimal string")
    digits = value[1:] if signed and value.startswith("-") else value
    if not digits or not digits.isascii() or not digits.isdecimal():
        raise AuditError(f"{label} is not a canonical decimal string")
    if len(digits) > 1 and digits.startswith("0"):
        raise AuditError(f"{label} is not a canonical decimal string")
    if value.startswith("-") and digits == "0":
        raise AuditError(f"{label} is not a canonical decimal string")
    return int(value, 10)


def exact_integer(value: object, label: str) -> int:
    if type(value) is not int:
        raise AuditError(f"{label} is not a JSON integer")
    return value


def write_manifest(path: Path, value: dict[str, Any]) -> None:
    temporary = path.with_name(f"{path.name}.tmp.{os.getpid()}")
    temporary.write_text(canonical_json(value) + "\n", encoding="utf-8")
    temporary.replace(path)


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


def git_identity() -> dict[str, Any]:
    completed = run_bounded(
        ["git", "rev-parse", "HEAD"],
        "Git commit identity",
        timeout_seconds=30,
        max_output_bytes=1024 * 1024,
    )
    value = completed.stdout.strip()
    if (
        completed.returncode != 0
        or len(value) != 40
        or any(character not in "0123456789abcdef" for character in value)
    ):
        raise AuditError("unable to bind the corpus to a Git commit")
    status = run_bounded(
        ["git", "status", "--porcelain=v1", "--untracked-files=normal"],
        "Git worktree identity",
        timeout_seconds=30,
        max_output_bytes=1024 * 1024,
    )
    if status.returncode != 0:
        raise AuditError("unable to inspect Git worktree state")
    return {"commit": value, "worktree_clean": status.stdout == ""}


def host_identity() -> dict[str, Any]:
    return {
        "platform": platform.platform(),
        "machine": platform.machine(),
        "processor": platform.processor(),
        "logical_cpu_count": os.cpu_count(),
        "python_version": platform.python_version(),
        "python_executable": str(Path(sys.executable).resolve()),
        "python_sha256": digest(Path(sys.executable).resolve()),
    }


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
    try:
        native_source = executable_path(args.native, "native")
        magma = executable_path(args.magma_runtime, "Magma runtime")
        magma_root = directory_path(args.magma_root, "Magma root")
        magma_system_spec = magma_root / "package" / "spec"
        if not magma_system_spec.is_file():
            raise AuditError(f"Magma system spec is missing: {magma_system_spec}")
        driver_source = Path(__file__).resolve()
        source_inputs = {
            "native source": native_source,
            "Magma runtime executable": magma,
            "Magma system spec": magma_system_spec,
            "original corpus bootstrap": ORIGINAL_BOOTSTRAP,
            "original corpus driver": ORIGINAL_DRIVER,
            "original point-count script": ORIGINAL_POINT_COUNT,
            "original prime-check script": ORIGINAL_PRIME_CHECK,
            "executing corpus driver": driver_source,
            "executing point-count script": POINT_COUNT_SCRIPT,
            "executing prime-check script": PRIME_CHECK_SCRIPT,
        }
        tracked_inputs = {
            label: (path, digest(path)) for label, path in source_inputs.items()
        }
        if digest(ORIGINAL_DRIVER) != digest(driver_source):
            raise AuditError("executing corpus driver differs from its source snapshot")
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
        snapshot_inputs = {
            "native snapshot": native,
            "point-count script snapshot": point_count_script,
            "prime-check script snapshot": prime_check_script,
            "corpus bootstrap snapshot": bootstrap_snapshot,
            "corpus driver snapshot": driver_snapshot,
        }
        tracked_inputs.update(
            {
                label: (path, digest(path))
                for label, path in snapshot_inputs.items()
            }
        )
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
            "magma_root": str(magma_root),
            "magma_runtime_path": str(magma),
            "magma_runtime_sha256": digest(magma),
            "magma_system_spec_sha256": digest(magma_system_spec),
            "magma_runtime": magma_version_identity,
            "magma_environment_policy": {
                "cleared": list(MAGMA_ENVIRONMENT_KEYS),
                "mode": "direct-runtime-controlled-root",
                "startup_file": os.devnull,
                "numeric_library_threads": "1",
            },
            "loaded_corpus_code_sha256": loaded_module_code_digest(
                sys.modules[__name__]
            ),
            "point_count_script_sha256": digest(point_count_script),
            "prime_check_script_sha256": digest(prime_check_script),
            "corpus_bootstrap_sha256": digest(bootstrap_snapshot),
            "corpus_driver_sha256": digest(driver_snapshot),
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
        write_manifest(manifest_path, base)
        raise


def main(argv: list[str] | None = None) -> int:
    actual_argv = sys.argv[1:] if argv is None else argv
    try:
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
