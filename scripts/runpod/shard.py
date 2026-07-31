#!/usr/bin/env python3
"""Report the shared search identity and one deterministic worker assignment."""

from __future__ import annotations

import argparse
import json

MAX_U64 = (1 << 64) - 1


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description=(
            "Report the shared global range/seed and the subrange that the "
            "oneshotsea CLI will assign to one worker."
        )
    )
    p.add_argument("--global-start", type=int, required=True)
    p.add_argument("--global-count", type=int, required=True)
    p.add_argument("--worker-id", type=int, required=True, help="zero-based worker id")
    p.add_argument("--worker-count", type=int, required=True)
    p.add_argument("--seed", type=int, required=True, help="shared search seed")
    p.add_argument("--format", choices=("json", "shell"), default="json")
    return p


def main() -> int:
    args = parser().parse_args()
    if args.global_start < 0 or args.global_count <= 0:
        raise SystemExit("global-start must be nonnegative and global-count positive")
    if args.worker_count <= 0 or not 0 <= args.worker_id < args.worker_count:
        raise SystemExit("require 0 <= worker-id < worker-count")
    if args.seed < 0:
        raise SystemExit("seed must be nonnegative")
    global_end = args.global_start + args.global_count
    if (
        args.global_start > MAX_U64
        or global_end > MAX_U64
        or args.worker_count > MAX_U64
        or args.seed > MAX_U64
    ):
        raise SystemExit("search values must fit the unsigned 64-bit CLI contract")

    width, remainder = divmod(args.global_count, args.worker_count)
    count = width + (1 if args.worker_id < remainder else 0)
    start = args.global_start + args.worker_id * width + min(args.worker_id, remainder)
    end = start + count
    result = {
        "worker_id": args.worker_id,
        "worker_count": args.worker_count,
        "range_start": str(args.global_start),
        "range_end": str(global_end),
        "range_count": str(args.global_count),
        "seed": str(args.seed),
        "assigned_range_start": str(start),
        "assigned_range_end": str(end),
        "assigned_range_count": str(count),
    }
    if args.format == "shell":
        for key, value in result.items():
            print(f"{key.upper()}={value}")
    else:
        print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
