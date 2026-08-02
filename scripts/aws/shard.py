#!/usr/bin/env python3
"""Report one exact production-CLI partition of a shared global range."""

from __future__ import annotations

import argparse
import json

MAX_U64 = (1 << 64) - 1


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Report the shared range/seed passed to every worker and the "
            "contiguous subrange assigned by oneshotsea to one worker."
        )
    )
    parser.add_argument("--global-start", type=int, required=True)
    parser.add_argument("--global-count", type=int, required=True)
    parser.add_argument("--worker-id", type=int, required=True)
    parser.add_argument("--worker-count", type=int, required=True)
    parser.add_argument("--seed", type=int, required=True)
    args = parser.parse_args()

    if args.global_start < 0 or args.global_count <= 0:
        raise SystemExit("global-start must be nonnegative and global-count positive")
    if args.worker_count <= 0 or not 0 <= args.worker_id < args.worker_count:
        raise SystemExit("require 0 <= worker-id < worker-count")
    if args.seed < 0:
        raise SystemExit("seed must be nonnegative")
    global_end = args.global_start + args.global_count
    if any(
        value > MAX_U64
        for value in (
            args.global_start,
            global_end,
            args.worker_id,
            args.worker_count,
            args.seed,
        )
    ):
        raise SystemExit("search values must fit the unsigned 64-bit CLI contract")

    width, remainder = divmod(args.global_count, args.worker_count)
    assigned_count = width + int(args.worker_id < remainder)
    assigned_start = (
        args.global_start
        + args.worker_id * width
        + min(args.worker_id, remainder)
    )
    result = {
        "worker_id": args.worker_id,
        "worker_count": args.worker_count,
        "range_start": str(args.global_start),
        "range_end": str(global_end),
        "range_count": str(args.global_count),
        "seed": str(args.seed),
        "assigned_range_start": str(assigned_start),
        "assigned_range_end": str(assigned_start + assigned_count),
        "assigned_range_count": str(assigned_count),
    }
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
