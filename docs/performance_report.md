# Performance and ablation report

This report consolidates the retained measurements relevant to deliverable 5
of `revised_prompt.md`.  It distinguishes controlled A/B measurements from
operation counts, cross-run indications, and work that has not yet been
measured.  Unless stated otherwise, local results used an Apple M4 (10 cores,
16 GiB) and the `p125` field.  A speedup is reported only when both sides of an
appropriate comparison exist.

## Required-ablation status

| Required dimension | Implemented path | Best retained evidence | Status |
|---|---|---|---|
| Early abort | Complete bounded trace enumeration followed by exact full-`n^4` smooth screening of both signs | Raising the screen from the default path to 195 traces would have made the measured index-0 run 27.8 s slower; eight retained production curves were soundly rejected from 102 trace candidates (204 curve/twist orders) without completing a point count | Implemented and policy-calibrated; no same-build off/on wall-time speedup yet |
| Batching | Batched exact accumulation in polynomial reduction and batched remainder-tree smooth extraction | Controlled reducer A/B: 2.07x synthetic median, 2.32x invocation wall and 2.59x complete curve work on a deterministic `p125` replay | Measured for two hot-path batching mechanisms; no cross-curve SEA batch A/B |
| Curve/twist sharing | Derive `p+1-t` and `p+1+t` from one SEA trace state and submit both to one smooth batch | Every trace supplies two order candidates; the first eight retained curves produced 102 trace candidates and 204 order screens from eight SEA executions | Exact work-count reduction; no wall-time ablation |
| Prime scheduling | Available Weber levels are processed in increasing prime order | No alternate schedule has been run.  The current loop is increasing-order, not a measured information-per-cost policy | Missing required ablation |
| Specialized modular-polynomial path | Authenticated Weber-f tables, BMSS kernel recovery, and verified 24th-root orbit transport | Controlled orbit off/on A/B: 2.366x median wall and 3.445x modular-root speedup through level 193; at common levels 5/7, classical-j is honestly 12.449x/10.982x faster because Weber source-lift discovery dominates | Specialized hot-path ablation and tractable classical boundary measured; production-scale classical-j is unavailable |

The report is therefore evidence for substantial performance progress, not a
claim that deliverable 5 is fully closed.  Prime scheduling remains an open
measurement.  The direct classical comparison is bounded to the only two
common checked-in levels; no classical schedule comparable to the 77 Weber
levels exists in this repository.

## 1. Sound early abort

The production rule is conservative.  SEA stops early only after its current
exact and certified constraints represent a complete bounded set of
Hasse-compatible traces.  The exact smooth engine then checks both
`p+1-t` and `p+1+t` for every trace.  A candidate is rejected only when its
complete `n^4`-smooth part is at most the certificate lower bound.  Heuristic
rejection is disabled in production.

The controlled threshold study on production index 0 found candidate counts
of 55,089 at level 281, 195 at level 283, and one at level 307.  Screening the
195 traces meant extracting 390 order smooth parts and took 99.722 s.  The two
remaining SEA levels took 68.115 s, and extracting the final curve/twist pair
took 3.769 s.  Thus stopping at 195 would predict 781.2 s versus the observed
753.4 s completion path: **27.8 s slower**, or about **3.7% more wall time**.
This negative result is why the default cap remains 64.

An end-to-end run with `trace-cap=4096` stopped index 1 at 1,188 traces, then
spent 465.623 s screening 2,376 orders and 1,097.637 s total.  It was a sound
rejection, but it is not a valid speedup comparison with the later optimized
index-1 run because the binaries differ.

The retained production history through global index 7 contains eight sound
smoothness rejections.  In total, 102 complete trace candidates generated 204
exact curve/twist order screens, and no exact point count was completed.  This
quantifies useful early-exit incidence, but the missing same-build `cap=64`
versus `cap=1` replay means there is not yet an attributable early-abort wall
speedup.

Source: [CPU benchmark](benchmark_20260730.md), [filtered-search benchmark](benchmark_20260731.md), and [bottleneck registry](bottleneck_registry.md).

## 2. Batching

### Polynomial modular reduction

A controlled code A/B replaced a modular reduction after every elimination
term with exact `mpz_submul` accumulation and one normalization per output
coefficient.  A permanent differential test covers dense near-modulus inputs
and a non-monic modulus.

| Measurement | Per-term reduction | Batched accumulation | Speedup |
|---|---:|---:|---:|
| Degree-301 `linear_roots` median | 6.919 s | 3.337 s | 2.07x |
| `p125` index-1 modular roots | 603.731 s | 223.681 s | 2.70x |
| `p125` index-1 SEA total | 770.594 s | 293.641 s | 2.62x |
| `p125` index-1 complete curve work | 777.153 s | 299.917 s | 2.59x |
| `p125` index-1 invocation wall | 856.12 s | 369.18 s | 2.32x |

The deterministic replay preserved 60 levels, 31 exact levels, four terminal
traces, and the `sound_smoothness_reject` result.  This is the strongest
retained batching ablation because the source difference and both result
identities are recorded.

