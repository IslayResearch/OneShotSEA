#!/usr/bin/env python3
"""Compute one deterministic contiguous worker range using arbitrary integers."""

from __future__ import annotations

import argparse
import json


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Partition [global_start, global_start + global_count) exactly."
    )
    p.add_argument("--global-start", type=int, required=True)
    p.add_argument("--global-count", type=int, required=True)
    p.add_argument("--worker-id", type=int, required=True, help="zero-based worker id")
    p.add_argument("--worker-count", type=int, required=True)
    p.add_argument("--seed-base", type=int, required=True)
    p.add_argument("--format", choices=("json", "shell"), default="json")
    return p


def main() -> int:
    args = parser().parse_args()
    if args.global_start < 0 or args.global_count <= 0:
        raise SystemExit("global-start must be nonnegative and global-count positive")
    if args.worker_count <= 0 or not 0 <= args.worker_id < args.worker_count:
        raise SystemExit("require 0 <= worker-id < worker-count")
    if args.seed_base < 0:
        raise SystemExit("seed-base must be nonnegative")

    width, remainder = divmod(args.global_count, args.worker_count)
    count = width + (1 if args.worker_id < remainder else 0)
    start = args.global_start + args.worker_id * width + min(args.worker_id, remainder)
    end = start + count
    result = {
        "worker_id": args.worker_id,
        "worker_count": args.worker_count,
        "range_start": str(start),
        "range_end": str(end),
        "range_count": str(count),
        "seed": str(args.seed_base + args.worker_id),
    }
    if args.format == "shell":
        for key, value in result.items():
            print(f"{key.upper()}={value}")
    else:
        print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
