# 2026-08-02 p125 polynomial hot-path A/B

## Scope

This benchmark compares the current `main` polynomial implementation at
`27b6fee66535ee943cc65a13ced15cfd056673f2` with the candidate that:

- normalizes GMP coefficients in place;
- reuses monic reduction pivots directly;
- assembles Karatsuba low/high limbs by move rather than redundant additions;
- computes overlapping Karatsuba input sums with direct `mpz_add` calls.

It is a quotient-ring/Frobenius kernel benchmark. It is not an end-to-end
search throughput claim.

## Reproduction

GitHub Actions run `30767649530` built the baseline and candidate from the
same checkout and compiler environment (Ubuntu 24.04, GCC, GMP 6.3). For each
degree, the run order was baseline, candidate, candidate, baseline. Each
binary executed:

```sh
build/benchmark_p125_poly_trusted frobenius DEGREE 1
```

The complete mathematical projection was written to stdout and compared
byte-for-byte across all runs. Timing was written separately to stderr.

## Results

| Degree | Baseline mean | Candidate mean | Speedup | Projection SHA-256 |
|---:|---:|---:|---:|---|
| 194 | 2,393,483 us | 2,288,329 us | 1.046x | `441a733c086c464efdf6a27c413bdd26f2ae88ac23cb9cf8b64bbc132f134fa6` |
| 277 | 4,712,049 us | 4,464,011 us | 1.056x | `964211bd835a436a7fb5bfac20c59574fb39533e2cda19e5c61e99775c8e1d21` |

Raw measurements:

```text
baseline degree=194 sequence=1 timing.elapsed_us=2390576
candidate degree=194 sequence=1 timing.elapsed_us=2286866
candidate degree=194 sequence=2 timing.elapsed_us=2289792
baseline degree=194 sequence=2 timing.elapsed_us=2396390

baseline degree=277 sequence=1 timing.elapsed_us=4727373
candidate degree=277 sequence=1 timing.elapsed_us=4466230
candidate degree=277 sequence=2 timing.elapsed_us=4461792
baseline degree=277 sequence=2 timing.elapsed_us=4696724
```

## Correctness gates

The candidate also passed:

- the full portable native/reference suite;
- exact convolution and quotient-ring differential tests;
- specialized-square differential tests;
- the 416-bit general Elkies reference test;
- exact p125 projection comparison at both benchmark degrees;
- focused ASan and UBSan polynomial tests locally.

A Kronecker-substitution prototype was separately tested against the same
projection. It was exact but approximately 10% slower at degree 194, so it
was rejected and is not part of this change.
