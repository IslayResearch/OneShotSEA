# Filtered p125 search benchmark, 2026-07-31

This record is the first production measurement after the deterministic
Montgomery-compatibility filter, bounded SEA modular-root concurrency, and the
filtered schedule-identity correction.  It searched global index zero from a
previously absent artifact directory and ended in a sound smoothness rejection.

## Immutable inputs and environment

- target: `nextprime(10^125)`, 416 bits;
- Git commit: `6db9b32e8d0e16d600f7ce8b488e173014954b48`;
- production binary SHA-256:
  `b9d3fe0261395f125bc53ecc5266d87eda96f4fe8d6db77ef7adc5c622e138a6`;
- filtered schedule SHA-256:
  `e02abdd93ff5327f5ffadbe128b423a0ae32299871cf07c275ecf23abae92a2f`;
- Weber table-manifest SHA-256 through level 401:
  `6976beb4d8306c04cc0bc6359eb104a71c761346dbe5055989f87ff381936047`;
- trusted 5,400,760,038-byte smooth-cache SHA-256:
  `afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551`;
- pinned verifier SHA-256:
  `e0ba3b8a7ed2ff48bd2fd824642bf67b0954a9f03f57daeb4ac4302691e1b666`;
- host: Apple M4, 16 GiB RAM, macOS 26.5.1 (25F80);
- compiler: Apple clang 21.0.0, arm64; and
- production build: `make clean && make -j4 all`, followed by the focused test
  suite and `make -q all` with a clean worktree.

The complete Git commit and complete binary digest were supplied as the
explicit build id; the run did not rely on the CLI's shortened default binary
identity.

## Command

The directory `work/p125/filtered` was confirmed absent before it was created.
The shell used `pipefail`, so `tee` could not hide a search failure.

```sh
set -o pipefail
test ! -e work/p125/filtered
mkdir work/p125/filtered
/usr/bin/time -l ./build/oneshotsea search \
  --p 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 \
  --seed 202607300000 \
  --range-start 0 --range-end 1000000 \
  --worker-id 0 --worker-count 1 \
  --max-level 401 --trace-cap 64 --sea-threads 10 \
  --table-dir data/modpoly/weber_f \
  --smooth-cache work/p125/smooth.cache \
  --smooth-cache-sha256 afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551 \
  --smooth-threads 8 --smooth-max-batch 128 \
  --smooth-root-auxiliary-bytes 134217728 \
  --checkpoint work/p125/filtered/checkpoint.json \
  --progress work/p125/filtered/progress.jsonl \
  --certificate-out work/p125/filtered/certificate.txt \
  --build-id git:6db9b32e8d0e16d600f7ce8b488e173014954b48+binary-sha256:b9d3fe0261395f125bc53ecc5266d87eda96f4fe8d6db77ef7adc5c622e138a6 \
  --max-curves 1 2>&1 | tee work/p125/filtered/first-curve.log
```

## Result

The generator cheaply rejected one certificate-incompatible Weber image, then
the admitted curve reached a sound early screen after 54 SEA levels, 31 of
them exact.  The screen contained 19 curve traces (38 curve/twist orders), none
with an exact smooth part above the certificate lower bound.  The cursor
advanced from zero to one.  No full point count, assembly attempt, or
certificate was produced.

| Measurement | Value |
|---|---:|
| Status | `sound_smoothness_reject` |
| Generator rejections | 1 |
| SEA | 584.849969 s |
| Exact smoothness | 13.461561 s |
| Reported curve total | 598.317583 s |
| End-to-end wall time, including cache authentication | 676.01 s |
| User / system CPU time | 3,255.23 s / 15.38 s |
| Search-report peak RSS | 5,557,174,272 bytes |
| `/usr/bin/time` maximum RSS | 5,557,256,192 bytes |
| Swaps during invocation | 0 |

SEA stage totals make the next optimization target unambiguous:

| SEA substage | Time | Share of SEA |
|---|---:|---:|
| Modular roots | 431.199718 s | 73.7% |
| Eigenvalues | 143.382281 s | 24.5% |
| BMSS | 9.262735 s | 1.6% |
| Normalized codomain | 0.845888 s | 0.1% |

The ten-worker ceiling was used at every recorded level.  High-level samples
reached about ten cores during modular-root work but returned to one core in
the serial parts, consistent with the stage totals above.

## Retained local artifacts

The large/generated artifacts remain under the ignored `work/p125/filtered`
directory.  Their content digests are:

| Artifact | SHA-256 |
|---|---|
| `checkpoint.json` | `a9a319fab31a39b9f95b72a3b31febfdd68c9356b093cca99d51be4dd03b2c35` |
| `progress.jsonl` | `01d78856aa738d0a009213ce878abf657485548ec74c211ade64f1be4c762dff` |
| `first-curve.log` | `36a2eac3b7e7c7a3d5bbd370f5927932e863863225529b765c3e431baa32bbf0` |

The checkpoint records `next_index=1` and the exact build, schedule, table,
cache, verifier, Python, range, seed, and worker identities required for a
resume.
