# p125 specialized Frobenius-X-window audit

This bundle validates implementation commit
`69bf11543bb7fc8237e5c60a53fc00d62d039fbb` at
`nextprime(10^125) = 10^125 + 237` (416 bits).

The complete-root proof begins by computing `X^p mod f`. The generic
sliding-window planner charged every odd-power table entry as a quotient-ring
operation. For the actual base `X`, however, every `X^k` with
`k < degree(f)` is already its canonical polynomial representative. The new
path constructs those monomials directly and selects its window using only
the remaining quotient-ring execution chain.

At level 89, the generic width-5 plan uses 462 execution operations plus 16
table operations. The specialized width-6 plan uses 452 execution operations
and no table arithmetic, removing 26 quotient-ring squares or multiplies from
each initial Frobenius computation. The exponent, quotient modulus, root
split, and all downstream Elkies/Atkin proofs are unchanged.

## Isolated result

Both builds authenticated the same complete eight-level p125 cache and
evaluated only level 89 on the same four deterministic X1(27) curves. All 16
records were Atkin and agreed exactly on curve identity, trace prior,
projective order, residue set, and information content.

| run | library implementation | target evaluation | generation control |
| --- | --- | ---: | ---: |
| baseline A | `c10902a` | 2,505,271 us | 46,640,713 us |
| candidate A | `69bf115` | 1,520,798 us | 30,423,781 us |
| candidate B | `69bf115` | 2,768,039 us | 48,001,766 us |
| baseline B | `c10902a` | 1,992,101 us | 36,836,861 us |

Across this B/A/A/B bracket, the raw isolated target timer fell from 4,497,372
to 4,288,837 microseconds, a 4.6368% reduction (1.0486x). The candidate's
generation control was also 6.0520% faster. Together with the large monotonic
host variation inside the bracket, that favorable control drift prevents a
general, whole-cohort, or end-to-end speed claim. The deterministic removal of
26 quotient operations is the stronger evidence for the optimization.

## Production correctness replay

The committed candidate ran the actual four-curve p125 search with the exact
authenticated 22-level direct context, threshold-two pre-smooth policy,
one-gigabyte direct-cache residency budget, Weber continuation through level
401, and 5.4 GB smooth cache. The zero-curve identity probe and full run both
reproduced schedule digest `c542232b...acf14ad`.

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

## Validation and review boundary

The independent binary exponentiation reference covers every exponent from 0
through 1024 on the specialized `X` path and the real p125 exponent in a
degree-90 quotient. Generic-base, nonmonic, repeated-factor, constant-modulus,
and invalid-exponent behavior remains covered separately. Release and
ASan/UBSan factor, Atkin, CM-surface, and search-pipeline suites pass.

The review obligation is small: confirm that a monomial of degree below the
modulus needs no reduction, that the specialized planner admits only table
powers below that bound, and that detecting the reduced quotient element `X`
cannot change generic-base behavior.

This is a constant-factor optimization. It preserves but does not prove the
conditional outer `p^(1/8+o(1))` curve-search heuristic, establish the CM/SEA
crossover, or measure certificate yield.

## Audit

From the repository root, run:

```sh
python3 artifacts/local/p125-direct-frobenius-x-window-20260803/audit.py
```

The audit authenticates every retained file, resolves both source trees,
rederives the B/A/A/B measurements and operation counts, checks exact
semantics across all 16 Atkin records, validates the production replay against
the preceding checkpoint, and compares every exact trace with the independent
PARI record.
