#!/usr/bin/env python3
"""Snapshot and execute the deterministic Schoof corpus driver."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import sys
import tempfile


HERE = Path(__file__).resolve().parent
REPOSITORY_ROOT = HERE.parent
DRIVER_SOURCE = HERE / "corpus_audit_driver.py"
POINT_COUNT_SOURCE = HERE / "point_count.m"
PRIME_CHECK_SOURCE = HERE / "prime_check.m"


def main(argv: list[str] | None = None) -> int:
    actual_argv = sys.argv[1:] if argv is None else argv
    snapshot_directory = Path(
        tempfile.mkdtemp(prefix="oneshotsea-oracle-audit-")
    ).resolve()
    try:
        driver = snapshot_directory / DRIVER_SOURCE.name
        shutil.copy2(DRIVER_SOURCE, driver)
        shutil.copy2(POINT_COUNT_SOURCE, snapshot_directory / POINT_COUNT_SOURCE.name)
        shutil.copy2(PRIME_CHECK_SOURCE, snapshot_directory / PRIME_CHECK_SOURCE.name)
        environment = os.environ.copy()
        environment.update(
            {
                "ONESHOTSEA_AUDIT_REPOSITORY_ROOT": str(REPOSITORY_ROOT),
                "ONESHOTSEA_AUDIT_ORIGINAL_BOOTSTRAP": str(Path(__file__).resolve()),
                "ONESHOTSEA_AUDIT_ORIGINAL_DRIVER": str(DRIVER_SOURCE),
                "ONESHOTSEA_AUDIT_ORIGINAL_POINT_COUNT": str(POINT_COUNT_SOURCE),
                "ONESHOTSEA_AUDIT_ORIGINAL_PRIME_CHECK": str(PRIME_CHECK_SOURCE),
                "ONESHOTSEA_AUDIT_EXECUTION_SNAPSHOT_DIR": str(snapshot_directory),
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
