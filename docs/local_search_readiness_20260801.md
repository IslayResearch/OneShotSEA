# Local p125 search readiness audit, 2026-08-01

This audit identifies the next non-overlapping production action on the Apple
M4 after the retained searches through global index 11 and the complete clean
gate for measured scheduling commit
`c5de5735e722baf79592da96e8b07c1dd8a6aee5`.  The clean release executable
hashes to `e05a393ff57ba9d0948fc8e91d1f2f91e0af427f73c27a7c699168f03ea0aeea`.
It treats the ignored
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
- The conjugate-optimized production identity was commit
  `9fd6fa35323a045d7f1e03cce33597bf7abae10c`, executable SHA-256
  `83392eb8d362e21e894a0fad3c38f77c9ec68ca9264d124e2695e20f673c1421`,
  and range `[8,1000000)`.  Index 8 completed as a sound rejection before the
  worker was stopped during a replayable prefix of index 9.  Its durable
  checkpoint is `next_index=9`; checkpoint, progress, and log SHA-256 values
  are `794b8258d45e5339b0e309716ff960f7cdff7003a1a264858dd1a810a2300a68`,
  `8930229042226cb5d5a973b835b17ebda57e9178f1a026575c8313923f0266f7`,
  and `46d5de047baceaadef9100b73a652d91e82245b12dbbbe13868707ab74a5024b`.
  The next changed executable must start a fresh range at 9.
- The measured-scheduling production identity was commit
  `c5de5735e722baf79592da96e8b07c1dd8a6aee5`, executable SHA-256
  `e05a393ff57ba9d0948fc8e91d1f2f91e0af427f73c27a7c699168f03ea0aeea`,
  and range `[9,1000000)`.  It soundly rejected indices 9--11 and was stopped
  during the replayable prefix of index 12.  Its durable checkpoint is
  `next_index=12`; checkpoint, progress, and log SHA-256 values are
  `2b584f325040b111b1464b3464fce3083b29e36a0ecb4757c23f9e67f95841e0`,
  `2cdad57b734cfcbaebbb32aea3c0004ef4a8e760e8f57c024a6e05c522109ba4`,
  and `537100931d6b8aaac9fdcba3ae8ca49f3e078bfb570a597c5b0c9ab30f742682`.
  The next changed executable must start a fresh range at 12.
- The trace-prior/cap-16 X1(11) production identity was commit
  `1e84475a7c5440df8b3446a8f7c4362344d28b62`, executable SHA-256
  `feb9c06aa0303c699b4c6c9f777b53e0936b2e64b6a6908faa2ee0fc723288aa`,
  and range `[12,1000000)`.  It durably processed indices 12--59 as 48
  sound early rejections, including 26 full point counts, before a clean
  pause.  Its checkpoint is `next_index=60`; checkpoint, progress, and log
  SHA-256 values are
  `064b12c392db7ae41019e5b843bd7a10d768b7b0d55cab8916694dde9ca17c45`,
  `ee7496e34c619ad23ba9910c8eaabeea9956c26442cfe2a37d0e28e20b99d032`,
  and `2ad8d832d849348bcfda9313da026c8ee3256b08f078281343775e2ec91a0d55`.
  The retained-source policy changes the schedule digest, so the new committed
  identity must start a fresh range at 60 rather than load this checkpoint.
- The retained-source production identity was commit
  `d957b156966f89948fef64a7cad0527358365264`, executable SHA-256
  `14fa5ac090eef32008d898f7bfb389f74aec4bc4a9eb9b1fad32e476868abfcc`,
  schedule `efad4eb5bf58fa3bf08045d296da89b0f3000a8ebd59755e920220b225f0f82a`,
  and range `[60,1000000)`.  It durably processed indices 60--89 as 30
  sound early rejections, including 15 full point counts, then paused for the
  exact arithmetic and modulus-176 gates.  Its checkpoint is `next_index=90`;
  checkpoint, progress, and log SHA-256 values are
  `36686f6926354cd7eee399f2e1ef83dd35f84af1c575d2012433cb6fe6b84ea6`,
  `9156ae704a280d97273017f05681e5fd1acb725f14e480aa6b06915ce707528e`,
  and `6eb321d21978c65641afdd4264fd4184e40df555019f818031b90200e29d5f08`.
  The exact binary is pinned in the run directory; the restart script checks
  its digest before execution.  The stronger trace-prior policy changes the
  schedule identity, so its successor must begin at 90.
