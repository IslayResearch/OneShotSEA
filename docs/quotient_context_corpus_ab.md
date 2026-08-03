# Prepared quotient-context p125 corpus A/B, 2026-08-03

## Question

`PolyModContext` prepares reciprocal reduction once for a kernel quotient ring
and reuses it across Frobenius powers and Jacobian eigenvalue arithmetic.  The
single-level level-409 ablation showed a strong eigenvalue benefit, but that
fixture did not establish the realized effect across ordinary search curves.
This experiment asks two narrower questions:

1. Is the optimization neutral on curves that only reach moderate SEA levels?
2. Does the benefit appear consistently once the same curves require larger
   kernel quotient degrees?

The comparison changes only
`ONESHOTSEA_QUOTIENT_CONTEXT_REUSE`.  Complete mathematical projections are
compared byte-for-byte before any timing result is accepted.

## Exact source and host

- arithmetic base: `fecf5a5d0824f114137b1272f9aa73d2c86d35df`;
- target: `nextprime(10^125)`, 416 bits;
- deterministic family: X1(27), point-order-four admission enabled;
- indices: `1000030`, `1000031`, and `1000032`;
- trace cap: 16;
- SEA threads: 1;
- run order for every index: context off, context on, context on, context off;
- host: Intel Xeon Platinum 8370C, Linux x86-64, five available CPUs;
- timed affinity: CPU 0;
- compiler: GCC 14.2.0, glibc 2.41.

The context-off binary has SHA-256
`6c683fe5b298b56204e7d089113022de35ba57494f918633015b722154e582ba`;
the context-on binary has SHA-256
`34404e3fcd52c8d1e20d38179cac85fd0dd8965ea54a3b261696d06e546be4fe`.
Both were built from the same source snapshot.  The benchmark harness only adds
an optional deterministic global-index argument; it does not alter arithmetic.

## Level-193 control

All three curves processed every authenticated level through 193 and remained
above the 16-trace cap.  Across six runs per variant, aggregate means were:

| Stage | Context off | Context on | Off/on ratio |
|---|---:|---:|---:|
| SEA | 30.197250 s | 30.200722 s | 0.99989x |
| Complete invocation | 34.260610 s | 34.284229 s | 0.99931x |
| Modular roots | 15.437734 s | 15.389453 s | 1.00314x |
| BMSS | 5.523192 s | 5.562445 s | 0.99294x |
| Eigenvalue recovery | 9.045372 s | 9.061160 s | 0.99826x |

This is a clean neutral result.  It rules out treating quotient-context reuse
as a uniform speedup at moderate levels.  Setup and control-stage noise are as
large as any measured benefit in this range.

## Level-277 corpus

The same curves were then allowed to continue through level 277.  Indices
`1000030` and `1000031` reached complete trace sets at levels 271 and 257;
index `1000032` reached level 277 and remained incomplete.  All twelve
projections again matched byte-for-byte.

| Stage | Context off | Context on | Speedup |
|---|---:|---:|---:|
| SEA | 72.569333 s | 71.241490 s | **1.01864x** |
| Complete invocation | 76.642707 s | 75.351885 s | **1.01713x** |
| Modular roots | 32.356015 s | 32.537772 s | 0.99441x |
| BMSS | 14.894739 s | 14.901901 s | 0.99952x |
| Eigenvalue recovery | 25.016835 s | 23.500073 s | **1.06454x** |

Per-curve eigenvalue speedups were 1.08992x, 1.02909x, and 1.06293x.  The
modular-root and BMSS controls were neutral to slightly negative, while all
three curves improved in the stage that actually uses the persistent kernel
quotient context.  This stage separation is stronger evidence than the 1.7%
whole-invocation headline by itself.

## Interpretation

The A/B establishes a level-dependent crossover:

- through level 193, persistent context reuse is neutral;
- on the same deterministic corpus through level 277, it saves 6.45% in
  eigenvalue recovery and 1.86% in SEA overall;
- this is directionally consistent with the previously retained level-409
  single-level result, where eigenvalue recovery improved 1.24286x.

Across the two gates, all 24 complete mathematical projections matched their
paired context-off and context-on runs byte-for-byte.

Production should keep context reuse enabled.  The result does **not** justify
claiming a 1.86% full-search throughput gain: it excludes the 5.4 GB exact
smoothness scan, checkpoint publication, candidate assembly, and certificate
verification.  The next honest gate remains a bounded production-path corpus
using the authenticated smooth cache.

## Reproduction

The checked-in driver builds both variants, performs B/A/A/B ordering, saves
raw projections and timings, and fails on any byte difference:

```sh
python3 tools/run_p125_context_corpus_ab.py \
  --indices 1000030,1000031,1000032 \
  --max-level 193 \
  --cpu 0 \
  --output-dir work/context-ab-193

python3 tools/run_p125_context_corpus_ab.py \
  --indices 1000030,1000031,1000032 \
  --max-level 277 \
  --cpu 0 \
  --output-dir work/context-ab-277
```

Compact timing vectors, projection hashes, binary identities, source hashes,
and limitations are retained in
[`artifacts/local/p125-context-corpus-20260803/result.json`](../artifacts/local/p125-context-corpus-20260803/result.json).
