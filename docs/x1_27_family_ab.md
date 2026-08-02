# p125 X1(11) versus X1(27) family A/B

This note records the bounded same-binary gate for choosing between the
point-four X1(11) and X1(27) search families. The result supports promoting
X1(27) for the next production identity: it measured **1.08675x wall curve
throughput** in this window. That is throughput evidence, not empirical yield.
All four runs found zero certificates.

The compact machine-readable record is
[`artifacts/local/p125-x1-27-family-ab-20260801/result.json`](../artifacts/local/p125-x1-27-family-ab-20260801/result.json).
It pins the command construction, identities, raw-output hashes, parsed sums,
calculations, and limitations. Raw files remain host-local under
`/private/tmp/oneshotsea-x127-ab-07bb`.

## Setup and identity

The order was X1(11) baseline, two X1(27) runs, then X1(11) baseline. Every
invocation used frozen binary SHA-256
`d4d839b889fc4f4cf50d70e5a17743c6f34b11ffbf29d1f5804026f382394fac`
from commit `07bbda3333ed88297d8a5a3a15650584e8956070`, seed
`202607300000`, indices `[0,10)`, ten curve threads, one SEA thread and one
smooth thread per curve, maximum level 401, trace cap 16, the same
authenticated smooth cache, Weber table set, verifier, and Python runtime.
Both families required point four. Their distinct schedule digests were
`bb2c036c...5d75bc` for X1(11) and `984ab615...5d8725` for X1(27).

The opt-in incomplete-skip capability was enabled symmetrically. It was never
used: every curve record reports `heuristic:false`, every checkpoint reports
zero heuristic rejections, and every curve ended in a sound smoothness
rejection. Each run exhausted all ten indices, reached smoothness ten times,
completed six point counts, and found zero certificates. The summary's
`verified:false` is expected because there was no certificate to verify.

## Measured result

| run | family | wall | user | sys | sum generation | sum SEA | sum smoothness | sum curve work | SEA levels | final candidates |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| B1 | X1(11) | 215.53 s | 1109.97 s | 5.21 s | 12.379 s | 802.164 s | 250.149 s | 1064.692 s | 578 | 27 |
| X1 | X1(27) | 197.74 s | 944.66 s | 4.34 s | 43.538 s | 615.578 s | 229.097 s | 888.214 s | 542 | 22 |
| X2 | X1(27) | 198.12 s | 948.12 s | 5.13 s | 44.315 s | 616.360 s | 231.115 s | 891.790 s | 542 | 22 |
| B2 | X1(11) | 214.67 s | 1113.99 s | 5.34 s | 12.350 s | 795.792 s | 249.186 s | 1057.328 s | 578 | 27 |

The two-run means are 215.10 seconds for X1(11) and 197.93 seconds for
X1(27), or 21.510 versus 19.793 wall seconds per curve. Thus X1(27) used 7.98%
less wall time and delivered **1.08674784x observed wall throughput**.

Generation was 3.55259x slower for X1(27), as expected from the more expensive
parameterization, but SEA dominated the window. Mean summed SEA work improved
from 798.978 to 615.969 concurrent curve-seconds, a 1.29711x ratio. Mean total
reported curve work improved by 1.19214x. These per-curve timing sums overlap
under the ten-curve scheduler; they describe task work and must not replace
the measured 1.08675x invocation-wall throughput.

After removing timings, shared process RSS, and embedded mutable state, both
repetitions within each family have byte-identical normalized progress. Their
checkpoints are also byte-identical. This confirms deterministic mathematical
replay within a family; it does not make the families paired. X1(11) and
X1(27) use different deterministic curve streams even when their index labels
match. The independent sample size remains ten curves per family, not twenty.

## Separate modeled opportunity rate

The checked-in Dickman--Mertens model was evaluated at the conservative cyclic
divisors 44 for X1(11) and 108 for X1(27). It gives a **1.068973x modeled
smooth-order opportunity multiplier** for X1(27) over X1(11). Multiplying that
model by the observed wall-throughput ratio gives **1.16170x modeled
opportunities per wall time**.

As a sensitivity only, selected-side group divisors 176 and 432 give a
1.073699x modeled opportunity multiplier and a 1.16684x combined planning
rate. A group-order divisor need not divide the group exponent or supply a
point of that exact order, so the cyclic comparison is the conservative one.

Neither result is an observed certificate-rate multiplier. The model treats
selected and paired smooth-tail marginals independently and omits family trace
bias, curve/twist correlation, group-exponent restrictions, representability,
exact-order assembly, and canonical verification. The empirical certificate
count in this benchmark is zero for both families.

## Decision and limits

Promote X1(27) point-four for the next production identity. The bounded gate
shows a material wall-throughput improvement in both orders, while the
separate conservative opportunity model is also favorable. Retain X1(11) as
a reproducible fallback and monitor the longer production sample for tail
behavior.

This is still an `n=10` family comparison. SEA and smoothness have long tails,
host scheduling and memory pressure can move a ten-curve wave, and the repeated
runs time the same deterministic curves rather than drawing new observations.
Reported peak RSS is a shared-process high-water mark, not isolated per-curve
memory. Finally, `/usr/bin/time -l` printed usable real/user/sys values but
then reported `sysctl kern.clockrate: Operation not permitted`; no unavailable
extended counter is inferred.
