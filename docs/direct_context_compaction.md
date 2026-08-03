# Compact classical direct-SEA contexts

## Purpose

Classical direct preparation must enumerate rich CM/isogeny objects to prove
each auxiliary-prime interpolation row. A warm curve does not use those
objects. It needs only:

1. the `ell+2` Lagrange basis rows for the admitted surface invariants; and
2. the `ell+2` monic neighbor-polynomial rows built from the complete Vélu
   codomain set.

The run-scoped context now computes those two square matrices immediately
after complete surface admission and releases every auxiliary curve, point,
kernel polynomial, codomain curve, and edge classification. Matrix entries are
canonical `uint64_t` residues because the producer already proves every
auxiliary prime over the full unsigned 64-bit range. Warm evaluation uses
exact `unsigned __int128` modular products and converts only the final two
`ell+2` residue vectors back to GMP for CRT.

This does not weaken the trust boundary. The full objects exist while their
class polynomial, trace-signed twist, `ell+1` kernels, horizontal-edge count,
neighbor degrees, monicity, and Lagrange partition of unity are checked. Only
the checked linear data survives. Existing table differentials and independent
Schoof validation cover both the value and X-derivative channels after
compaction.

## Space effect

Let `K` be the number of witnessed CRT primes for one level. Retaining
`ell+2` rows of `ell+1` isogenies, each with an order-`ell` kernel polynomial,
uses `O(K ell^3)` auxiliary-field coefficients. The compact context retains
exactly

```text
2 K (ell+2)^2
```

64-bit coefficients, or `O(K ell^2)` persistent field storage. Preparation
still has `O(ell^3)` transient data per active witness, but at most the bounded
preparation worker count is live; completed rich surfaces no longer accumulate
across all `K` witnesses.

`oneshotsea.search-summary.v1` reports both `matrix_coefficients` and
`matrix_payload_bytes`. The byte value is the exact coefficient payload and
deliberately excludes witness metadata, vector headers, allocator overhead,
and unrelated process memory.

## Reproduction

Build and run the checked 416-bit harness:

```sh
/usr/bin/make build/benchmark_p125_classical_direct
./build/benchmark_p125_classical_direct --threads 4 13
./build/benchmark_p125_classical_direct --threads 4 29
```

Every record times lazy preparation plus a cold curve, then a second curve
with a distinct `j`. It independently checks both results with Schoof after
the measured intervals and reports that validation time separately. Peak RSS
is process-wide. Run one level per process for isolated memory data.

The same source can be compiled against the pre-compaction library with
`-DONESHOTSEA_BENCHMARK_LEGACY_CONTEXT=1`; only the two unavailable matrix
telemetry fields become JSON null. This was used with baseline commit
`4f917fc` so curves, caps, thread count, and validation were identical.

## Local bracket

The following same-host measurements used four preparation workers. Times are
wall microseconds; RSS is bytes.

| Level | Build | Preparation | Cold | Distinct-j warm | Peak RSS | Matrix payload |
|---:|---|---:|---:|---:|---:|---:|
| 13 | compact, before | 477,693 | 530,325 | 18,739 | 5,160,960 | 162,000 |
| 13 | baseline, run 1 | 468,312 | 537,778 | 35,912 | 9,142,272 | n/a |
| 13 | baseline, run 2 | 445,150 | 515,113 | 36,132 | 8,978,432 | n/a |
| 13 | compact, after | 453,744 | 506,890 | 18,768 | 5,308,416 | 162,000 |
| 29 | compact, before | 20,227,559 | 20,306,219 | 84,143 | 13,205,504 | 1,168,576 |
| 29 | baseline | 22,210,538 | 22,455,093 | 251,353 | 60,620,800 | n/a |
| 29 | compact, after | 26,460,253 | 26,543,931 | 87,072 | 12,779,520 | 1,168,576 |

All level-29 cold residues were `23 mod 29`; the distinct-`j` warm residues
were `12 mod 29`. Independent Schoof returned the same values. Level 13
included both exact and Atkin cases; the Atkin set contained Schoof's residue.

The robust conclusions are:

