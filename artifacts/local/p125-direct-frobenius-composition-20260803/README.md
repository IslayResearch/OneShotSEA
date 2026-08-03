# p125 prepared Frobenius-composition audit

This bundle validates implementation commit
`c10902aa411e3b6105755c1e1b8859cc3462a70c` at
`nextprime(10^125) = 10^125 + 237` (416 bits).

The Atkin factor-degree proof repeatedly composes different outer polynomials
with the same Frobenius maps. The generic modular-composition call rebuilt the
fixed inner polynomial's baby-step table every time. The implementation now
prepares that table once for each Frobenius power and reuses it throughout the
factor proof.

The plan owns its quotient-ring context and inner powers, rejects a mismatched
field or an outer polynomial beyond its declared bound, and produces the same
polynomial as the generic composition path. It changes neither the complete
root proof nor the factor-degree, projective-order, trace-CRT, or early-abort
logic.

## Isolated result

The benchmark authenticated the same complete eight-level p125 cache in both
builds and evaluated only level 89 on the same four deterministic X1(27)
curves. All 16 records were Atkin and agreed exactly on curve identity, trace
prior, projective order, residue set, and information content.

| run | library implementation | target evaluation | generation control |
| --- | --- | ---: | ---: |
| baseline A | `6cd7ca9` | 3,531,726 us | 58,175,750 us |
| candidate A | `c10902a` | 2,915,432 us | 52,684,524 us |
| candidate B | `c10902a` | 2,002,096 us | 35,360,070 us |
| baseline B | `6cd7ca9` | 1,658,417 us | 27,213,499 us |

Across this B/A/A/B bracket, the raw isolated target timer fell from 5,190,143
to 4,917,528 microseconds, a 5.2526% reduction (1.0554x). The candidate's
generation control was 3.1097% slower, so favorable control drift does not
explain the direction of the target result. Large monotonic host drift across
the bracket still makes this an isolated directional result, not a general or
end-to-end speed claim.

For these four deterministic factor proofs, the known composition schedule
reduces fixed-inner baby-table modular multiplications from 700 to 280, saving
420. Giant steps and the initial `X^p mod f` exponentiation are unchanged.

## Production correctness replay

The candidate also ran the actual four-curve p125 search with the authenticated
22-level selected prefix/suffix context, threshold-two pre-smooth policy,
one-gigabyte direct-cache residency budget, Weber continuation through level
401, and the authenticated smooth cache. It reproduced schedule digest
`c542232b...acf14ad` and the prior proof semantics:

| index | direct levels | direct Elkies / Atkin | Weber levels | exact trace |
| ---: | ---: | ---: | ---: | ---: |
| 2,000,001 | 20 | 12 / 8 | 33 | -498621923547174620050105080065695461058932825132695425058035790 |
| 2,000,002 | 22 | 11 / 11 | 33 | 312744557074493258005540218670034986285435355679693042023392238 |
| 2,000,003 | 22 | 11 / 11 | 35 | -252845884365417830567895303231394093497235790636298489485509474 |
| 2,000,004 | 21 | 10 / 11 | 52 | 432966650303160993124127306120296021107647349914430129038843294 |

All four traces equal the independently retained PARI/GP 2.17.4 point counts.
The run retained 85 direct evaluations, submitted eight curve/twist orders,
and reached the same four sound smoothness rejections. Production timing is
correctness evidence only.

## Review value and boundary

The review surface is small and reusable: the prepared plan's lifetime,
coefficient bound, field identity checks, and equivalence to generic modular
composition. Unit tests cover repeated, zero, constant, temporary-context,
high-degree p125, invalid-bound, and mismatched-field cases; release and
ASan/UBSan suites pass; the real 416-bit replay preserves all proof semantics.

This is a constant-factor improvement. It does not change or prove the
conditional outer `p^(1/8+o(1))` curve-search heuristic, establish the CM/SEA
crossover, or measure certificate yield. The next dominant target remains the
initial quotient-ring Frobenius exponentiation.

## Audit

From the repository root, run:

```sh
python3 artifacts/local/p125-direct-frobenius-composition-20260803/audit.py
```

The audit authenticates every retained file, resolves both source trees,
rederives the B/A/A/B comparison, checks all 16 Atkin records for exact
semantic equality, validates the production replay against the prior
checkpoint, and compares every exact trace with the independent PARI record.
