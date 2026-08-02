#!/usr/bin/env python3
"""Snapshot and execute the production-Weber/Magma corpus driver."""

from __future__ import annotations

import hashlib
import marshal
import os
from pathlib import Path
import shutil
import sys
import tempfile
import types


LOADED_MODULE_CODE = sys._getframe().f_code


HERE = Path(__file__).resolve().parent
REPOSITORY_ROOT = HERE.parent
DRIVER_SOURCE = HERE / "weber_corpus_audit_driver.py"
COMMON_SOURCE = HERE / "audit_common.py"
POINT_COUNT_SOURCE = HERE / "point_count.m"
PRIME_CHECK_SOURCE = HERE / "prime_check.m"


def stable_code_constant(value: object) -> object:
    if isinstance(value, types.CodeType):
        return ("code", stable_code_payload(value))
    if isinstance(value, slice):
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


def stable_code_payload(code: types.CodeType) -> tuple[object, ...]:
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


def loaded_bootstrap_code_digest() -> str:
    return hashlib.sha256(
        marshal.dumps(stable_code_payload(LOADED_MODULE_CODE), 2)
    ).hexdigest()


def main(argv: list[str] | None = None) -> int:
    actual_argv = sys.argv[1:] if argv is None else argv
    snapshot_directory = Path(
        tempfile.mkdtemp(prefix="oneshotsea-weber-oracle-audit-")
    ).resolve()
    try:
        driver = snapshot_directory / DRIVER_SOURCE.name
        shutil.copy2(DRIVER_SOURCE, driver)
        shutil.copy2(COMMON_SOURCE, snapshot_directory / COMMON_SOURCE.name)
        shutil.copy2(POINT_COUNT_SOURCE, snapshot_directory / POINT_COUNT_SOURCE.name)
        shutil.copy2(PRIME_CHECK_SOURCE, snapshot_directory / PRIME_CHECK_SOURCE.name)
        environment = os.environ.copy()
        environment.update(
            {
                "ONESHOTSEA_AUDIT_REPOSITORY_ROOT": str(REPOSITORY_ROOT),
                "ONESHOTSEA_WEBER_AUDIT_ORIGINAL_BOOTSTRAP": str(
                    Path(__file__).resolve()
                ),
                "ONESHOTSEA_WEBER_AUDIT_ORIGINAL_DRIVER": str(DRIVER_SOURCE),
                "ONESHOTSEA_WEBER_AUDIT_ORIGINAL_COMMON": str(COMMON_SOURCE),
                "ONESHOTSEA_WEBER_AUDIT_ORIGINAL_POINT_COUNT": str(
                    POINT_COUNT_SOURCE
                ),
                "ONESHOTSEA_WEBER_AUDIT_ORIGINAL_PRIME_CHECK": str(
                    PRIME_CHECK_SOURCE
                ),
                "ONESHOTSEA_WEBER_AUDIT_EXECUTION_SNAPSHOT_DIR": str(
                    snapshot_directory
                ),
                "ONESHOTSEA_WEBER_AUDIT_LOADED_BOOTSTRAP_CODE_SHA256": (
                    loaded_bootstrap_code_digest()
                ),
                "PYTHONHASHSEED": "0",
            }
        )
        os.execve(
            sys.executable,
            [sys.executable, str(driver), *actual_argv],
            environment,
        )
    except BaseException:
        shutil.rmtree(snapshot_directory, ignore_errors=True)
        raise
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