- The modulus-176/deferred-normalization production identity was commit
  `9655670bc8667cfee91afd91b4cdbca5ce2b1752`, executable SHA-256
  `77e42838a0388b152fa03949da33fd52375fda619de786260b1fcac6baf001f7`,
  schedule `55c31578d49d91ca9050fd3eba94c93f9bfdb170b818bf4a96dff718fdc04641`,
  and range `[90,1000000)`. It durably processed indices 90--145 as 56
  sound early rejections, including 32 full point counts, with zero heuristic
  rejections and zero certificates. Its checkpoint is `next_index=146`, CRC64
  `6b9f21329467e794`; checkpoint, progress, log, and launch-script SHA-256
  values are
  `3f19eea6c41a9e685a52395d6c20926d4095e96804af12d8dd4208efd1cbbcd0`,
  `198053f6c8dd131f6c487fd451ee21b89c6538984355e37f012cf472b9f32831`,
  `43e255833ac4e3a4dfbf8727301de7ec0b3998a5f5cabb523cde985dc763e05f`,
  and `37e18b26294b66e7c9ae5792bd6cfe5b7e6e46d6d200f638688fead35c902c7a`.
  The immutable binary remains at `work/oneshotsea-9655670`. A failed nested-
  binary startup, which touched no checkpoint, is separately preserved and is
  not outcome evidence. The exact-cost Frobenius-window arithmetic changes the
  build identity, so its successor begins at 146 rather than loading this
  checkpoint.
- The exact-cost polynomial-window production identity was commit
  `85d289a72b94b00002a3acfe93bffc0754223d0f`, executable SHA-256
  `2adedf2e7bb69886ff3b6d586966577b57b87191868ac89dc67fed556b9aff23`,
  schedule `55c31578d49d91ca9050fd3eba94c93f9bfdb170b818bf4a96dff718fdc04641`,
  and range `[146,1000000)`. It durably processed indices 146--176 as 31
  sound early rejections, including 13 full point counts, with zero heuristic
  rejections and zero certificates. Its checkpoint is `next_index=177`, CRC64
  `1056740c16f1fee6`; checkpoint, progress, log, and launch-script SHA-256
  values are
  `75f392dccd341d1bec2c8be9f93e3987330e181c53621edd0631b2af594252f5`,
  `8e67d326fcf1336fd57f10d35c6b790cf85fad5bf4eb1b64070160733c5b708a`,
  `8ec91acfeea740a0dfbbaab827a4e9b012ad2b9f46ad108449806d169e4c30f7`,
  and `f1f86f0a07d2cb955d4ca458ad2901ce6a17312015d80038d5ffff210635a099`.
  The pinned binary is mode 0555 at `work/oneshotsea-85d289a`. The
  polynomial-subring Element delegation changes the build identity, so its
  successor begins at 177 rather than loading this checkpoint.

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

Global indices 0--11 are the twelve independent production samples.  The old
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
| 8 | 0 | 65 / 31, plus 1 Atkin | 2 (4 curve/twist orders) | sound smoothness rejection |
| 9 | 7 | 57 / 31, plus 1 Atkin | 21 (42 curve/twist order screens) | sound smoothness rejection |
| 10 | 1 | 63 / 30, plus 1 Atkin | 3 (6 curve/twist order screens) | sound smoothness rejection |
| 11 | 3 | 59 / 31, plus 1 Atkin | 8 (16 curve/twist order screens) | sound smoothness rejection |

Thus 272 trace-candidate order values were exactly and soundly screened, with
zero assembly calls and zero certificates from twelve curves.  These are not
272 independent certificate opportunities: each curve has one actual trace
and therefore only two actual group orders, so the twelve curves contribute
24 actual curve/twist orders.  The other trace candidates provide sound early
rejection evidence but cannot increase hit probability.  A model-based search
forecast must preserve that distinction and account for the correlation
between `p+1-t` and `p+1+t`; the small observed sample cannot estimate the rare
tail directly.

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
a new search sample.  Index 8 supplied the first complete search-curve
observation: 318.729
seconds of SEA, 10.518 seconds of exact smoothness, and 329.264 seconds of
warm curve work for two final traces.  Peak RSS was 5,441,470,464 bytes.
The same clean scheduling binary then processed indices 9--11 in 254.163,
325.570, and 214.242 seconds of warm curve work.  Their SEA subtotals were
224.190, 313.713, and 204.736 seconds, and their exact-smooth subtotals were
29.936, 11.850, and 9.485 seconds.  The largest reported resident set was
5,578,883,072 bytes.

On index 2, modular roots consumed 87.6% and eigenvalues 10.8% of SEA time.
The ten-thread run accumulated 6,795.35 user-seconds during 1,436.58 wall
seconds, so serial phases and uneven per-level jobs leave substantial cores
idle even though the modular-root portions can saturate all ten.

