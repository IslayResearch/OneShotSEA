# p125 Weber versus X1(11) point-four family A/B

This note records the bounded, same-build p125 family comparison requested by
the local-search readiness audit.  It is benchmark evidence only.  Both runs
covered global indices `[12,22)`, but neither checkpoint advances or
authenticates a production cursor.

The complete compact record is
[`artifacts/local/p125-x1-11-family-ab-20260801/result.json`](../artifacts/local/p125-x1-11-family-ab-20260801/result.json).
The host-local raw checkpoint, progress, and verbose log files remain under
`/tmp/oneshotsea-family-ab-20260801.Ba9BnP`; the artifact pins their paths,
line counts, SHA-256 digests, and checkpoint CRC64 values.

## Authenticated identity and setup

Both invocations emitted the same committed build identity:

- Git commit `4d0df939c38d225da6e86020968e370b61920798`
- binary SHA-256
  `1ab2ffa06dff5970955675814c2cd5198dee4150476397e83fc5c208eee09cf2`
- smooth-cache SHA-256
  `afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551`
- authenticated Weber table-set SHA-256
  `ac1fb3eafd991bccae2fcc05572108f318522b15fd6a3a164b8665c16f2d6bd5`
- canonical verifier SHA-256
  `e0ba3b8a7ed2ff48bd2fd824642bf67b0954a9f03f57daeb4ac4302691e1b666`
- Python SHA-256
  `b502cb4c5b46b8d4192ec6bcb600ce8922f1afc396fcf646e8765c6eba74a0bf`

The common effective configuration was seed `202607300000`, worker `0/1`,
ten rolling curve threads, one SEA thread and one smooth thread per curve,
maximum Weber level 401, trace cap 64, smooth batch 128, 128 MiB of smooth-root
auxiliary memory, checkpoint interval one, and the production assembly and
candidate limits.  The Weber run used family `weber-f`, point-four false, and
schedule digest `84bf5d7a...f11f25`.  The alternate used family `x1-11`,
`--x1-require-point4 1`, and schedule digest `d81c3f2a...abec3`.

The logs authenticate effective configuration and identity but do not echo the
original argv.  The artifact therefore records values, paths, and the timing
wrapper without claiming exact CLI token order or whether a default-valued
option was explicitly written.

## Result

| family | real | user | sys | wall seconds/curve | wall curves/second | sum SEA | sum smoothness | sum generation | full point counts | peak RSS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Weber-f | 306.19 s | 1892.70 s | 5.64 s | 30.619 | 0.0326595 | 1230.119 s | 614.143 s | 0.085 s | 3/10 | 6.482 GiB |
| X1(11), point-four | 294.57 s | 1618.52 s | 4.94 s | 29.457 | 0.0339478 | 1217.423 s | 327.610 s | 22.386 s | 3/10 | 5.969 GiB |

X1 completed the bounded invocation 11.62 seconds sooner: 3.80% less wall
time, or **1.03945x the observed wall curve throughput**.  The sum of concurrent
per-curve `total_us` was 1844.347 seconds for Weber and 1567.419 seconds for
X1, a 1.17668 ratio.  That second ratio is a description of this sample's
concurrent curve tasks, not wall throughput; it must not replace 1.03945.

Every curve reached exact smoothness screening and ended in a sound smoothness
rejection.  Each run recorded ten sound early aborts, three full point counts,
zero completed-without-certificate cases, and zero certificates.  A summary
field of `verified: false` is consequently expected: no certificate existed to
verify.  The artifact retains every index's generation, SEA, smoothness and
total timings, initial trace count, final trace when present, full-count flag,
SEA-level counts, and reported RSS.

## Opportunity-rate interpretation

Point-four X1(11) guarantees a rational point of exact order 44 and divisibility
of the selected group order by 88.  For a conservative adjustment, use the
checked-in yield model's exact-cyclic-44 smooth-opportunity multiplier of
1.1775, not the stronger group-order-88 sensitivity.  Multiplying by observed
wall throughput gives:

`1.039447 x 1.1775 = 1.223949`

Thus this bounded observation supports a **1.22395x conservative modeled
smooth-order opportunity rate**, conditional on the model.  The group-order-88
sensitivity would be 1.28829x, but it does not guarantee a point of exact order
88 or the same divisor in the group exponent.

Neither number is an observed yield or certificate-rate multiplier.  Both
runs found zero certificates, and the Dickman model omits family trace bias,
curve/twist dependence, group-exponent restrictions, exact-order assembly,
and canonical verification.

## Limits on the comparison

Matching index labels do not make the observations paired: Weber-f and X1(11)
sample different deterministic curve distributions.  With only ten curves per
family, SEA and exact-smooth tails, scheduling overlap, and host load can move
the result substantially.  The roughly 0.512 GiB lower X1 process peak RSS is
descriptive for this window only; reported RSS is a shared-process high-water
mark repeated in each ordered curve record, not isolated per-curve memory.

Finally, `/usr/bin/time -l` printed real, user, and sys times for both runs but
also reported `sysctl kern.clockrate: Operation not permitted`.  The search's
own peak-RSS fields remain available; extended host counters blocked by that
sysctl call are not inferred.