- isolated level-29 RSS fell from 60.6 MB to 12.8--13.2 MB, about 79%;
- level-29 warm evaluation fell from 251 ms to 84--87 ms;
- median level-13 RSS fell 42% and warm evaluation 48%; and
- cold level-29 measurements varied too much with sustained thermal load to
  support a speedup or regression claim.

The seven raw bracket records, the three same-binary serial/four-worker
brackets at levels 7 and 11, clean-clone identities, commands, and independent
aggregation checks are retained in the
[audited evidence bundle](../artifacts/local/p125-classical-direct-compact-20260803/result.json).

This changes a polynomial memory factor and materially improves multi-level
feasibility, but it does not change the conditional outer
`p^(1/8+o(1))` curve-search exponent. The compact matrices can now be
atomically persisted and reused with an external SHA-256 trust anchor; see
[the authenticated direct-context cache](direct_context_cache.md). That
removes repeated process-start preparation but likewise does not change the
outer exponent.

## Current direct-preparation hot path

The table above is the retained historical compaction bracket. Subsequent
work removed a separate preparation bottleneck without changing its context
format:

1. find two independent exact order-`ell` points instead of coupon-collecting
   all `ell+1` kernels;
2. enumerate `<P>` and `<Q+kP>` as the complete projective line;
3. accumulate all Vélu half-system sums with one batch inversion per scalar;
4. run the quadratic point loop in a proved 64-bit Montgomery field;
5. construct Lagrange and neighbor rows directly as `uint64_t` polynomials;
   and
6. buffer authenticated context reads and writes instead of issuing one
   syscall per coefficient.

Retained four-worker same-host measurements for the current build are:

| Level | Preparation | Context bytes | Context SHA-256 |
|---:|---:|---:|:---|
| 29 | 326,099 us | 1,170,564 | `8e1f56edac173796c3c07f95d5fce2ec66fd9000d4faf1d8035f61b746d0e6c4` |
| 101 | 5,080,798 us | 35,651,444 | `9bd15d4f0b5af08a95348d849ef332f75012bc13a61f98b92be4e2d79847543b` |
| 157 | 28,396,348 us | 124,996,844 | `b6961ff01e2b43dd26a1d11a15ae9828ba144198f1e8a5d4881bcdfaa56e1dc6` |

Each digest exactly matches a context produced before the corresponding
arithmetic rewrite. At level 29, the two target curves also retain exact SEA
residues `23` and `12`, independently reproduced by Schoof. Loading the
authenticated level-29 context took 33,911 us. The retained same-binary
pre-basis level-29 preparation was 19,967,030 us, while a retained
post-compaction/pre-hot-path level-101 preparation was 33,818,533 us.

The level-157 profile now attributes most remaining time to complete
ring-class-polynomial root recovery, class-polynomial tower arithmetic, and
the still-required authenticated surface enumeration. These measurements
support the implemented polynomial-factor improvements; they do not prove a
full p125 schedule, the `p^(1/8+o(1))` yield heuristic, or an empirical
CM/SEA crossover.

## Current direct-evaluation hot path

Once a compact context is resident, specialization evaluates two Lagrange
matrix-vector products and two neighbor-matrix products over a 64-bit
auxiliary field. The current implementation precomputes the derivative powers
and accumulates products in `unsigned __int128` batches. The batch length is
the exact quotient of the largest 128-bit value by `(q-1)^2`, so every batch
is reduced before overflow is possible; no machine wraparound is treated as
field arithmetic.

An isolated A/B/A over four fixed p125 curves and levels 61 through 97 retained
identical exact/Atkin semantics in all 32 records. Summed evaluation time was
38,214,216 us / 31,727,943 us / 40,633,241 us, a 19.52069424% candidate
reduction against the bracketing-baseline mean. A real four-curve search replay
also preserved the prior statuses, trace counts, direct/Weber work counts, and
independently checked traces. The replay timing was thermally noisy and is not
used as performance evidence. See the
[batched-interpolation audit](../artifacts/local/p125-direct-batched-interpolation-20260803/README.md).

This is a constant-factor evaluation improvement. It does not alter the
context producer's current finite auxiliary-prime bounds or the conditional
outer search exponent.