Keep the general default at `--trace-cap 64`.  On index 0, screening 195 traces
at level 283 cost more than finishing SEA to the singleton.  On index 1, cap
4096 took 1,097.637
seconds versus 777.153 seconds for the old cap-64 baseline and 299.917 seconds
for the optimized cap-64 replay.  No retained evidence supports a larger cap.

The X1 point-four trace-prior path has a separate same-build result on indices
`[12,22)`: caps 64, 32, 16, and 1 took 272.19, 271.95, 260.34, and 260.30
seconds.  Cap 16 reduced bounded trace screens from 97 to 24 and summed curve
work from 1678.006 to 1435.541 seconds.  Cap 1's 0.04-second wall advantage is
measurement noise at this scale, while its summed curve work was 1.08% higher.
Select `--trace-cap 16` for the new X1 production identity.  The full audit is
in [the trace-cap note](x1_11_trace_cap.md).

## Completed family gate and exact trace prior

The prescribed same-build comparison is complete on commit `4d0df939`.  Both
families used the same binary, cache, table set, verifier, Python runtime,
indices `[12,22)`, ten rolling curve slots, and one SEA and smooth thread per
curve.

| Family | Invocation wall | Seconds/curve | Curves/second | Full point counts | Certificates |
|---|---:|---:|---:|---:|---:|
| Weber-f | 306.19 s | 30.619 | 0.0326595 | 3/10 | 0 |
| X1(11), point-four | 294.57 s | 29.457 | 0.0339478 | 3/10 | 0 |

X1 measured 1.03945x the Weber wall throughput.  The index labels do not make
this paired curve work: each family has a different deterministic distribution,
and `n=10` is too small to characterize the SEA or smoothness tails.  Every
observation was a sound smoothness rejection and neither run produced a
certificate.  The records are benchmark-only and do not advance the
authenticated production cursor at 12 or enlarge the unique yield sample.

The conservative checked-in yield adjustment uses the guaranteed X1 cyclic
divisor 44, whose modeled smooth-order opportunity multiplier is 1.1775.
Multiplying it by measured wall throughput gives 1.22395x modeled opportunity
rate.  The group-order-88 sensitivity is 1.28829x, but neither value is an
observed certificate-yield ratio.  Group-order divisibility does not guarantee
the same group-exponent divisor or exact-order assembly.  Full commands,
per-curve timing/trace/RSS data, raw-output hashes, and limitations are in
[the family A/B note](x1_11_family_ab.md) and
[compact artifact](../artifacts/local/p125-x1-11-family-ab-20260801/result.json).

The subsequently implemented exact trace-prior policy is fail-closed.  Weber
uses `t=p+1 (mod 4)` only after the actual short Weierstrass cubic is directly
validated to have full rational `E[2]`.  X1 uses
`t=+(p+1) (mod D)` when its selected Tate class is the canonical curve and
`t=-(p+1) (mod D)` when it is the canonical twist, with `D=44` or `88`
according to the validated sample; requiring point four forces `D=88`.  SEA
seeds both exact constraint states with this
prior and skips redundant `ell=11` for X1.  The policy version changes the
schedule digest, so the family A/B timings do not measure its additional SEA
reduction and no pre-policy checkpoint can be resumed under it.

## X1(27) family promotion gate

The current frozen binary at commit `07bbda3` completed a reverse-order
X1(11)/X1(27) point-four comparison on deterministic indices `[0,10)`. The
execution order was X1(11), X1(27), X1(27), X1(11), with the same cache, table
set, verifier, Python runtime, cap 16, and ten-curve scheduler throughout.

| Family | Two-run mean wall | Seconds/curve | Curves/second | Sound rejects/run | Certificates/run |
|---|---:|---:|---:|---:|---:|
| X1(11), point-four | 215.10 s | 21.510 | 0.0464900 | 10/10 | 0 |
| X1(27), point-four | 197.93 s | 19.793 | 0.0505229 | 10/10 | 0 |

X1(27) measured 1.08675x the wall throughput and is selected for the next
production identity. The separate conservative divisor model adds a
1.068973x smooth-order opportunity multiplier, for 1.16170x modeled
opportunities per wall time. That model is not observed yield: all runs found
zero certificates, equal indices are different family-specific curve streams,
and each repeat times the same ten curves. Full hashes, calculations, and
limitations are in [the X1(27) family A/B note](x1_27_family_ab.md) and
[artifact](../artifacts/local/p125-x1-27-family-ab-20260801/result.json).

