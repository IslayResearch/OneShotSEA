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
| 13 | baseline, run 1 | 463,828 | 536,111 | 37,596 | 9,125,888 | n/a |
| 13 | compact, run 1 | 521,105 | 576,068 | 19,627 | 5,095,424 | 162,000 |
| 13 | baseline, run 2 | 460,830 | 532,660 | 37,611 | 9,338,880 | n/a |
| 13 | compact, earlier reverse run | 453,961 | 508,063 | 19,287 | 5,210,112 | 162,000 |
| 29 | compact, before baseline | 22,183,812 | 22,264,500 | 85,932 | 12,517,376 | 1,168,576 |
| 29 | baseline | 20,439,041 | 20,684,859 | 255,865 | 60,424,192 | n/a |
| 29 | compact, after baseline | 30,047,767 | 30,163,397 | 127,656 | 13,008,896 | 1,168,576 |

All level-29 cold residues were `23 mod 29`; the distinct-`j` warm residues
were `12 mod 29`. Independent Schoof returned the same values. Level 13
included both exact and Atkin cases; the Atkin set contained Schoof's residue.

The robust conclusions are:

- isolated level-29 RSS fell from 60.4 MB to 12.5--13.0 MB, about 79%;
- level-29 warm evaluation fell from 256 ms to 86--128 ms;
- level-13 RSS fell about 44% and warm evaluation about 48%; and
- cold level-29 measurements varied too much with sustained thermal load to
  support a speedup or regression claim.

This changes a polynomial memory factor and materially improves multi-level
feasibility, but it does not change the conditional outer
`p^(1/8+o(1))` curve-search exponent. Persisting authenticated compact matrices
across process restarts remains separate work.
