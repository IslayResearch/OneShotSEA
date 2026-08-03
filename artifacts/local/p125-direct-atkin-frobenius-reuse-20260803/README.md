# p125 direct Atkin Frobenius-reuse audit

This bundle validates implementation commit
`ef42ed25ebec9887a65867f8a776941f585329af` at
`nextprime(10^125) = 10^125 + 237` (416 bits).

At a direct Atkin level, the Elkies attempt already computes
`X^p mod Phi_ell(j,Y)` to prove that the specialization has no rational roots.
The factor-degree certificate previously computed that same first Frobenius
image again. The implementation now retains complete, polynomial-bound root
evidence and passes its validated Frobenius image into the Atkin proof.

The evidence is opaque to callers, owns the immutable specialization, and is
accepted only when its exact polynomial matches the Atkin or Elkies consumer.
The factor-degree algorithm, projective-order proof, residue enumeration,
trace CRT, early-abort policy, cache format, and schedule are unchanged.

## Isolated result

The benchmark authenticated the complete eight-level p125 cache but, using an
identical benchmark-only driver in both builds, evaluated only level 89 on the
same four deterministic X1(27) curves. All 16 level records were Atkin and
agreed exactly on curve identity, trace prior, projective order, residue set,
and information content.

| run | library implementation | target evaluation | curve generation control |
| --- | --- | ---: | ---: |
| baseline A | `302e4be` | 5,473,230 us | 57,946,481 us |
| candidate A | `ef42ed2` | 2,843,110 us | 45,760,647 us |
| candidate B | `ef42ed2` | 2,519,907 us | 39,453,874 us |
| baseline B | `302e4be` | 5,369,323 us | 54,705,959 us |

Summed across the B/A/A/B bracket, the raw isolated target timer fell from
10,842,553 to 5,363,017 microseconds, a 50.5373% reduction (2.0217x). The
generation control was also 24.3563% faster in the candidate pair, so host
state favored the candidate. The target result is consistent with removing a
known full quotient-ring exponentiation, but it is deliberately **not** used
as a general, whole-cohort, or end-to-end speedup estimate.

## Production correctness replay

The candidate then ran the actual four-curve p125 search with the authenticated
22-level selected prefix/suffix context, threshold-two pre-smooth policy,
one-gigabyte residency budget, Weber continuation through level 401, and the
5.4 GB authenticated smooth cache. It reproduced the prior schedule digest
`c542232b...acf14ad` and all prior proof semantics:

| index | direct levels | direct Elkies / Atkin | Weber levels | exact trace |
| ---: | ---: | ---: | ---: | ---: |
| 2,000,001 | 20 | 12 / 8 | 33 | -498621923547174620050105080065695461058932825132695425058035790 |
| 2,000,002 | 22 | 11 / 11 | 33 | 312744557074493258005540218670034986285435355679693042023392238 |
| 2,000,003 | 22 | 11 / 11 | 35 | -252845884365417830567895303231394093497235790636298489485509474 |
| 2,000,004 | 21 | 10 / 11 | 52 | 432966650303160993124127306120296021107647349914430129038843294 |

All four exact traces equal the independently retained PARI/GP 2.17.4 point
counts. All four curves reached the same sound smoothness rejection, with 85
direct evaluations and eight submitted curve/twist orders in total. Production
timing is retained only as a correctness run and is not part of the speed
claim.

## Why this is worth Drew's review

This change removes deterministic duplicate work from the dominant
post-interpolation direct-Atkin path without altering the mathematics of the
certificate. The review surface is bounded and security-relevant: Drew can
check that the root evidence is complete and unforgeable, that its lifetime is
safe, that exact polynomial equality gates reuse, and that `X^p` is valid for
the same quotient used by the uniform-factor-degree proof. The differential
tests cover cached versus independent factor-degree paths and mismatched
evidence; release, ASan/UBSan, and real 416-bit replay all pass.

That makes review useful now: approval would justify carrying the optimization
into a larger p125/p130 yield experiment. It does not ask Drew to accept a
benchmark as a proof of certificate yield or of the CM/SEA crossover.

## Asymptotic boundary and next optimizations

This is a per-level constant-factor improvement. It preserves the intended
polynomial-in-`log p` direct-SEA work and the conditional outer
`p^(1/8+o(1))` curve-search heuristic; it neither proves nor changes that
exponent. The current 64-bit auxiliary field, fixed selector, and finite
measured schedule still prevent an unqualified implementation-wide
asymptotic claim.

The next promising work is to retain more validated Frobenius composition
powers when several factor-degree checks share a specialization, profile and
replace the remaining quotient-ring composition bottleneck, and use a larger
curve-independent schedule cohort to optimize information per cost. Beyond
constants, removing the 64-bit auxiliary-prime ceiling and aligning prime
selection with the theorem's randomized assumptions are the important steps
toward a literal unbounded scaling claim.

## Audit

From the repository root, run:

```sh
python3 artifacts/local/p125-direct-atkin-frobenius-reuse-20260803/audit.py
```

The audit authenticates every retained file, resolves both source trees,
rederives the B/A/A/B measurements, checks exact semantics across all 16 Atkin
records, validates the production replay against the prior checkpoint, and
compares every exact trace with the independent PARI record.
