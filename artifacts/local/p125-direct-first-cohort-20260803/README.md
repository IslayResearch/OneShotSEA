# p125 retained direct-first cohort and schedule selection

This bundle extends the fixed-curve direct-first result to four additional
deterministic X1(27) curves at `nextprime(10^125)`. It answers three distinct
questions:

1. Does retaining direct exact/Atkin state help consistently at 416 bits?
2. Can it recover a curve on which the Weber catalog fails to fit the measured
   trace cap?
3. Do independently counted traces remain inside the native retained sets?

The answer is yes to all three on this bounded cohort.

## Production-policy coverage result

At trace cap 16, Weber-only soundly rejected indices 2,000,001 through
2,000,003, then exhausted all 77 usable levels at index 2,000,004 with 64
effective trace candidates. It failed closed as `sea_level_limit`; the worker
cursor did not advance past that curve.

The low 15-level direct-first schedule completed all four curves. It evaluated
38, 39, 42, and 58 Weber levels and presented sets of 6, 1, 1, and 2 traces to
exact smoothness screening. All four ended in `sound_smoothness_reject`.

A measured 20-level schedule added levels 67, 71, 79, 61, and 73. It required
only 33, 33, 35, and 52 Weber levels and produced retained sets of 1, 13, 4,
and 5 traces. Again, all four rejections were sound. Relative to the low
schedule it saved 24 Weber levels while adding 20 direct-level evaluations
across the cohort.

The difference between the candidate counts is not a correctness difference:
both schedules stopped when they first fit the same cap. Exact smoothness
screening rejected every member of each complete bounded set.

## Timing evidence and its limit

At trace cap 64, the matched four-curve Weber/direct-first comparison was run
before the schedule study. Direct-first reduced aggregate SEA time from
131.095410 s to 121.318986 s, a 1.08058x speedup, and saved 69 Weber levels.
The larger total-time difference is not claimed because the smooth cache was
warmer in the second arm.

Sustained profiling later changed the machine's thermal state, so the raw
cap-16 cohort seconds are not treated as an A/B. A same-curve selected/low/
selected bracket has matched deterministic generation times within 1.1%:

| schedule | generation | direct | Weber continuation | SEA |
| --- | ---: | ---: | ---: | ---: |
| selected-20 A | 2.428114 s | 5.145512 s | 31.619920 s | 36.765432 s |
| low-15 | 2.407764 s | 2.582720 s | 37.639762 s | 40.222482 s |
| selected-20 B | 2.433365 s | 6.343829 s | 32.843500 s | 39.187329 s |

The mean selected SEA time is 37.976381 s, 1.05915x faster than the bracketed
low run. This is a one-curve thermal control, not a throughput confidence
interval. The stronger result is the deterministic stopping/coverage change.

## Schedule selection

The retained 16-curve direct profile covers eight measured levels from 61
through 97. Combining it with the retained low-level profile shows that 67,
71, 79, 61, and 73 all deliver more information per warm evaluation cost than
level 59; 83, 89, and 97 do not. The chosen five contribute about 21.80
expected trace bits for 2.78 s/curve in the retained profile.

The selected ordered schedule is:

```text
7,5,11,13,19,17,23,29,31,37,41,43,47,53,67,71,79,61,73,59
```

Its authenticated cache is 94,601,556 bytes with SHA-256
`d9848275c04d77c5a40f96eb06f113100ebc7a1b3ac0bc6c15d207677be41a53`.
The cache is an external benchmark input and is not duplicated here.

## Independent validation

PARI/GP 2.17.4 independently counted each exact generated short-Weierstrass
model. Its signed traces agree with all native singleton traces.

A separate native enumerator replayed the authenticated selected-20 direct and
Weber constraints. Each PARI trace is explicitly present in its complete
retained set, including index 2,000,004:

```text
index 2000001: 1 candidate, PARI trace present
index 2000002: 13 candidates, PARI trace present
index 2000003: 4 candidates, PARI trace present
index 2000004: 5 candidates, PARI trace present
```

PARI is offline validation only. The producer and production search do not
invoke it. The retained native enumerator shares the implementation under
test, so it validates cache replay, retained-state composition, and candidate
inclusion, while PARI supplies the independent exact point count.

Run `python3 audit.py` to authenticate the retained files and rederive every
aggregate above. See `commands.sh` for cache generation and replay commands.

## Scope

This is a four-curve coverage/performance checkpoint. It does not measure
certificate yield, establish a CM crossover, find a one-shot certificate, or
prove the `p^(1/8+o(1))` heuristic. It does show that custom direct Atkin data
can turn a real Weber catalog limit into a complete, soundly screened p125
result.
