# p125 pre-smooth direct-tail promotion

This artifact validates implementation commit
`6601389fb87866c68f435974a4056e24c0261935` at the 416-bit target
`nextprime(10^125) = 10^125 + 237`.

The earlier deferred-tail policy waited until every cap-16 trace had passed
exact smoothness screening before evaluating direct levels 89/97 at cap one.
This checkpoint adds an explicit, checkpoint-bound threshold: if the complete
cap-16 set contains at least two traces, evaluate the authenticated suffix
first. A singleton submits only its curve and twist orders to smoothness. If
the suffix remains multiple, the refined certified set is screened normally.

## Paired cohort result

Both runs used the same binary, four deterministic X1(27) curves, authenticated
22-level direct cache, Weber tables, smooth cache, construction caps, one SEA
thread, and 1 GB context-residency budget. Only
`--classical-direct-pre-smooth-tail-min-traces` changed from 0 to 2.

| Measurement | threshold 0 | threshold 2 |
| --- | ---: | ---: |
| Initial cap-16 trace counts | 1, 13, 4, 5 | 1, 13, 4, 5 |
| Trace counts entering smoothness | 1, 13, 4, 5 | 1, 1, 1, 1 |
| Curve/twist orders scanned | 46 | 8 |
| Direct-level evaluations | 80 | 85 |
| Summed smoothness time | 70.458 s | 38.058 s |
| Summed SEA time | 310.318 s | 313.701 s |
| Summed per-curve total | 440.663 s | 409.084 s |

The deterministic work result is the important claim: five cached suffix
evaluations replaced 38 smooth-cache order scans. The measured smoothness time
fell 45.98%, while the summed per-curve total fell 7.17%.

The timing is one sequential, non-randomized A/B, with the promoted run first
and the baseline second. It is useful directional evidence, not a general
speedup estimate or confidence interval.

## Correctness checks

All eight curve records ended in `sound_smoothness_reject`. Both runs began
with the same trace counts. The promoted run resolved the three multi-trace
sets to exact traces before smoothness:

| index | prefix traces | direct suffix evaluations | exact trace |
| ---: | ---: | ---: | ---: |
| 2,000,002 | 13 | 2 | 312744557074493258005540218670034986285435355679693042023392238 |
| 2,000,003 | 4 | 2 | -252845884365417830567895303231394093497235790636298489485509474 |
| 2,000,004 | 5 | 1 | 432966650303160993124127306120296021107647349914430129038843294 |

Each equals the independently retained PARI/GP 2.17.4 trace for the same
deterministic curve. Index 2,000,004 needed only level 89 because the retained
Weber state already owned modulus 97. The single-trace first curve did not
evaluate the suffix.

## Interpretation

This policy is worth deploying when a cached suffix is cheaper than scanning
every order represented by a multi-trace cap-N set. The threshold is explicit
because that tradeoff depends on cache residency, cohort size, smooth-cache
layout, and the measured level schedule. Zero preserves the conservative
post-smooth behavior.

The change improves a policy constant and rejected-curve work. It does not
change the proposed `p^(1/8+o(1))` outer search exponent, establish the
SEA-versus-CM crossover, measure certificate yield, or find a p125 certificate.

## Audit

The compact raw records retain the terminal fields used in the comparison;
`result.json` binds the exact implementation tree, semantic schedule hashes,
authenticated input digests, and aggregate measurements. Run:

```sh
python3 artifacts/local/p125-pre-smooth-direct-tail-20260803/audit.py
```

The audit authenticates the artifact, resolves the implementation commit,
recomputes all counts and timings from the retained records, and compares the
promoted exact traces with the independent PARI record.