### Exact-smooth order batching

The full-cache fixture extracted 128 orders in one bounded remainder-tree
batch in 25.371 s with eight threads.  A separate production extraction of
one curve/twist pair took 3.769 s under the same cache size and thread count.
Those rates are approximately 5.05 versus 0.53 orders/s, an indicative 9.5x
throughput ratio.  The 128-order fixture deliberately repeats two orders and
the observations are from separate invocations, so this is **not** presented
as a controlled 9.5x search speedup.

Current production processes one curve at a time.  It does not yet share one
reduced Weber table or polynomial setup across a batch of curves, so the
requested cross-curve SEA batching benefit remains unmeasured.

Source: [2026-08-01 reducer benchmark](benchmark_20260801.md) and [CPU benchmark](benchmark_20260730.md).

## 3. Curve/twist sharing

For a trace `t` of `E/F_p`, the quadratic twist has trace `-t`, so its order is
`p+1+t`.  The implementation constructs both orders from one trace state and
passes the entire interleaved list to the exact-smooth engine.  No second SEA
run is needed for the twist.

The exact operation count is therefore two order candidates per SEA trace.
For the first eight retained `p125` curves, eight SEA executions produced 102
bounded trace candidates and 204 exact order screens.  Relative to separately
point-counting both members of each curve/twist pair, this removes one of two
SEA invocations per generated pair (**50% of SEA invocation count**).  That is
an algebraic work-count result, not a measured 2x wall speedup: smoothness and
certificate work still runs on both orders, and no intentionally duplicated
twist-counting baseline was timed.

Correctness coverage includes the identity
`#E + #E_twist = 2p+2`, paired smooth-part extraction, and both certificate
sides.  See [search pipeline](search_pipeline.md) and `tests/test_core.cpp` /
`tests/test_exact_smooth.cpp`.

## 4. Prime scheduling

The current production loop visits authenticated, available prime levels in
strictly increasing order (starting at 5 and skipping 3 and missing tables).
It does not rank levels from measured information probability divided by cost,
and no alternate ordering has been replayed on the same curves.

There is useful input to a future scheduler, but not yet an ablation:

- the certified level-7 Atkin slice removed exactly `5/7` of the current
  ambiguity (1.807354922 bits) while levels 5 and 7 together took 0.34 s;
- production index 4 used 64 levels, 31 exact residues, and finished with 15
  traces; and
- retained per-level records contain modular-root, codomain, BMSS, and
  eigenvalue times, plus exact/Atkin information gain.

The level-7 measurement does not isolate level-7 cost, and a single curve does
not estimate its probability of supplying information.  Consequently no
schedule speedup is claimed.  Closing this row requires a frozen curve set and
same-binary replays of increasing order versus a schedule trained on a
disjoint curve set, with terminal trace sets checked for equality.

## 5. Specialized Weber modular-polynomial path

The actual search uses authenticated sparse Weber-f modular polynomials through
level 401, exhaustive source-lift handling, normalized codomain construction,
BMSS kernel recovery, and exact Frobenius eigenvalue checks.  Classical-j is
currently a correctness/reference path and supplies the tightly pinned level-5
and level-7 Atkin constraints.

### Controlled specialized-path ablation

All 77 Weber tables obey a checked covariance that permits one modular-root
evaluation per 24th-root source-lift orbit.  The implementation verifies the
identity term by term and has an explicit `--root-orbit-reuse 0|1` ablation;
unverified inputs retain exact per-lift evaluation.

| Workload | Per-lift baseline | Verified orbit reuse | Result |
|---|---:|---:|---:|
| Same `p125` curve through level 193, wall (two interleaved runs) | 154.52, 154.62 s | 64.88, 65.76 s | 2.366x median wall |
| Same level-193 runs, modular roots | 125.714, 125.871 s | 36.207, 36.825 s | 3.445x mean |
| Root evaluations through level 193 | 504 | 42 | 462 lifts transported |
| Production index-4 replay, modular roots | 1,252.390 s | 207.917 s | 6.023x |
| Production index-4 baseline SEA versus optimized invocation | 1,418.823 s | 377.42 s | 3.759x |

Both level-193 modes emitted the same canonical mathematical hash.  The
index-4 replay matched all 64 old per-level mathematical projections, including
31 exact residues and the final 15 traces.  The compact identities and raw
output hashes are retained in
`artifacts/local/p125-weber-root-orbits-20260801/result.json`.

The next exact optimization reuses the characteristic-polynomial conjugacy of
the two Frobenius eigenvalues after separately validating both rational
isogenies.  A controlled level-193 off/on run reduced independent quotient-ring
recoveries from 42 to 21, derived the other 21 values as
`p*lambda^-1 mod ell`, cut eigenvalue time from 23.494 to 9.644 seconds
(2.436x), and cut wall time from 62.44 to 47.64 seconds (1.311x).  All 42
canonical level records matched.  On production index 4, eigenvalue time fell
from 148.418 to 70.196 seconds and wall from 377.42 to 291.43 seconds, while all
64 canonical records again matched.  The compact evidence is in
`artifacts/local/p125-conjugate-eigenvalue-20260801/result.json`.

