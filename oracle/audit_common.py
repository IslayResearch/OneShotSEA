"""Shared fail-closed primitives for independent-oracle corpus audits."""

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
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import types
from typing import Any


REPOSITORY_ROOT = Path(
    os.environ.get(
        "ONESHOTSEA_AUDIT_REPOSITORY_ROOT", Path(__file__).resolve().parents[1]
    )
).resolve()
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


def stable_code_constant(value: Any) -> Any:
    if isinstance(value, types.CodeType):
        return ("code", stable_code_payload(value))
    if isinstance(value, slice):
        # CPython 3.14 may constant-fold subscription bounds into a slice,
        # which marshal format 2 cannot serialize directly.  Preserve its
        # complete typed structure rather than dropping the new constant.
        return (
            "slice",
            stable_code_constant(value.start),
            stable_code_constant(value.stop),
            stable_code_constant(value.step),
        )
    if isinstance(value, tuple):
        return ("tuple", tuple(stable_code_constant(item) for item in value))
    if isinstance(value, frozenset):
        return (
            "frozenset",
            frozenset(stable_code_constant(item) for item in value),
        )
    return value


def stable_code_payload(code: types.CodeType) -> tuple[Any, ...]:
    constants = tuple(stable_code_constant(value) for value in code.co_consts)
    line_table = (
        code.co_linetable if hasattr(code, "co_linetable") else code.co_lnotab
    )
    return (
        code.co_argcount,
        code.co_posonlyargcount,
        code.co_kwonlyargcount,
        code.co_nlocals,
        code.co_stacksize,
        code.co_flags,
        code.co_code,
        constants,
        code.co_names,
        code.co_varnames,
        code.co_filename,
        code.co_name,
        getattr(code, "co_qualname", code.co_name),
        code.co_firstlineno,
        line_table,
        getattr(code, "co_exceptiontable", b""),
        code.co_freevars,
        code.co_cellvars,
    )


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
        value.update(marshal.dumps(stable_code_payload(function.__code__), 2))
    return value.hexdigest()


def source_module_code_digest(path: Path, _module_name: str = "") -> str:
    """Digest the complete compiled module code without executing the source."""
    source = path.read_bytes()
    code = compile(source, str(path), "exec")
    return hashlib.sha256(marshal.dumps(stable_code_payload(code), 2)).hexdigest()


def directory_tree_identity(root: Path) -> dict[str, int | str]:
    """Return a content-addressed identity for a directory without following links."""
    root = root.resolve()
    if not root.is_dir():
        raise AuditError(f"identity tree is not a directory: {root}")
    value = hashlib.sha256()
    file_count = 0
    link_count = 0
    directory_count = 1
    total_bytes = 0
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        encoded = relative.encode("utf-8")
        value.update(len(encoded).to_bytes(8, "big"))
        value.update(encoded)
        if path.is_symlink():
            target = os.readlink(path).encode("utf-8")
            value.update(b"L")
            value.update(len(target).to_bytes(8, "big"))
            value.update(target)
            link_count += 1
        elif path.is_dir():
            value.update(b"D")
            directory_count += 1
        elif path.is_file():
            size = path.stat().st_size
            file_digest = bytes.fromhex(digest(path))
            value.update(b"F")
            value.update(size.to_bytes(16, "big"))
            value.update(file_digest)
            file_count += 1
            total_bytes += size
        else:
            raise AuditError(f"identity tree contains an unsupported entry: {path}")
    return {
        "sha256": value.hexdigest(),
        "files": file_count,
        "directories": directory_count,
        "symbolic_links": link_count,
        "bytes": total_bytes,
    }


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
            cwd=REPOSITORY_ROOT,
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


