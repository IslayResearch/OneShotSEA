# Local p125 search readiness audit, 2026-08-01

This audit identifies the next non-overlapping production action on the Apple
M4 at Git commit `62ce891e5775e41330a1db00e5f5b91d93e6e794`.  It treats the
ignored `work/p125` tree as live evidence.  Older benchmark prose is not used
as authority where that tree was subsequently extended.

## Authenticated inputs

- An isolated `git archive` of the named commit builds successfully with
  `make -j4 all`.  Its executable SHA-256 is
  `0162e9284f3e8405a74046430bdbac37c1d329e7b95d63ff5c36644ee2acd128`.
  This isolated digest matters because a workspace binary built while other
  source changes are present is not an artifact of the named commit.
- `work/p125/smooth.cache` is 5,400,760,038 bytes and hashes to
  `afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551`,
  exactly the digest pinned in
  `data/smooth_cache/TRUSTED_MANIFEST.json`.  The manifest attributes its
  5,400,759,974 product bytes (plus the 64-byte portable header) to builder
  commit `25dd0fb29a90d2bacc5bee8009b4a1a23c1e2c05`, binary id
  `binary-sha256:3d535909ba6b8debbf52feb47a01af0d`, bound
  `29948379136`, 1,297,866,953 primes, segment span 500,000,000, and nine
  build threads.
- The retained filtered checkpoint binds table-manifest SHA-256
  `6976beb4d8306c04cc0bc6359eb104a71c761346dbe5055989f87ff381936047`
  and schedule SHA-256
  `e02abdd93ff5327f5ffadbe128b423a0ae32299871cf07c275ecf23abae92a2f`.
  Its range is `[0,1000000)`, seed `202607300000`, and cursor 2.

The filtered artifacts were extended after `docs/benchmark_20260731.md` was
written.  Their current authoritative digests are:

| Artifact | Current state | SHA-256 |
|---|---|---|
| `work/p125/filtered/checkpoint.json` | `next_index=2`, two attempts | `320d024dda38c571ff55662ce7f5bad23f678d92189877a0f3707281f0b18b82` |
| `work/p125/filtered/progress.jsonl` | records for indices 0 and 1 | `0df1f681abf12e41998b3d4aefaae525eb5d70d4bbb448a3b239efb94263f719` |
| `work/p125/filtered/first-curve.log` | index 0 invocation | `36a2eac3b7e7c7a3d5bbd370f5927932e863863225529b765c3e431baa32bbf0` |
| `work/p125/filtered/curve-1.log` | index 1 invocation | `f16fff9e866955d5e3c4703ce944b9aaa77f71b001580b42b71ed67d0b3b2ef5` |

That checkpoint cannot be resumed by the current executable: checkpoint
identity deliberately includes the old commit/binary build id.  Reusing its
path with a new build would be rejected.  The safe continuation is a fresh
identity whose global range starts at 2.

## Unique yield evidence

Only global indices 0 and 1 in the filtered log are independent production
samples.  The old singleton point count, cap-4096 experiment, baseline index-1
record, and optimized index-1 replay duplicate one of those indices and must
not inflate yield.

| Index | Generator rejects | SEA levels / exact | Traces screened | Result |
|---:|---:|---:|---:|---|
| 0 | 1 | 54 / 31 | 19 (38 curve/twist orders) | sound smoothness rejection |
| 1 | 0 | 60 / 31 | 4 (8 curve/twist orders) | sound smoothness rejection |

Thus the unique retained yield is zero smooth-enough orders out of 46 screened
orders, zero assembly calls, and zero certificates from two curves.  This is
too little evidence for a defensible certificate waiting-time estimate.  Even
an independence-assuming binomial calculation gives only a 6.30% one-sided
95% upper bound per screened order, and the orders within a curve are not
independent.  It must not be presented as a certificate probability.

The larger generator probe admitted 32 curves after 41 incompatible images,
or about 43.8% admission.  Generation takes milliseconds and is immaterial
next to SEA, so it does not change the compute forecast.

## Throughput and trace-cap decision

The current reducer was measured on the deterministic index-1 replay at
299.917 seconds of complete curve work: 293.641 seconds SEA and 6.270 seconds
exact smoothness.  This is 12.00 curves/hour, or 288 curves/day, once the cache
is resident.  The separate one-curve invocation took 369.18 seconds including
startup and cache authentication, a cold-launch rate of 9.75 curves/hour.
This forecast rests on one optimized curve; budget approximately 5--6 minutes
per admitted curve until a longer run measures variance.

Keep `--trace-cap 64`.  On index 0, screening 195 traces at level 283 cost more
than finishing SEA to the singleton.  On index 1, cap 4096 took 1,097.637
seconds versus 777.153 seconds for the old cap-64 baseline and 299.917 seconds
for the optimized cap-64 replay.  No retained evidence supports a larger cap.

## Next local action

First run exactly one clean, committed-build curve at index 2 in a new
directory.  This is both the build acceptance gate and the first
non-overlapping search work.  It leaves a checkpoint that can be continued by
removing only `--max-curves 1` from the same command.  The exact `62ce891`
command below is valid only when `./build/oneshotsea` matches the isolated
digest above; if in-flight source work is committed first, substitute that new
commit and clean-build digest rather than mislabeling the binary.

```sh
set -o pipefail
test ! -e work/p125/search-62ce891
mkdir work/p125/search-62ce891
/usr/bin/time -l ./build/oneshotsea search \
  --p 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 \
  --seed 202607300000 \
  --range-start 2 --range-end 1000000 \
  --worker-id 0 --worker-count 1 \
  --max-level 401 --trace-cap 64 --sea-threads 10 \
  --table-dir data/modpoly/weber_f \
  --smooth-cache work/p125/smooth.cache \
  --smooth-cache-sha256 afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551 \
  --smooth-threads 8 --smooth-max-batch 128 \
  --smooth-root-auxiliary-bytes 134217728 \
  --checkpoint work/p125/search-62ce891/checkpoint.json \
  --checkpoint-every 1 \
  --progress work/p125/search-62ce891/progress.jsonl \
  --certificate-out work/p125/search-62ce891/certificate.txt \
  --build-id git:62ce891e5775e41330a1db00e5f5b91d93e6e794+binary-sha256:0162e9284f3e8405a74046430bdbac37c1d329e7b95d63ff5c36644ee2acd128 \
  --max-curves 1 2>&1 | tee work/p125/search-62ce891/index-2.log
```

Before an unbounded continuation, confirm the emitted search-start record has
the expected cache, table, range, seed, build, and schedule identities; confirm
the curve record is non-heuristic and either advances soundly or produces a
locally verified certificate.  Keep one ten-thread worker initially: the
5.5 GB resident cache makes multi-process throughput a separate memory and
scaling experiment on the 16 GB M4.
