# p125 X1(25) versus X1(27) family gate

X1(25) is a measured losing route for this search and is not integrated. A
same-binary reverse-order p125 gate averaged **221.85 seconds** for X1(25)
versus **198.19 seconds** for X1(27) over deterministic indices `[0,10)`.
X1(27) therefore delivered **1.11938x wall throughput**. The checked-in
Dickman--Mertens model also gives X1(25) a slightly lower selected-side
opportunity, so the combined conservative planning rate is **0.88816x** the
X1(27) rate. Retain X1(27).

The compact evidence is in
[`artifacts/local/p125-x1-25-family-ab-20260801/result.json`](../artifacts/local/p125-x1-25-family-ab-20260801/result.json).
Raw logs remain under `/private/tmp/oneshotsea-x125-ab.BYFbEP`.

## Exact construction and identity

The isolated prototype implemented Sutherland and van Hoeij's optimized
X1(25) equation and map from
`https://math.mit.edu/~drew/X1/X1opt25new.txt`, SHA-256
`0bab11b78faa5ed48e00b20318fc728bbf45c805c9f3024bcf718869d1a8f2bc`.
It rejects every zero map denominator, constructs the Tate normal form,
checks the distinguished point has exact order 25, rejects singular and
exceptional-j curves, validates the retained Weber/Montgomery identity, and
directly checks full rational `E[2]` and the point-four condition.

For the p125 `p = 5 (mod 8)` point-four branch, the selected-side cyclic and
group-order divisors are 100 and 400. X1(27)'s corresponding divisors are 108
and 432. The schedules bind distinct generator/formula/trace-prior policies:

- X1(25): `28d7d1d7...cd47dbcc`
- X1(27): `9820d6c1...6ad2c22e`

Every retained run used prototype binary SHA-256
`b775e7e622d2242b1c39e134bda040b7767a4dea169ba3861f34129aa6a834da`
from isolated base commit `976924b3aa2148f62ceae11948824d8aed5a41bb`, the
same authenticated table set, 5.4 GB smooth cache, canonical verifier and
Python executable, maximum level 401, trace cap 16, exact Schoof fallback,
ten curve threads, and one SEA/smooth thread per curve. The configured
incomplete skip was never used, and no fallback level was needed.

Small-field brute-force tests sharply exercised divisors 100, 200, and 400;
the p125 test checks determinism and exact-order metadata. Search-pipeline and
targeted CLI tests verify that the X1(25) formula and trace-prior identities
change the schedule and that the signed modulus-400 prior reaches exact search
state. This prototype remains isolated because the performance gate failed.

## Measured gate

The retained order was X1(27) baseline, X1(25), a clean X1(25) repetition,
then X1(27) baseline. A second X1(25) attempt measured 259.65 seconds while it
overlapped the committed index-246 release replay; it is explicitly excluded
from every timing mean and decision. Its mathematical output still matches
the clean X1(25) projections exactly.

| run | family | wall | user | sum generation | sum SEA | sum smoothness | sum work | SEA levels | full counts |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| B1 | X1(27) | 198.29 s | 942.69 s | 45.661 s | 621.106 s | 232.909 s | 899.677 s | 542 | 6/10 |
| X1 | X1(25) | 222.57 s | 1237.53 s | 37.154 s | 947.627 s | 213.266 s | 1198.047 s | 619 | 7/10 |
| X3 | X1(25) | 221.13 s | 1242.94 s | 36.694 s | 942.564 s | 211.965 s | 1191.224 s | 619 | 7/10 |
| B2 | X1(27) | 198.09 s | 944.38 s | 45.076 s | 620.904 s | 231.318 s | 897.298 s | 542 | 6/10 |

X1(25) reduced generator work by 18.61%, confirming that its degree-five
root polynomial is cheaper in this bounded window. That saving was swamped by
its SEA work: 945.096 concurrent curve-seconds versus 621.005 for X1(27), or
1.52188x. It used 619 versus 542 SEA levels and 1.32961x total curve work.
Measured wall time was 11.94% higher.

Every retained run completed ten sound smoothness rejections, zero heuristic
rejections, and zero certificates. After removing timings, peak RSS, and
embedded mutable state, the two progress streams within each family are
byte-identical under canonical sorted-key serialization. Their checkpoints
are byte-identical as well. Thus signed priors, per-index terminal states,
SEA/exact/Atkin level counts, candidate counts, and exact traces replayed
deterministically.

## Opportunity boundary and decision

The same model used for earlier family decisions gives X1(25)/X1(27)
opportunity ratios of `0.994186` for conservative cyclic divisors 100/108 and
`0.993816` for group-order divisors 400/432. Combining the cyclic ratio with
measured wall throughput gives:

`0.994186 * (198.19 / 221.85) = 0.888157`

The group-divisor sensitivity is `0.887827`. Both are planning heuristics, not
observed certificate rates; all runs found zero certificates. They omit
curve/twist correlation, family trace bias, group-exponent restrictions,
representability, exact-order assembly, and canonical verification.

Do not promote X1(25). Its cheaper generator is real, but the complete p125
gate is slower and its guaranteed divisors are weaker. Keep X1(27) as the
production family and record X1(25) as a closed negative route unless a future
change specifically removes the observed SEA disadvantage.

Matching index labels do not pair the two family distributions, and ten
curves are a small sample with long SEA tails. The reverse-order repetitions
control timing reproducibility for the same deterministic curves; they do not
increase the independent sample size beyond ten per family.
