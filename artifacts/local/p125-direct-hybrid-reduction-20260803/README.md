# p125 hybrid quotient-reduction audit

This bundle validates arithmetic commit
`a83a6a6e22384ebcad6c3180eab1336460c7bf9e` at
`nextprime(10^125) = 10^125 + 237` (416 bits). Benchmark-only commits
`e915e36` and `a8a045e` separate the real `X^p` workload from its dense
control and add an interleaved below-threshold control.

## Change

The initial direct-SEA Frobenius computation at level 89 works in a dense
degree-90 quotient. The existing long reducer cancels an 89-coefficient
quotient one coefficient at a time, requiring 89 x 90 = 8,010 scalar GMP
`submul` calls for a full reduction. The existing reciprocal reducer replaces
that loop with packed polynomial products, but its all-or-nothing form was
measured as neutral at degree 90 and remains reserved for degree 96 and above.

The retained hybrid computes only the highest 48 quotient coefficients with a
prepared reciprocal prefix. Two exact packed convolutions cancel that block;
the proven long reducer finishes the remaining 41 coefficients. A full dense
reduction therefore retains 41 x 90 = 3,690 scalar `submul` calls, replacing
4,320 of them with the two packed products.

The measured activation band is deliberately narrow: degrees 80 through 90.
Degree 95 regressed in the neighbor experiment, so it stays on the long
reducer; degree 96 continues to use the previously validated full reciprocal
path. Disabling packed Kronecker convolution also disables the hybrid.

## Controlled degree-90 result

Absolute timings on the Apple host varied substantially with scheduling and
core frequency. The retained benchmark therefore interleaves the degree-90
target with degree 79, which remains below the hybrid threshold, reversing
their order on alternating repetitions. Each B/A/A/B phase performs 20 target
and 20 control exponentiations in one process.

| aggregate | pre-hybrid baseline | hybrid candidate | ratio reduction |
| --- | ---: | ---: | ---: |
| process CPU, degree 90 / degree 79 | 1.245430 | 1.196244 | 3.9493% |
| wall time, degree 90 / degree 79 | 1.253906 | 1.204869 | 3.9107% |

All four target/control projections have the same SHA-256,
`abca412b...081f1263`. The aggregated control CPU times differ by only 0.052%,
and the independent CPU and wall ratios agree on the direction and magnitude.

## Real level-89 profile

Both profilers authenticated the same complete eight-level cache and evaluated
only level 89 on four fixed X1(27) curves. All 16 records remained Atkin and
agreed exactly on curve identity, trace prior, projective order, residue set,
and information content.

| run | implementation | evaluation | generation control |
| --- | --- | ---: | ---: |
| baseline A | `0802f77` | 2,051,555 us | 38,209,243 us |
| candidate A | `a83a6a6` | 1,356,919 us | 25,457,031 us |
| candidate B | `a83a6a6` | 1,851,449 us | 34,192,935 us |
| baseline B | `0802f77` | 1,842,398 us | 32,867,512 us |

The raw target subtotal fell 17.61%, while the unrelated curve-generation
control also favored the candidate by 16.08%. This bracket is correctness and
directional evidence only; the normalized interleaved benchmark above is the
supported isolated performance result. No whole-cohort or end-to-end speedup
is inferred.

## Correctness gates

Independent long-reduction differentials cover degrees
79/80/81/89/90/91/95/96/97 and extend through 401. Full p125 exponents are
checked at degrees 80, 90, 96, and 194. Release tests passed for core
arithmetic, specialized squaring, factorization, Atkin classification, direct
modular-polynomial construction, CM surfaces, checkpoints, the search
pipeline, CLI behavior, and retained benchmark parsing. Core, square, factor,
and Atkin suites also passed under ASan and UBSan.

The exact committed production binary then replayed the authenticated
four-curve p125 search with the 20-level prefix, deferred 89/97 suffix,
pre-smooth threshold two, Weber continuation through 401, and the 5.0 GiB
smooth cache:

| index | direct levels | direct Elkies / Atkin | Weber levels | exact trace |
| ---: | ---: | ---: | ---: | ---: |
| 2,000,001 | 20 | 12 / 8 | 33 | -498621923547174620050105080065695461058932825132695425058035790 |
| 2,000,002 | 22 | 11 / 11 | 33 | 312744557074493258005540218670034986285435355679693042023392238 |
| 2,000,003 | 22 | 11 / 11 | 35 | -252845884365417830567895303231394093497235790636298489485509474 |
| 2,000,004 | 21 | 10 / 11 | 52 | 432966650303160993124127306120296021107647349914430129038843294 |

Every trace equals the independently retained PARI/GP 2.17.4 count. The replay
preserved all prior proof semantics, submitted eight curve/twist orders, and
reached the same four sound smoothness rejections under schedule digest
`c542232b...acf14ad`.

## Review and claim boundary

The review obligation is bounded: verify that the first 48 coefficients of
`reverse(M)^-1` determine exactly the highest 48 quotient coefficients, that
lower quotient coefficients cannot affect the cleared high block, and that
finishing the remaining polynomial with the long reducer preserves the field
remainder. The threshold conditions must also leave the degree-96 full
reciprocal path and compile-time Kronecker ablations unchanged.

This is a constant-factor quotient-arithmetic optimization. It does not change
or prove the conditional `p^(1/8+o(1))` outer curve-search heuristic, establish
the finite SEA/CM crossover, measure certificate yield, or find a p125
certificate.

## Audit

From the repository root, run:

```sh
python3 artifacts/local/p125-direct-hybrid-reduction-20260803/audit.py
```

The audit authenticates the exact file set, resolves implementation and
baseline trees, rederives both A/B comparisons and operation counts, checks all
16 Atkin records, compares the production replay with the preceding checkpoint,
and matches all four exact traces to the independent PARI records.