def executable_dependency_identity(executable: Path) -> dict[str, Any]:
    """Bind mutable, non-system direct dependencies of an executable or script."""
    executable = executable.resolve()
    dependencies: set[Path] = set()
    with executable.open("rb") as stream:
        first_line = stream.readline(4096)
    if first_line.startswith(b"#!"):
        words = shlex.split(first_line[2:].decode("utf-8", errors="strict").strip())
        if not words:
            raise AuditError(f"executable has an empty shebang: {executable}")
        interpreter = words[0]
        if Path(interpreter).name == "env":
            if len(words) < 2:
                raise AuditError(f"env shebang omits its interpreter: {executable}")
            resolved = shutil.which(words[1])
            if resolved is None:
                raise AuditError(f"cannot resolve shebang interpreter {words[1]!r}")
            dependencies.add(Path(resolved).resolve())
        else:
            dependencies.add(Path(interpreter).resolve())
    elif platform.system() == "Darwin":
        completed = run_bounded(
            ["/usr/bin/otool", "-L", str(executable)],
            "Mach-O dependency identity",
            timeout_seconds=30,
            max_output_bytes=4 * 1024 * 1024,
        )
        if completed.returncode != 0:
            raise AuditError(
                f"cannot inspect Mach-O dependencies for {executable}: "
                f"{completed.stderr.strip() or completed.stdout.strip()}"
            )
        for line in completed.stdout.splitlines()[1:]:
            name = line.strip().split(" (", 1)[0]
            if not name:
                continue
            if name.startswith("@executable_path/") or name.startswith("@loader_path/"):
                name = str(executable.parent / name.split("/", 1)[1])
            elif name.startswith("@"):
                raise AuditError(
                    f"unresolved dynamic-loader dependency {name!r} for {executable}"
                )
            if name.startswith("/System/Library/") or name.startswith("/usr/lib/"):
                continue
            dependencies.add(Path(name).resolve())
    elif platform.system() == "Linux":
        completed = run_bounded(
            ["ldd", str(executable)],
            "ELF dependency identity",
            timeout_seconds=30,
            max_output_bytes=4 * 1024 * 1024,
        )
        if completed.returncode != 0:
            raise AuditError(
                f"cannot inspect ELF dependencies for {executable}: "
                f"{completed.stderr.strip() or completed.stdout.strip()}"
            )
        for line in completed.stdout.splitlines():
            text = line.strip()
            if "=>" in text:
                name = text.split("=>", 1)[1].strip().split(" ", 1)[0]
            else:
                name = text.split(" ", 1)[0]
            if not name.startswith("/"):
                continue
            if name.startswith(("/lib/", "/lib64/", "/usr/lib/", "/usr/lib64/")):
                continue
            dependencies.add(Path(name).resolve())
    records: list[dict[str, Any]] = []
    for path in sorted(dependencies):
        if not path.is_file():
            raise AuditError(f"executable dependency is missing: {path}")
        records.append(
            {"path": str(path), "bytes": path.stat().st_size, "sha256": digest(path)}
        )
    payload = json.dumps(records, sort_keys=True, separators=(",", ":")).encode()
    return {"sha256": hashlib.sha256(payload).hexdigest(), "files": records}


def magma_dependency_identity(
    magma_root: Path, magma_runtime: Path
) -> dict[str, Any]:
    """Bind the mutable Magma package/library/runtime closure used by calls."""
    magma_root = magma_root.resolve()
    components: dict[str, Any] = {}
    for name, path in (
        ("package", magma_root / "package"),
        ("libraries", magma_root / "libs"),
        ("runtime_directory", magma_runtime.resolve().parent),
        ("help", magma_root / "InternalHelp"),
    ):
        if not path.is_dir():
            raise AuditError(f"Magma dependency directory is missing: {path}")
        components[name] = directory_tree_identity(path)
    passfile = magma_root / "magmapassfile"
    if not passfile.is_file() and passfile.with_suffix(".txt").is_file():
        passfile = passfile.with_suffix(".txt")
    if not passfile.is_file():
        raise AuditError(f"Magma passfile is missing: {passfile}")
    components["passfile"] = {
        "path": str(passfile.resolve()),
        "bytes": passfile.stat().st_size,
        "sha256": digest(passfile),
    }
    components["runtime_dynamic_dependencies"] = executable_dependency_identity(
        magma_runtime
    )
    payload = json.dumps(components, sort_keys=True, separators=(",", ":")).encode()
    return {"sha256": hashlib.sha256(payload).hexdigest(), "components": components}


def unique_json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise AuditError(f"JSON response contains duplicate key {key!r}")
        result[key] = value
    return result


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
    domain: str,
    timeout_seconds: int,
    max_output_bytes: int,
    max_attempts: int,
) -> tuple[int, int]:
    byte_count = (bits + 7) // 8
    attempt = 0
    while attempt < max_attempts:
        material = f"{domain}:{seed}:{bits}:{bucket_ordinal}:{attempt}".encode()
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
