#!/usr/bin/env python3
"""Run the independent Magma elliptic-curve point-counting oracle."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


SCRIPT = Path(__file__).with_name("point_count.m")


def integer(value: str) -> int:
    try:
        return int(value, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"not a base-10 integer: {value!r}") from exc


def resolve_magma(explicit: str | None) -> str:
    candidate = explicit or os.environ.get("MAGMA") or shutil.which("magma")
    if not candidate:
        raise RuntimeError(
            "Magma was not found; pass --magma PATH, set MAGMA, or add magma to PATH"
        )
    path = Path(candidate).expanduser()
    if path.parent != Path(".") and not path.exists():
        raise RuntimeError(f"Magma executable does not exist: {path}")
    return str(path)


def count_curve(magma: str, p: int, a: int, b: int) -> dict[str, int]:
    environment = os.environ.copy()
    environment.update(
        {
            "ONESHOT_SEA_ORACLE_P": str(p),
            "ONESHOT_SEA_ORACLE_A": str(a),
            "ONESHOT_SEA_ORACLE_B": str(b),
        }
    )
    completed = subprocess.run(
        [magma, "-b", str(SCRIPT)],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
        env=environment,
    )
    stdout = completed.stdout.strip()
    if completed.returncode != 0:
        detail = completed.stderr.strip() or stdout or "no diagnostic"
        raise RuntimeError(f"Magma failed with status {completed.returncode}: {detail}")
    try:
        result = json.loads(stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"Magma returned invalid JSON: {stdout!r}") from exc
    expected_keys = {"p", "a", "b", "order", "trace"}
    if set(result) != expected_keys or not all(
        isinstance(result[key], int) for key in expected_keys
    ):
        raise RuntimeError(f"Magma returned an unexpected result: {result!r}")
    if result["p"] != p or result["a"] != a % p or result["b"] != b % p:
        raise RuntimeError(f"Magma returned mismatched inputs: {result!r}")
    if result["trace"] != p + 1 - result["order"]:
        raise RuntimeError(f"Magma returned inconsistent order and trace: {result!r}")
    return result


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Count y^2 = x^3 + a*x + b over GF(p) using Magma"
    )
    parser.add_argument("p", type=integer, help="prime field characteristic (p > 3)")
    parser.add_argument("a", type=integer, help="short Weierstrass coefficient a")
    parser.add_argument("b", type=integer, help="short Weierstrass coefficient b")
    parser.add_argument(
        "--magma",
        metavar="PATH",
        help="Magma launcher (otherwise use $MAGMA or magma on PATH)",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        magma = resolve_magma(args.magma)
        result = count_curve(magma, args.p, args.a, args.b)
    except RuntimeError as exc:
        print(f"point-count oracle: {exc}", file=sys.stderr)
        return 1
    json.dump(result, sys.stdout, separators=(",", ":"))
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