### Classical-j comparison boundary

The checked-in low-level tables show the expected footprint direction, but
only at levels 5 and 7:

| Level | Classical-j bytes | Weber-f bytes | Size ratio |
|---:|---:|---:|---:|
| 5 | 1,126 | 80 | 14.1x |
| 7 | 2,636 | 87 | 30.3x |
| Combined | 3,762 | 167 | 22.5x |

The clean release binary was also timed on retained production index 4.  Each
mode was invoked 20 times at each common level and every invocation returned
the same exact result: two kernels and trace residue 2.

| Level | Classical-j wall (20) | Weber-f wall (20) | Weber/classical |
|---:|---:|---:|---:|
| 5 | 0.49 s | 6.10 s | 12.449x slower |
| 7 | 0.56 s | 6.15 s | 10.982x slower |

This is the honest tractable baseline: the small classical reference wins
because the Weber command exhaustively discovers source lifts.  Weber's
production value is not a low-level speed claim; it is the authenticated
sparse 77-level schedule plus the measured orbit transport and conjugate
optimizations above.  There is no classical-j table schedule comparable to
those 77 levels and therefore no same-curve production-scale classical-j
runtime record.  The compact commands and result identity are retained in
`artifacts/local/p125-weber-classical-lowlevel-20260801/result.json`.

Source: [Weber implementation](weber_implementation.md), [orbit benchmark](benchmark_20260801.md), and [compact result](../artifacts/local/p125-weber-root-orbits-20260801/result.json).

## 6. CPU, thread, cloud, and GPU decisions

The retained AWS Graviton4 trial gives a controlled thread-scaling result for
the pre-orbit level-193 workload:

| AWS `m8g.xlarge` threads | Wall | Speedup | Parallel efficiency |
|---:|---:|---:|---:|
| 1 | 614.87 s | 1.00x | 100% |
| 2 | 324.76 s | 1.89x | 94.7% |
| 4 | 179.20 s | 3.43x | 85.8% |

The matched local M4 ten-thread slice took 87.813 s, so the four-core AWS
instance was 2.04x slower per curve.  It could add about 0.49 local-machine
equivalents of throughput, but does not reduce single-curve latency.  The trial
cost about $0.07460 of instance time and the instance was terminated.  The
current decision is one ten-thread local worker; AWS remains off unless a
different instance wins a bounded benchmark.

After Weber orbit reuse, the level-193 modular-root work fell from 504 jobs to
42.  Exact conjugate reuse then reduced the 42 quotient-ring eigenvalue
recoveries to 21 without changing the root stage.  The resulting level-193
eigenvalue subtotal is 9.644 seconds.  On the first optimized production
curves, complete curve work was 180.496 seconds for index 6 and 525.564 seconds
for the higher-lift index 7; these are outcome runs, not a controlled ablation.

Peak resident memory in production is dominated by the authenticated 5.4 GB
exact-smooth cache: retained runs report roughly 5.5 GB RSS with zero swaps.
The CAS-free AWS SEA-only thread benchmark excluded that cache and therefore
is not a full production-memory result.

No CUDA SEA kernel or RunPod GPU throughput benchmark exists in the retained
evidence.  The RunPod scripts establish a safe operational path only.  GPU
acceleration remains disabled; no GPU speedup, cost efficiency, or batch-size
claim is justified until a homogeneous multi-limb kernel improves end-to-end
curve throughput against this CPU baseline.

Source: [AWS benchmark](aws_benchmark_20260801.md), [AWS operations](aws.md), and [RunPod operations](runpod.md).

## 7. Reproduction and interpretation rules

- The controlled polynomial-reducer commands, hashes, and environment are in
  [the 2026-08-01 benchmark](benchmark_20260801.md).
- The orbit ablation's exact command options, commit, binary digest, canonical
  hashes, and raw-output hashes are in
  [the compact artifact](../artifacts/local/p125-weber-root-orbits-20260801/result.json).
- The conjugate-eigenvalue off/on command, clean release identity, 42- and
  64-level canonical hashes, and raw timing hashes are in
  [the conjugate artifact](../artifacts/local/p125-conjugate-eigenvalue-20260801/result.json).
- The same-binary level-5/7 Weber/classical commands and negative timing
  boundary are in
  [the low-level comparison artifact](../artifacts/local/p125-weber-classical-lowlevel-20260801/result.json).
- The early-abort commands and full-cache extraction parameters are in
  [the CPU benchmark](benchmark_20260730.md).
- AWS commands, instance identity, pricing, artifacts, and teardown evidence
  are in [the AWS benchmark](aws_benchmark_20260801.md).

Timings from different commits are not combined into speedups.  A projected
time is labeled as such.  A table footprint, eliminated operation count, or
throughput indication is not relabeled as end-to-end wall acceleration.  The
live certificate search is outcome evidence and is deliberately excluded from
performance comparisons unless it is a frozen, identity-bound replay.