## Index-206 exact fallback recovery gate

An authenticated prototype replay retried X1(11) point-four index 206 with cap
16, exact Schoof fallback enabled, and incomplete skip disabled. The retained
ordinary state had 45,948 exact and 9,189 effective candidates. Exact residues
`t=0 mod 3`, `t=0 mod 5`, `t=2 mod 13`, and `t=0 mod 17` reduced both complete
sets to 14. Exact smoothness screening then produced a nonheuristic
`sound_smoothness_reject`; there was no full point count, assembly call, or
certificate. Invocation wall was 172.17 seconds.

The final committed-build replay used commit
`976924b3aa2148f62ceae11948824d8aed5a41bb`, frozen binary SHA-256
`67a85ad69d176f8602af5e177819709f86893a32939691b2b4ca43fc8d7c7a70`,
and `--skip-incomplete-curves 0`. It reproduced the prototype's complete
mathematical projection and sound rejection in 169.36 seconds wall, with
3.922415 seconds of exact fallback work, then advanced its isolated checkpoint
to 207 complete. Its corrected raw log, progress, checkpoint hashes, and
checkpoint CRC are pinned in the artifact. The earlier `QHrL4v` attempt is
excluded because its emitted full Git SHA failed the source-identity check.
The final recovery gate is therefore satisfied. Do not alter the old
production checkpoint's explicit heuristic record; the separate committed
replay supplies sound index-206 evidence. The full residue/count audit is in
[the fallback note](schoof_fallback.md) and
[artifact](../artifacts/local/p125-index206-schoof-fallback-20260801/result.json).

## Next local action

The authenticated production frontier is 246. Indices 0--205 and 207--245
have sound production outcomes; index 206 now has a separate final
committed-build sound replay, while its old production checkpoint remains an
explicit historical heuristic skip. Start a new X1(27) point-four identity at
global index 246 with exact fallback enabled and incomplete skip disabled;
every older checkpoint remains evidence rather than resumable state under the
new schedule.
The held-out scheduling A/B found the measured
alternate 0.7% slower, so SEA levels remain in increasing order.  Replace the
build id and `search-NEXT` directory below with the exact committed
production identity.

```sh
set -o pipefail
test ! -e work/p125/search-NEXT
mkdir work/p125/search-NEXT
/usr/bin/time -l ./build/oneshotsea search \
  --p 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 \
  --seed 202607300000 \
  --range-start 246 --range-end 1000000 \
  --worker-id 0 --worker-count 1 \
  --curve-family x1-27 --x1-require-point4 1 \
  --max-level 401 --trace-cap 16 --schoof-fallback 1 \
  --skip-incomplete-curves 0 --curve-threads 10 --sea-threads 1 \
  --sea-level-telemetry 0 \
  --table-dir data/modpoly/weber_f \
  --smooth-cache work/p125/smooth.cache \
  --smooth-cache-sha256 afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551 \
  --smooth-threads 1 --smooth-max-batch 128 \
  --smooth-root-auxiliary-bytes 134217728 \
  --checkpoint work/p125/search-NEXT/checkpoint.json \
  --checkpoint-every 1 \
  --progress work/p125/search-NEXT/progress.jsonl \
  --certificate-out work/p125/search-NEXT/certificate.txt \
  --build-id git:COMMIT+binary-sha256:BINARY_SHA256 \
  --max-curves 1000 2>&1 | tee work/p125/search-NEXT/search.log
```

Before the continuation, confirm the emitted search-start record has
the expected X1(27) family, point-four flag, exact-fallback policy, disabled
incomplete skip, cache, table, range, seed, build, and new schedule identities.
Confirm each curve record is non-heuristic and reports the expected mod-432
signed trace prior; confirm no redundant exact-prior SEA level is emitted. A
curve must either advance soundly or produce a locally verified
certificate.  Use one process with ten rolling curve slots, one SEA thread and
one smooth thread per curve.  The earlier same-binary scaling replay measured
27.071 warm seconds per curve and 6.19 GB peak RSS, with 68.9 MiB of system-wide
swapouts already included in its wall time; retain `vm_stat` monitoring and do
not raise concurrency beyond the ten physical cores without a new bounded A/B.
For the long run, `--sea-level-telemetry 0` suppresses live per-level records
and leaves the embedded `sea_level_timings` array empty, while retaining
per-curve SEA counts, major-kernel timings, outcomes, trace priors, RSS, and
durable checkpoints.  Benchmark runs should retain the default verbose level
telemetry.  Only complete curve records and checkpoints advance durable state.
