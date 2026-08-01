# Local p125 search readiness audit, 2026-08-01

This audit identifies the next non-overlapping production action on the Apple
M4 after the bounded gate at Git commit
`43528617c6494202ecc6d3f5e8b97561e3139bfe`.  It treats the
ignored `work/p125` tree as live evidence.  Older benchmark prose is not used
as authority where that tree was subsequently extended.

## Authenticated inputs

- The bounded gate used executable SHA-256
  `7e3348b3cdf9fa3abfa5f00eba50e504024c77e1d9b405b692ddfc92783369db`
  and build id
  `git:43528617c6494202ecc6d3f5e8b97561e3139bfe+binary-sha256:7e3348b3cdf9fa3abfa5f00eba50e504024c77e1d9b405b692ddfc92783369db`.
  The emitted start record authenticated that exact value before work began.
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

## Bounded committed-build gate

The prescribed index-2 gate completed on commit `4352861` and advanced a
fresh `[2,1000000)` checkpoint to 3.  It was a sound smoothness rejection, not
an implementation-limit or heuristic result.

| Evidence | Value |
|---|---:|
| SEA passes / levels | 1 / 66 |
| Exact / certified Atkin levels | 29 / 2 |
| Exact / effective trace candidates | 25 / 3 |
| Curve work / SEA / exact smoothness | 1,325.776 / 1,320.045 / 5.720 s |
| Modular roots / eigenvalues | 1,156.327 / 143.213 s |
| Cold invocation including cache load | 1,436.58 s |
| Maximum resident set / peak footprint | 5,457,362,944 / 10,808,221,568 bytes |

The three effective traces independently replayed all logged exact CRT and
Atkin residue-set constraints with `tools/audit_sea_progress.py`.  The durable
checkpoint, curve progress, and complete invocation-log SHA-256 values are,
respectively,
`ee846fa8a96250c666a63298f5c6278cab5611ed8ad780fd0429f52cc200b1ad`,
`59c3364b84da6183c30764c7766bb5261751a2bf47e93c57a172028420bd0b65`,
and `8f7497d1895901f053371a2ea77ef93116e1fc76646ab28b1921c96be5eabf06`.
The ignored raw files remain under `work/p125/search-4352861`; the compact
committed result is under `artifacts/local/p125-index2-20260801`.

## Unique yield evidence

Global indices 0 and 1 in the filtered log and the new index-2 gate are the
three independent production samples.  The old singleton point count,
cap-4096 experiment, baseline index-1 record, and optimized index-1 replay
duplicate one of those indices and must not inflate yield.

| Index | Generator rejects | SEA levels / exact | Traces screened | Result |
|---:|---:|---:|---:|---|
| 0 | 1 | 54 / 31 | 19 (38 curve/twist orders) | sound smoothness rejection |
| 1 | 0 | 60 / 31 | 4 (8 curve/twist orders) | sound smoothness rejection |
| 2 | 0 | 66 / 29, plus 2 Atkin | 3 (6 curve/twist orders) | sound smoothness rejection |

Thus the unique retained yield is zero smooth-enough orders out of 52 screened
orders, zero assembly calls, and zero certificates from three curves.  This is
too little evidence for a defensible certificate waiting-time estimate.  Even
an independence-assuming binomial calculation gives only a 5.60% one-sided
95% upper bound per screened order, and the orders within a curve are not
independent.  It must not be presented as a certificate probability.

The larger generator probe admitted 32 curves after 41 incompatible images,
or about 43.8% admission.  Generation takes milliseconds and is immaterial
next to SEA, so it does not change the compute forecast.

## Throughput and trace-cap decision

The production tail variance is material.  The deterministic index-1 reducer
replay took 299.917 seconds, while the committed index-2 gate took 1,325.776
seconds after the cache was resident.  The new direct observation is only 2.72
curves/hour warm (2.51/hour including its cold cache load), not the prior
single-curve estimate of 12/hour.  A defensible forecast therefore remains a
measured range of roughly 2.7--12 curves/hour until more unique curves finish;
do not extrapolate a certificate waiting time from either endpoint.

On index 2, modular roots consumed 87.6% and eigenvalues 10.8% of SEA time.
The ten-thread run accumulated 6,795.35 user-seconds during 1,436.58 wall
seconds, so serial phases and uneven per-level jobs leave substantial cores
idle even though the modular-root portions can saturate all ten.

Keep `--trace-cap 64`.  On index 0, screening 195 traces at level 283 cost more
than finishing SEA to the singleton.  On index 1, cap 4096 took 1,097.637
seconds versus 777.153 seconds for the old cap-64 baseline and 299.917 seconds
for the optimized cap-64 replay.  No retained evidence supports a larger cap.

## Next local action

Commit and test the per-level live telemetry added after this gate, then start
a clean identity at global index 3.  A changed executable cannot reuse the
index-2 checkpoint because build identity is deliberately authenticated; the
new half-open range must therefore begin at 3 to avoid overlap.  The command
shape below is the audited template, but its build id and directory must be
replaced with the new committed build before launch.

```sh
set -o pipefail
test ! -e work/p125/search-NEXT
mkdir work/p125/search-NEXT
/usr/bin/time -l ./build/oneshotsea search \
  --p 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 \
  --seed 202607300000 \
  --range-start 3 --range-end 1000000 \
  --worker-id 0 --worker-count 1 \
  --max-level 401 --trace-cap 64 --sea-threads 10 \
  --table-dir data/modpoly/weber_f \
  --smooth-cache work/p125/smooth.cache \
  --smooth-cache-sha256 afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551 \
  --smooth-threads 8 --smooth-max-batch 128 \
  --smooth-root-auxiliary-bytes 134217728 \
  --checkpoint work/p125/search-NEXT/checkpoint.json \
  --checkpoint-every 1 \
  --progress work/p125/search-NEXT/progress.jsonl \
  --certificate-out work/p125/search-NEXT/certificate.txt \
  --build-id git:COMMIT+binary-sha256:BINARY_SHA256 \
  --max-curves 1000 2>&1 | tee work/p125/search-NEXT/search.log
```

Before the continuation, confirm the emitted search-start record has
the expected cache, table, range, seed, build, and schedule identities; confirm
the curve record is non-heuristic and either advances soundly or produces a
locally verified certificate.  Keep one ten-thread worker initially: the
5.5 GB resident cache makes multi-process throughput a separate memory and
scaling experiment on the 16 GB M4.  Each completed SEA level now emits and
flushes `oneshotsea.search-sea-level.v1`, while only complete curve records
and checkpoints advance durable state.
