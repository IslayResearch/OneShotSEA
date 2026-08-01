# Local p125 search readiness audit, 2026-08-01

This audit identifies the next non-overlapping production action on the Apple
M4 after the retained searches through global index 7 and the exact conjugate
Frobenius optimization at Git commit
`164814bbeb55c7408511d7a53dfedbc5c39db47a`.  It treats the ignored
`work/p125` tree as live evidence.  Older benchmark prose is not used as
authority where that tree was subsequently extended.

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
- The most recent production identity before orbit reuse was commit
  `4eda9277ebec63bab99dc42f2a9bd591be350070`, executable SHA-256
  `0a973fc7cc00c1ca0910aa2b2e7be66213143c1103c05d0d282c171843c60d93`,
  and range `[3,1000000)`.  Its retained checkpoint is at `next_index=6` after
  three sound rejections.  Checkpoint, progress, and log SHA-256 values are
  `0e1076b163dcbd354cbdb43390ec12ab587d990a01d5c6d36e480db728751491`,
  `b090397fb954cffba19e86e95a218d18c3439cc6d8f6cad76c1cb7e130a5b8fa`,
  and `2361c19cd59c6a3fb3c950d72f5e08699dfe76de63e6e3ec0cb9067384a5d68f`.
  Because orbit reuse changed the executable identity, this checkpoint is now
  historical evidence only.
- The orbit-reuse production identity was commit
  `deaeaf4ee5003348f158b93d875651e417c143fc`, executable SHA-256
  `ba10fe7f7887e98d67e704e5322d740c0345122899e3faff339e2066a02fde48`,
  and range `[6,1000000)`.  It soundly rejected indices 6 and 7 before being
  stopped during the replayable prefix of index 8.  Its durable checkpoint is
  `next_index=8`; checkpoint, progress, log, and launch-script SHA-256 values
  are `659aa0b09d0f2af6423f2ac6c34ab1fbc6e47ad530b1bbfe70336d4cddaedc64`,
  `9fa5faa0ddadf013fd34791f86179e4d895404b467f31702432938c701042391`,
  `2c921f50db5bb7abc95941bb2ff1b1ed6f2e2d81868d214c772313e5202eb965`,
  and `95d7cf0880373fc630289547367e6c1b68f019852c002127c3e64a2b38717998`.
  The exact executable is retained locally with its original digest; the next
  optimized identity must begin at 8 rather than resume this checkpoint.

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

Global indices 0--7 are the eight independent production samples.  The old
singleton point count, cap-4096 experiment, baseline index-1 record, optimized
index-1 replay, and optimized index-4 replays duplicate those indices and must
not inflate yield.

| Index | Generator rejects | SEA levels / exact | Traces screened | Result |
|---:|---:|---:|---:|---|
| 0 | 1 | 54 / 31 | 19 (38 curve/twist orders) | sound smoothness rejection |
| 1 | 0 | 60 / 31 | 4 (8 curve/twist orders) | sound smoothness rejection |
| 2 | 0 | 66 / 29, plus 2 Atkin | 3 (6 curve/twist orders) | sound smoothness rejection |
| 3 | 0 | 59 / 31, plus 1 Atkin | 12 (24 curve/twist orders) | sound smoothness rejection |
| 4 | 0 | 64 / 31 | 15 (30 curve/twist orders) | sound smoothness rejection |
| 5 | 0 | 57 / 31, plus 1 Atkin | 17 (34 curve/twist orders) | sound smoothness rejection |
| 6 | 0 | 50 / 32 | 10 (20 curve/twist orders) | sound smoothness rejection |
| 7 | 0 | 67 / 29, plus 1 Atkin | 22 (44 curve/twist orders) | sound smoothness rejection |

Thus the unique retained yield is zero smooth-enough orders out of 204 screened
orders, zero assembly calls, and zero certificates from eight curves.  This is
too little evidence for a defensible certificate waiting-time estimate.  Even
an independence-assuming binomial calculation is misleading because the
orders within a curve and nearby smoothness events are not independent.  It
must not be presented as a certificate probability.

The larger generator probe admitted 32 curves after 41 incompatible images,
or about 43.8% admission.  Generation takes milliseconds and is immaterial
next to SEA, so it does not change the compute forecast.

## Throughput and trace-cap decision

The pre-orbit production tail variance is material.  Complete curve work for
indices 2--5 was 1,325.776, 475.550, 1,441.080, and 509.937 seconds.  The
root-orbit change then replayed the expensive 64-level index 4 in 377.42
seconds of direct SEA wall time, with all mathematical level records
identical.  That is a 3.759x comparison against the old 1,418.823-second SEA
stage, but it excludes cache loading and final smoothness.  Complete
orbit-optimized curve work was 180.496 seconds at index 6 and 525.564 seconds
at the 36-lift index 7, a measured warm range of 6.85--19.95 curves/hour.

Conjugate eigenvalue reuse then replayed index 4 in 291.43 seconds, with all 64
canonical level records identical.  It reduced the orbit-only eigenvalue
subtotal from 148.418 to 70.196 seconds by replacing 30 of 61 full recoveries
with exact determinant-derived conjugates.  This is a controlled replay, not
yet a complete search-curve observation; use production records from index 8
for the next throughput update.

On index 2, modular roots consumed 87.6% and eigenvalues 10.8% of SEA time.
The ten-thread run accumulated 6,795.35 user-seconds during 1,436.58 wall
seconds, so serial phases and uneven per-level jobs leave substantial cores
idle even though the modular-root portions can saturate all ten.

Keep `--trace-cap 64`.  On index 0, screening 195 traces at level 283 cost more
than finishing SEA to the singleton.  On index 1, cap 4096 took 1,097.637
seconds versus 777.153 seconds for the old cap-64 baseline and 299.917 seconds
for the optimized cap-64 replay.  No retained evidence supports a larger cap.

## Next local action

Publish the cleanly tested conjugate-eigenvalue implementation, then start a
clean identity at global index 8.  A changed executable cannot reuse the
`deaeaf4` checkpoint because build identity is deliberately authenticated; the
new half-open range must therefore begin at 8 to avoid overlap.  The command
shape below is the audited template, but its build id and directory must be
replaced with the new committed build before launch.

```sh
set -o pipefail
test ! -e work/p125/search-NEXT
mkdir work/p125/search-NEXT
/usr/bin/time -l ./build/oneshotsea search \
  --p 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 \
  --seed 202607300000 \
  --range-start 8 --range-end 1000000 \
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
