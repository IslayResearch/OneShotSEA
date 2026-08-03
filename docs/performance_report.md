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
| Early abort | Complete bounded trace enumeration followed by exact full-`n^4` smooth screening of both signs | A source-bound 10,000-curve Magma corpus found 122 sound rejections, 14 before unique trace, and zero sound false negatives; same-build X1 caps 16/1 remained wall-indistinguishable | Implemented and independently audited; fixed-wave wall benefit over completing every point count was not observed |
| Batching | Batched polynomial reduction, batched remainder-tree smooth extraction, and a rolling shared-cache curve window | Reducer A/B: 2.07x synthetic median and 2.59x complete curve work; same-binary curve window: 1.46x full invocation and 1.73x warm-window throughput | Measured at kernel, smooth-batch, and complete-curve levels; level-major table reuse remains unimplemented |
| Curve/twist sharing | Derive `p+1-t` and `p+1+t` from one SEA trace state and submit both to one smooth batch | Every trace supplies two order candidates; the first twelve retained curves produced 136 trace candidates and 272 order screens from twelve SEA executions | Exact work-count reduction; no wall-time ablation |
| Prime scheduling | Available Weber levels are processed in increasing prime order | Held-out two-pair A/B: trained information/cost order 58.36 s median versus increasing 57.945 s, with identical final constraints and intrinsic evidence | Measured alternate was 0.7% slower; retain increasing order |
| Specialized modular-polynomial path | Authenticated Weber-f tables, BMSS kernel recovery, verified 24th-root orbit transport, and a validated generator-retained source coordinate | Controlled orbit off/on A/B: 2.366x median wall and 3.445x modular-root speedup through level 193; retained-source p125 index 17: 2.159x SEA and 2.995x modular roots with exact projections unchanged | Specialized hot-path ablations measured; production-scale classical-j is unavailable |
| Yield-biased family | Opt-in X1(11) and X1(27) point-four generators with the same SEA/smooth/certificate path | Same-binary reverse-order n=10 A/B: X1(27) averaged 197.93 s versus 215.10 s for X1(11); every run produced ten sound rejections and zero certificates | Promote X1(27): 1.08675x observed wall throughput; any yield uplift remains modeled, not observed |

Every requested dimension now has either a controlled wall-time A/B or an
exact algebraic work-count comparison.  Limitations remain explicit: the
same-build early-abort cap-16/cap-1 fixed wave was wall-neutral, curve/twist
sharing has no artificial duplicated-work baseline, and the direct classical comparison is bounded to the only two
common checked-in levels because no classical schedule comparable to the 77
Weber levels exists in this repository.

The decisive local A/B bundles now retain every recovered original progress
stream, log, timing capture, checkpoint, runner, and frozen benchmark
executable; the categories available differ by experiment.  Each bundle has a
complete `SHA256SUMS` covering its exact file set, including canonical
mathematical projections that were not listed in the older summary JSON.
`make test-performance-artifacts` verifies those manifests, selected
retained-raw hashes and counts, canonical semantic projections, and the
headline aggregate arithmetic.  Retained runner scripts are capture
provenance: some contain their original absolute paths and are not represented
as clean-clone launchers.  The reducer artifact explicitly labels its
unrecovered synthetic timing transcript and baseline invocation-time file.
The 5.4 GB exact-smooth cache remains an authenticated reproducible dependency
rather than committed performance evidence.

## Production hot-path replay gate

The accepted arithmetic changes were replayed on the exact same 30 production
curves `[1000000,1000030)` on the 16-vCPU RunPod worker, with the same seed,
X1(27) point-four family, table and smooth-cache identities, fallback policy,
and resource topology.  The candidate was the clean immutable commit
`550815e0013361f5eee4cdb6b044f5cec1a9ae2c`, binary SHA-256
`550c38acebb0407de4fc1021d905796798f9f18534c341a8a5b5238d34259737`.
Persistent quotient-context reuse remained disabled because its stronger
full-SEA gate had missed the required `1.05x` threshold.

| Measurement | Baseline | Candidate | Candidate gate |
|---|---:|---:|---:|
| Wall time | 800.92 s | 554.18 s | `1.4452344x` speedup |
| Embedded total time | -- | -- | `1.544832x` speedup |
| Embedded SEA time | -- | -- | `1.879148x` speedup |
| Maximum RSS | 10,552,336 KiB | 10,551,336 KiB | `0.999905x` baseline |
| Swaps | 0 | 0 | pass |
| Exit status | 0 | 0 | pass |

Both runs produced 30 sound smoothness rejections, 14 complete point counts,
zero heuristic skips, and zero certificates.  Their exact non-timing semantic
projections have the same SHA-256,
`0eacf10cd8524641896755ad5e540a7e24542d23527993880606c989485f1459`.
The candidate attempt cost an estimated `$0.0989827056` at the recorded
`$0.643/hour` rate.  The retained binaries, manifests, commands, GNU-time
records, progress, checkpoints, checksums, result, and independent replay
auditor are in [the RunPod replay artifact](../artifacts/runpod/p125-runpod-cpu16-replay-550815e-20260802a/ohfo3hbov7ot8v/result.json).

The promoted binary then ran a predeclared adjacent-range `B_X -> A_X -> A_Y
-> B_Y` topology bracket.  `A` was one process with 16 curve threads and `B`
was two concurrently launched processes with 8 curve threads each.  Every
other workload and trust identity was fixed, each side of a pair produced the
same 64 exact semantic records, and all four legs completed without swaps,
heuristics, nonzero exits, or certificates.

| Pair | Dual wall union | Single wall | Single / dual | Dual summed peak RSS |
|---|---:|---:|---:|---:|
| X `[1000827,1000891)` | 1020.39 s | 1158.62 s | `1.1354678111x` | 20.126 GiB |
| Y `[1000891,1000955)` | 1145.04 s | 1204.37 s | `1.0518147837x` | 20.125 GiB |

The geometric-mean speedup is `1.0928411733x`, strictly above the predeclared
`1.05x` gate; both individual speedups are strictly above one and both dual
RSS sums are below the 48 GiB limit.  The four effective intervals cost an
estimated `$0.808826` at `$0.643/hour`.  The hardened auditor validates the
commands, assignments, chronology, concurrency, manifests, logs, GNU-time
records, checkpoints and CRCs, progress semantics, checksums, fetch metadata,
build provenance, retained binary, and source commit.  Full reproduction and
the conservative next-epoch calculation are in [the topology gate
artifact](../artifacts/runpod/p125-topology-gate-550815e-20260803a/README.md).

## 1. Sound early abort

The production rule is conservative.  SEA stops early only after its current
exact and certified constraints represent a complete bounded set of
Hasse-compatible traces.  The exact smooth engine then checks both
`p+1-t` and `p+1+t` for every trace.  A candidate is rejected only when its
complete `n^4`-smooth part is at most the certificate lower bound.  Heuristic
rejection is disabled in production.

### Source-bound 10,000-curve oracle audit

The production Weber emitter was compared with Magma 2.29-1 on 2,000
deterministic curves in each of the 16-, 20-, 24-, 28-, and 32-bit prime
buckets.  Every record includes the normal exact-Schoof-fallback result and a
separately executed fallback-off counterfactual for the same curve.  The
create-only corpus is bound to clean commit `dc79fbd`, the native executable,
all 82 authenticated table files, the controlled Magma installation, and the
driver/common/oracle source bytes.  Its 10,000 records have SHA-256
`0e02c9cb090bc3292a37fd924a092e21bef5629a3140541ed83db51bad1dfe6e`.

An offline audit then reconstructed every bounded trace set, required the
Magma trace, and independently trial-factored 107,220 curve/twist orders.  It
found 122 sound rejections, including 14 before a unique trace and therefore
14 saved full point counts, with **zero sound false negatives**.  The explicit
`schoof_fallback=0, skip_incomplete_curves=1` counterfactual skipped no curve
in this corpus, so both its observed rejection rate and false-negative rate
were zero.  This is an observed policy result, not evidence that the heuristic
can never skip: the test suite separately pins first- and second-pass
false-negative fixtures and keeps those labels out of the sound metric.

The corpus took 4,116 seconds on the local Apple M4; the streaming offline
factor/replay pass took 3.61 seconds.  Compact identities, commands, hashes,
and results are retained in
[`artifacts/local/weber-oracle-v2-10000-20260802/result.json`](../artifacts/local/weber-oracle-v2-10000-20260802/result.json).
The complete 94,301,494-byte record stream is also retained as a deterministic
3.6 MiB gzip beside that result; its audit streams and parses all 10,000
records and reproduces SHA-256
`0e02c9cb090bc3292a37fd924a092e21bef5629a3140541ed83db51bad1dfe6e`.

### Representative multi-limb oracle completion gate

The production-Weber corpus path was also run on one deterministic curve and
twist at each of 64, 128, 256, and 416 bits.  All four native point counts
completed and matched independent Magma orders and traces.  Across the normal
production final states, the retained stream contains 70 exact-Elkies records,
4 certified-Atkin records, 64 fail-closed unconstrained records, and 7
exact-Schoof fallback records.  The fallback-off executions are validated
separately but are not double-counted in those totals.  The live validator
checked every classification and every intermediate CRT state against the
Magma trace.  The accepted clean-commit recapture took 196 seconds on the
recorded Apple M4 host.

The compact artifact and exact recapture command are in
[`artifacts/local/weber-oracle-multilimb-20260803`](../artifacts/local/weber-oracle-multilimb-20260803/README.md).
Its clean-checkout audit authenticates the record stream and semantically
replays the deterministic source/curve/twist identities, every cumulative CRT
transition and Hasse candidate count, each retained residue/classification, and
the final singleton.  The exact clean-built native executable and capture
driver/oracle inputs are retained with the artifact.  This supplies
representative multi-limb correctness coverage; the
10,000-curve 16-32-bit corpus remains the statistical breadth and early-abort
audit.

The implemented exact trace-prior policy narrows this complete set without a
heuristic assumption.  Weber-f adds `t = p+1 (mod 4)` only after directly
finding all three rational roots of the actual short Weierstrass cubic.  For
X1(11), the selected curve or twist gives `t = +(p+1)` or `-(p+1)` modulo its
certified group-order divisor 44, 88, or conditionally 176; because 11 divides every modulus, SEA
skips the redundant `ell=11` table.  The prior participates in both sound early
screening and the exact unique-trace gate, is emitted in curve telemetry, and
changes the schedule digest.  The retained family A/B predates this policy, so
it does not measure the policy's additional SEA reduction.

The controlled threshold study on production index 0 found candidate counts
of 55,089 at level 281, 195 at level 283, and one at level 307.  Screening the
195 traces meant extracting 390 order smooth parts and took 99.722 s.  The two
remaining SEA levels took 68.115 s, and extracting the final curve/twist pair
took 3.769 s.  Thus stopping at 195 would predict 781.2 s versus the observed
753.4 s completion path: **27.8 s slower**, or about **3.7% more wall time**.
This negative result is why the family-agnostic default cap remains 64.

An end-to-end run with `trace-cap=4096` stopped index 1 at 1,188 traces, then
spent 465.623 s screening 2,376 orders and 1,097.637 s total.  It was a sound
rejection, but it is not a valid speedup comparison with the later optimized
index-1 run because the binaries differ.

Index 206 exposed the distinct exhausted-table case: ordinary cap-16 SEA ended
with 45,948 exact and 9,189 effective candidates after all 76 levels. A
retained-state exact-Schoof prototype added residues modulo 3, 5, 13, and 17,
reducing the complete sets to 14/14 in 3.997926 seconds of fallback work. Exact
screening of the resulting 28 curve/twist orders produced a nonheuristic sound
smoothness rejection in 172.17 seconds wall. The final committed frozen binary
then reproduced every mathematical projection and the same sound rejection in
169.36 seconds wall, with 3.922415 seconds of fallback work. There is no
same-binary no-fallback completion baseline, so these are completion and
release-gate evidence, not a speedup claim. The final replay satisfies the
committed-build recovery gate. See [the fallback audit](schoof_fallback.md).

Index 246 then exceeded the committed v1 tail: the final production binary
computed exact levels 13 and 17 but stopped fail-closed at 5,542 exact and 791
effective traces. It emitted `sea_level_limit`, did not advance the checkpoint,
and did not claim a rejection. A v2 prototype added missing exact levels 7,
11, 13, 17, and 37, reaching 2/2 traces in 82.850855 seconds of summed fallback
work; exact screening of four curve/twist orders produced a sound smoothness
rejection. The 78.273594-second level-37 residue dominated that tail. The v1
and v2 walls are not a speed comparison because their curve/SEA thread layouts
and telemetry differ. The final committed/frozen-v2 replay reproduced the
prototype's complete non-timing projection and sound rejection, satisfying the
release gate. Its 340.87-second wall and component timings are excluded from
performance comparison because the early run overlapped a concurrent X1(25)
benchmark. See [the fallback audit](schoof_fallback.md).

The retained production history through global index 11 contains twelve sound
smoothness rejections.  In total, 136 complete trace candidates generated 272
exact curve/twist order screens, and no exact point count was completed.  This
quantifies useful early-exit incidence, but the missing same-build `cap=64`
versus `cap=1` replay means there is not yet an attributable early-abort wall
speedup.

The 272 values are trace-candidate screens, not independent yield trials.  A
curve has one actual trace and therefore only two actual group orders; twelve
curves represent 24 actual curve/twist orders.  False trace candidates make
the rejection proof sound but cannot produce a certificate.

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
identities are recorded.  The retained progress, checkpoint, and log bytes,
including the explicit limitation on the unrecovered synthetic timing
transcript, are bound by [the reducer artifact](../artifacts/local/p125-polynomial-reducer-ab-20260731/result.json).

### Thresholded exact Karatsuba convolution

The dense multiplication backend now switches from exact schoolbook
convolution to recursive Karatsuba at 32 coefficients, while retaining
schoolbook multiplication for small or highly unbalanced operands.  An
independent field-reducing schoolbook oracle covers degrees straddling the
threshold, odd and non-power-of-two sizes, the 416-bit target field, and monic
and nonmonic quotient moduli.

An isolated degree-194 p125 benchmark measured raw multiplication at 1.896 ms
before and 0.703 ms after (2.70x), `mulmod` at 3.345 versus 2.183 ms (1.53x),
and `squaremod` at 3.333 versus 2.183 ms (1.53x).  Five interleaved quotient-
Frobenius pairs improved from 1.883760 to 1.212438 seconds median (1.554x), with
identical degree and evaluation checksums in every pair.

The specialized square's schoolbook leaves were then tightened by accumulating
undoubled cross terms in place with `mpz_addmul`, doubling each output
coefficient once, and adding the diagonal squares afterward.  Reverse-order
same-input p125 level-193 pairs improved external SEA wall from 15.275 to
14.165 seconds (1.07836x), modular roots by 1.08654x, and eigen recovery by
1.08973x, with all 42 non-timing level projections unchanged.  This is a
bounded SEA result rather than a full-search throughput claim; see
[the addmul artifact](../artifacts/local/p125-polynomial-square-addmul-20260801/result.json).

An exact odd-degree norm prototype then avoided the full y-coordinate
Frobenius exponent at `ell = 3 mod 4`. It improved the targeted eigenvalue
subtotal by 1.52646x and all eigen recovery by 1.25055x, but its reverse-order
p125 level-193 wall result was only 14.325 to 13.655 seconds, or 1.04907x.
That does not strictly exceed the predefined 1.05x gate, so the prototype was
rejected despite identical 42-level projections. See
[the negative norm artifact](../artifacts/local/p125-odd-degree-norm-sign-20260801/result.json).

The adjacent Gauss-lemma variant then eliminated the dense norms of the
generic point's Jacobian coordinates, retaining only `Norm(f)`. Its focused
18-kernel differential test passed and the targeted odd-degree subtotal
improved by 1.56006x, but a fresh strict B/S/S/B gate moved mean wall only from
14.140 to 13.645 seconds (1.036277x). All 43 non-timing level-and-summary
records matched. The candidate was rejected without a full promotion suite,
closing this route; see [the Gauss-lemma addendum](../artifacts/local/p125-odd-degree-gauss-norm-sign-20260801/result.json).

A follow-up exact cleanup retained the modulus and denominator coefficient
vectors by const reference in the two quadratic elimination loops, removing an
`mpz_class` copy from every inner product.  Five strictly interleaved
degree-194 quotient-Frobenius pairs improved from 1.183812 to 0.964617 seconds
median (1.227x, or 18.52% less time), with the same degree-193 checksum in all
ten runs.  This second measurement postdates the frozen curve-scaling binary,
so its benefit is not folded into the K=1--10 curve timings below.

The current frozen binary also replayed production indices 8 and 9 serially.
Warm curve work fell from 329.264 to 110.340 seconds (2.984x) and from 254.163
to 90.010 seconds (2.824x), respectively.  SEA alone fell from 318.729 to
106.529 seconds and from 224.190 to 75.932 seconds.  The complete canonical
mathematical projections match both the retained pre-change records and the
parallel replay.  Because the full replay also includes the subsequently
committed curve-window coordinator, the isolated kernel pairs are the direct
Karatsuba attribution; the curve comparison is labeled as a cross-commit
current-path result.

### Exact-smooth order batching

The full-cache fixture extracted 128 orders in one bounded remainder-tree
batch in 25.371 s with eight threads.  A separate production extraction of
one curve/twist pair took 3.769 s under the same cache size and thread count.
Those rates are approximately 5.05 versus 0.53 orders/s, an indicative 9.5x
throughput ratio.  The 128-order fixture deliberately repeats two orders and
the observations are from separate invocations, so this is **not** presented
as a controlled 9.5x search speedup.

Cache construction now combines segment products through a binary-carry
product forest instead of multiplying every new segment into the entire
accumulator. On a same-host 61-bit setup fixture deliberately split into 139
segments, wall time fell from 2.76 to 0.63 seconds (4.381x). The old and new
portable caches were byte-identical. This isolates the former unbalanced-merge
cost; the machine was concurrently loaded and the small target is not a p125
wall-time projection. The [retained setup microbenchmark](../artifacts/local/p61-smooth-product-forest-20260802/result.json)
binds both binaries, source, commands, timings, and cache digests.

### Rolling shared-cache curve window

Production can now keep a rolling, ordered window of complete curves in flight
against one immutable exact-smooth cache.  This is curve-level concurrency,
not level-major table reuse: each curve still performs its own Weber work.
Reports and durable artifacts commit in index order.

The same frozen Karatsuba binary was then scaled from one to ten curve slots.
The per-curve SEA split was `10,5,3,2,1` and the smooth split was `8,4,2,1,1`
for `K=1,2,3,5,10`, respectively.  Each parallel run launched one full initial
window; warm seconds per curve is the maximum reported curve total divided by
the window size.

| Curve slots | Range | Invocation wall/curve | Fixed warm seconds/curve | Peak RSS |
|---:|---:|---:|---:|---:|
| 1 | `[8,10)` | 133.095 s | 100.175 s | 6,020,055,040 B |
| 2 | `[8,10)` | 90.970 s | 57.800 s | 6,288,162,816 B |
| 3 | `[8,11)` | 62.677 s | 40.501 s | 5,786,042,368 B |
| 5 | `[8,13)` | 47.396 s | 34.185 s | 6,387,302,400 B |
| 10 | `[8,18)` | 33.651 s | 27.071 s | 6,188,974,080 B |

Every overlapping canonical record matches the serial and retained production
evidence.  Ten slots are the measured local choice: about 10x the former
270-second warm baseline and only 1.258x above the model's 21.51-second
30-day target.  System-wide `vm_stat` observed 68.9 MiB of swapouts during the
K=10 window; it is not process-attributed, but the pressure is included in the
measured wall time.  Concurrency is not raised beyond the ten physical cores,
and long-run workers retain memory telemetry.  This is not yet level-major
cross-curve table batching.  The original pre-commit observations at indices
12--17 were benchmark-only.  A later authenticated production identity
durably processed unique indices 12--59; those records, rather than the
benchmark observations, advance the retained yield sample and cursor to 60.

Source: [2026-08-01 reducer benchmark](benchmark_20260801.md) and [CPU benchmark](benchmark_20260730.md).

## 3. Curve/twist sharing

For a trace `t` of `E/F_p`, the quadratic twist has trace `-t`, so its order is
`p+1+t`.  The implementation constructs both orders from one trace state and
passes the entire interleaved list to the exact-smooth engine.  No second SEA
run is needed for the twist.

The exact operation count is therefore two order candidates per SEA trace.
For the first twelve retained `p125` curves, twelve SEA executions produced
136 bounded trace candidates and 272 exact order screens.  Relative to separately
point-counting both members of each curve/twist pair, this removes one of two
SEA invocations per generated pair (**50% of SEA invocation count**).  That is
an algebraic work-count result, not a measured 2x wall speedup: smoothness and
certificate work still runs on both orders, and no intentionally duplicated
twist-counting baseline was timed.

Correctness coverage includes the identity
`#E + #E_twist = 2p+2`, paired smooth-part extraction, and both certificate
sides.  The twelve source progress streams and the distinction between 272
screens and 24 actual curve/twist order opportunities are bound by
[the work-count artifact](../artifacts/local/p125-curve-twist-workcount-20260801/result.json).
See [search pipeline](search_pipeline.md) and `tests/test_core.cpp` /
`tests/test_exact_smooth.cpp`.

## 4. Prime scheduling

The benchmark-only scheduler ranks a complete level profile by decreasing
measured information per cost.  Cross-products are compared with exact integer
arithmetic, ties retain increasing-prime order, and malformed, duplicate,
missing, extra, or zero-cost rows fail closed.  Production does not consume a
profile and therefore remains strictly increasing-order.

The profile used all eight unique production indices 0--7 for information:
an exact residue contributes `log2(ell)`, while a certified Atkin set of size
`r` contributes `log2(ell/r)`.  Current optimized indices 4 and 8 supplied
the complete per-level cost sample.  A separate held-out `p125` curve was run
through every available level at or below 193 in two interleaved pairs.

| Schedule | Wall runs | Median wall | Median user |
|---|---:|---:|---:|
| Increasing prime | 58.57, 57.32 s | 57.945 s | 49.805 s |
| Expected information/cost | 57.94, 58.78 s | 58.36 s | 50.15 s |

All four runs produced identical final exact/effective constraints and the
same sorted intrinsic/operation-count SHA-256
`4bf0522c292002a1b9c51726684b9feae0c3bc05119867afcecd4d6622ce0d98`.
The scored order was 0.716% slower by median wall time, which is noise rather
than evidence of benefit.  Increasing order therefore remains the measured,
simpler production policy.  The profile, command identity, order, and raw
hashes are retained in
`artifacts/local/p125-prime-schedule-20260801/`.

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

The production generators also retain their exact Weber source coordinate.
After strict canonical, nonzero, nonexceptional, unramified, and exact-j
validation, SEA may begin with this singleton instead of rediscovering every
rational source lift.  The generic unknown-source API remains exhaustive.  On
the one-orbit p125 index 12 control, SEA changed only from 65.383 to 64.764
seconds (1.010x noise); existing orbit reuse had already collapsed all twelve
lifts.  The three-orbit index 17 case fell from 129.163 to 59.823 seconds
(2.159x), with modular roots falling from 100.457 to 33.537 seconds (2.995x)
and stable BMSS/eigenvalue work.  All exact per-level projections and final
traces matched.  This is a paired mechanism benchmark, not an estimate of the
frequency of 36-lift curves.  Evidence is in
`artifacts/local/p125-known-source-lift-20260801/result.json`.

A pre-Kronecker reusable reciprocal-reduction context was tested behind a
complete exactness gate and rejected.  It slowed a degree-194 Frobenius pair
from a 1.95062-second median to 2.22974 seconds (14.31%), and deterministic
p125 Weber level 277 from 3.79822 to 4.06195 seconds (6.94%); its eigenvalue
stage alone was 21.20% slower.  All mathematical outputs matched, and the
prototype was fully reverted.  The measured historical negative is retained in
`artifacts/local/p125-reciprocal-reduction-20260801/result.json`.

The accepted follow-up removes a redundant normalization pass over each raw
quotient-ring product.  Two interleaved p125 index-17 runs reduced mean SEA
from 59.942 to 57.927 seconds (1.03479x), modular roots from 33.635 to 32.391
seconds (1.03840x), and eigenvalue recovery from 18.652 to 17.810 seconds
(1.04723x).  All 55 per-level exact projections and the final trace matched.
Evidence is in
`artifacts/local/p125-deferred-product-normalization-20260801/result.json`.

Polynomial Frobenius now uses an unsigned odd-power window selected from
widths one through five by exact quotient-operation count, including table
setup.  The no-inverse construction is exact in reducible and repeated-factor
quotients; the former binary path remains the differential reference.  Two
reverse-order p125 index-17 pairs reduced mean SEA from 57.088 to 49.178
seconds (1.16085x), modular roots from 32.007 to 24.013 seconds (1.33293x),
and external wall from 57.905 to 49.860 seconds (1.16135x).  All 55 exact
level projections and the final trace matched. Evidence is in
`artifacts/local/p125-frobenius-window-20260801/result.json`.

The same window applies to Frobenius eigenvalue recovery after observing that
its production Element bases `x` and `f` have zero y-coordinate and remain in
the polynomial subring. The general nonzero-y binary path is unchanged. An
isolated reverse-order level-269 pair improved mean eigen recovery from 1.886
to 1.653 seconds (1.14084x). A full reverse-order p125 pair improved mean SEA
from 49.322 to 46.754 seconds (1.05493x) and eigen recovery from 17.681 to
15.069 seconds (1.17333x); roots and BMSS were stable controls. All kernel
projections, 55 exact level projections, and the final trace matched. Evidence
is in `artifacts/local/p125-element-subring-window-20260801/result.json`.

The quotient ring can own one prepared `PolyModContext` for the complete
Frobenius/eigenvalue calculation.  Three local compile-time off/on runs on the
catalog-authenticated level-409 p125 fixture reduced median eigenvalue recovery
from 2.836558 to 2.282278 seconds (1.24286x), median SEA from 5.155449 to
4.597293 seconds (1.12141x), and median full invocation time from 7.666286 to
7.074818 seconds (1.08360x).  Roots and BMSS were stable controls, and all six
non-timing projections matched.  That compact experiment is retained in
[`artifacts/local/p125-quotient-context-20260802/result.json`](../artifacts/local/p125-quotient-context-20260802/result.json).

A stronger isolated RunPod B/A/A/B gate then exercised the complete level-401
SEA path with an honestly disabled baseline that did not even construct the
prepared context.  The two pairs improved only 1.03990x and 1.03177x, for a
mean 1.03583x SEA speedup, and candidate peak RSS increased by 1,000 KiB.  The
semantic projections remained identical, but the result misses both the
predeclared 1.05x speed threshold and the 5% isolated-RSS threshold.  Reuse is
therefore disabled by default and retained only as an experimental compile-time
path.  Raw commands, binaries, checksums, and the rejected summary are in
[`artifacts/runpod/p125-poly-isolated-d280fa7-20260802`](../artifacts/runpod/p125-poly-isolated-d280fa7-20260802/result.json).

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
current decision is one local process with ten rolling curves and one SEA
thread per curve; AWS remains off unless a different instance wins a bounded
benchmark.

After Weber orbit reuse, the level-193 modular-root work fell from 504 jobs to
42.  Exact conjugate reuse then reduced the 42 quotient-ring eigenvalue
recoveries to 21 without changing the root stage.  The resulting level-193
eigenvalue subtotal is 9.644 seconds.  On the first optimized production
curves, complete curve work was 180.496 seconds for index 6 and 525.564 seconds
for the higher-lift index 7; these are outcome runs, not a controlled ablation.

Peak resident memory in production is dominated by the authenticated 5.4 GB
exact-smooth cache.  The current ten-curve replay reported 6.19 GB RSS versus
6.02 GB serial and modest system-wide swap activity; older retained
single-curve runs reported roughly 5.5 GB RSS with zero swaps.
The CAS-free AWS SEA-only thread benchmark excluded that cache and therefore
is not a full production-memory result.

No CUDA SEA kernel or RunPod GPU throughput benchmark exists in the retained
evidence.  The RunPod scripts establish a safe operational path only.  GPU
acceleration remains disabled; no GPU speedup, cost efficiency, or batch-size
claim is justified until a homogeneous multi-limb kernel improves end-to-end
curve throughput against this CPU baseline.

An exact limb-aligned Kronecker convolution now handles balanced polynomial
products and squares from 48 coefficients.  Same-source reverse-order p125
brackets improved degree-194 and degree-401 quotient Frobenius by 1.16986x and
1.17477x.  A full deterministic 55-level SEA replay improved the SEA subtotal
from 54.405483 to 46.688214 seconds (1.16529x); every baseline and candidate
projection had identical SHA-256
`8055a435d1abd535574867a55169168635ac683c2ed9e065df135d7440f4b8e6`.
The full bracket experienced host-load drift and therefore uses paired means;
the isolated Frobenius brackets are cleaner kernel evidence.  This is an SEA
throughput result, not yet a new multi-curve search-throughput or yield result.
See [the packed-convolution audit](kronecker_convolution.md).

With that packed-convolution foundation in place, reciprocal reduction was
reimplemented and re-measured.  One reversed-modulus inverse is now amortized
across each polynomial exponentiation, and its quotient products use packed
convolution.  Against the same source compiled with reciprocal reduction
disabled, reverse-order p125 brackets improved degree-194 and degree-401
Frobenius from 2.114704 to 1.191504 seconds (1.77482x) and 7.948951 to
2.473199 seconds (3.21404x).  Full SEA improved from 56.073711 to 42.228734
seconds (1.32786x), with modular roots improving 1.88712x and all four exact
projections unchanged.  Threshold 48 was neutral end to end versus threshold
96 and regressed at degree 48, so production activates conservatively at
degree 96.  See [the post-Kronecker reciprocal audit](reciprocal_reduction_ab.md).

The modular-polynomial trust boundary no longer pins only the checked-in
level-401 manifest.  A 12,727-byte normalized source catalog binds all 166
admissible levels through 997 in the same content-addressed upstream archive,
and the converter can materialize any compact subset.  On the fixed 416-bit
p125 curve, newly admitted level 409 produced two validated isogenies and exact
trace residue 19 in 5.118 seconds, matching a retained independent Magma full
point count; the top catalog level 997 completed its
no-rational-neighbor path in 2.140 seconds.  The ordinary level-401 projection
remained SHA-256
`8055a435d1abd535574867a55169168635ac683c2ed9e065df135d7440f4b8e6`.
This is a bounded range/trust extension, not a direct-evaluation performance
claim.  See [the catalog audit](weber_on_demand_catalog.md) and the
[checksummed native/Magma bundle](../artifacts/local/p125-weber-catalog-magma-20260802/README.md).

Source: [AWS benchmark](aws_benchmark_20260801.md), [AWS operations](aws.md), and [RunPod operations](runpod.md).

## 7. Certificate-yield feasibility

A checked-in standard-library model solves the Dickman delay equation and
applies the Dickman--Mertens approximation to the exact `n^4`-smooth-part
threshold.  For a random integer near `p125`, it estimates
`3.478e-6` smooth-factor opportunities per order.  Conditioning only on the
even orders forced by Montgomery compatibility raises the marginal estimate
to `3.799e-6`.  The production Weber prefilter is stronger: its admitted
`p125` curves and twists have full rational `E[2]`, so both actual orders are
divisible by four and the modeled marginal is `4.150e-6`.  Treating the curve
and twist marginals as independent gives an explicitly optimistic union
estimate of `8.300e-6` per curve: about 120,489 curves in expectation, 83,516
at the median, and 360,951 at the 95th percentile.

The fixed-grid solver reports five significant digits.  A second grid at
twice the step changes the production-order probability by less than
`1e-5` relative; finer offline spot checks put the remaining discretization
error at a few parts per million.  Extra decimal places would therefore be
spurious precision for an already heuristic marginal model.

This is not a certificate-success probability.  The two orders are correlated
by `#E + #E_twist = 2p+2`; a large smooth divisor of the order can fail to
divide the group exponent; and exact-order point assembly can still fail.  All
three effects can only worsen the optimistic estimate.  The 272 values in the
retained progress history are trace-candidate order screens, whereas twelve
curves provide only 24 actual curve/twist order opportunities.

At 240--300 seconds per curve, the optimistic expected time is 335--418 days.
An expected 30-day result requires aggregate throughput near 21.51 seconds per
curve, about 12.6x the 270-second midpoint; a one-week target requires
5.02 seconds per curve, about 53.8x.  The Karatsuba plus ten-curve local window
now measures 27.07 warm seconds per curve, reducing the optimistic mean to
about 37.8 days and the remaining 30-day throughput gap to 1.258x.  A one-week
mean still needs another 5.39x.  This makes the next polynomial ceiling and a
sound yield-biased family higher priorities than simply renting the measured
AWS worker.

The same model shifts the selected-side threshold for the torsion families
under evaluation.  It also uses the exact relation
`#E + #E_twist = 2p+2`: for `p125`, selected-side divisibility by 44, 100, 108,
176, 200, or 432 forces the paired order to be divisible by four. At the
measured K=10 warm rate:

| Optimistic selected-side guarantee | Paired-yield multiplier | Expected days |
|---|---:|---:|
| Current full-E[2] divisor 4 | 1.000x | 37.75 |
| X1(11), cyclic divisor 44 | 1.177x | 32.06 |
| X1(11), group divisor 176 | 1.307x | 28.89 |
| X1(25), cyclic divisor 100 | 1.251x | 30.17 |
| X1(27), cyclic divisor 108 | 1.259x | 29.99 |
| X1(25), group divisor 200 | 1.320x | 28.60 |
| X1(27), group divisor 432 | 1.403x | 26.91 |

These are smooth-factor opportunity models, not certificate rates: a group
divisor need not divide the group exponent, and exact point assembly remains a
separate gate.

The required same-build family comparison is now retained for global indices
12--21.  With ten rolling curve slots, Weber took 306.19 seconds and X1(11)
point-four took 294.57 seconds for ten curves: 30.619 versus 29.457 invocation
seconds per curve, or **1.03945x observed X1 wall throughput**.  X1 generation
cost more (22.386 versus 0.085 aggregate curve-seconds), but this particular
X1 distribution spent less time in smoothness; it is therefore not valid to
attribute the wall difference to generation or SEA in isolation.  The families
sample different curves, so equal indices do not make a paired per-curve A/B.
Both runs produced ten sound smoothness rejections, three full point counts,
and **zero certificates**.  The n=10 window cannot estimate certificate yield.

Using only the guaranteed cyclic divisor 44 gives the conservative checked-in
model multiplier 1.1775.  Combining that model with observed wall throughput
gives `1.039447 * 1.1775 = 1.22395x` modeled smooth-order opportunity rate.
Using selected group-order divisibility by 88 gives a 1.28829x sensitivity,
but does not establish an exact-order-88 point or an empirical certificate
rate.  The benchmark clears the earlier throughput-penalty gate in favor of
X1 point-four.  The exact trace-prior implementation postdates the captured
binary and deliberately changes schedule identity; its correctness and clean
build gate must pass before production, and the old benchmark checkpoint must
not be resumed as that new schedule.

Source: [reproducible yield artifact](../artifacts/local/p125-yield-model-20260801/result.json),
[same-build family A/B artifact](../artifacts/local/p125-x1-11-family-ab-20260801/result.json),
and [family A/B note](x1_11_family_ab.md).

After the exact X1 trace prior landed, a same-build trace-cap ablation on the
same ten X1 curves measured caps 64/32/16/1 at
272.19/271.95/260.34/260.30 seconds.  Caps 64 and 32 stopped identically at 97
complete trace candidates.  Cap 16 processed three additional SEA levels,
cut the retained complete set to 24 traces, and reduced summed smoothness from
469.073 to 221.979 seconds and summed concurrent curve work from 1678.006 to
1435.541 seconds.  Cap 1 finished every point count but used 1451.021 summed
curve-seconds, 1.08% more than cap 16; its 0.04-second fixed-wave wall edge is
not meaningful.  Production therefore overrides the default with cap 16.
All four runs ended in ten sound rejections and zero certificates.  Source:
[trace-prior A/B](x1_11_trace_prior_ab.md) and
[trace-cap ablation](x1_11_trace_cap.md).

For the production target `p125=5 (mod 8)`, the retained Weber/Montgomery
identity strengthens every accepted X1 point-four selected-side group divisor
from 88 to 176 at zero per-curve cost.  The two possible 2-primary structures
are full rational `E[4]` or a rational order-eight point with independent
`E[2]`; either contributes order 16, and the rational order-11 point gives
176.  An exhaustive accepted-generator sweep through prime 1009 covered 550
cases without a counterexample, while raw-X1 and admitted `p=1 (mod 8)`
order-88 counterexamples confirm both admission boundaries are necessary.
Exact retained p125 traces validate the signed modulus-176 residues, and
several exact orders rule out promotion to 352.  See
[the conditional divisor proof](x1_11_point4_176.md).

Relative to the earlier 306.19-second Weber family window, the selected
260.34-second X1/prior/cap-16 window is 1.17612x observed fixed-wave
throughput.  Combining that observation with the conservative 1.1775 cyclic-
44 model gives 1.38488x modeled opportunity rate.  This layered estimate
crosses different binaries and curve distributions and still has only ten
curves per window; it is a planning projection, not measured certificate
yield.

The subsequent same-binary X1(11)-versus-X1(27) gate used the current
point-four, cap-16 path on indices `[0,10)` in reverse order: X1(11), two
X1(27) repetitions, then X1(11). Mean invocation wall was 215.10 seconds for
X1(11) and 197.93 seconds for X1(27), giving **1.08675x observed X1(27) wall
throughput**. Mean summed concurrent curve work improved by 1.19214x even
though X1(27) generation was 3.55259x slower; SEA work improved by 1.29711x.
Every run contained ten sound smoothness rejections, six full point counts,
zero heuristic skips, and zero certificates.

The repeated runs reproduce the same ten deterministic curves within each
family, while equal cross-family indices denote different curve streams.
Consequently this is an `n=10` throughput gate, not 20 independent samples
and not a paired-curve experiment. Separately, the Dickman--Mertens cyclic
divisor comparison, 108 versus 44, models a 1.068973x X1(27) smooth-order
opportunity multiplier. Combined with measured wall throughput that is
1.16170x modeled opportunity per wall time; the group-divisor 432-versus-176
sensitivity is 1.16684x. Neither is observed yield or a certificate-rate
claim. The gate therefore promotes X1(27) for the next production identity,
subject to continued long-run monitoring. Source: [X1(27) family A/B
artifact](../artifacts/local/p125-x1-27-family-ab-20260801/result.json) and
[audit note](x1_27_family_ab.md).

## Compact classical direct contexts

The direct classical producer now discards admitted CM curves, kernels, and
isogenies after converting each witness to two square interpolation matrices.
For `K` auxiliary primes at level `ell`, the persistent coefficient payload is
exactly `2 K (ell+2)^2` canonical 64-bit residues rather than an
`O(K ell^3)` collection of rich isogeny objects.

On the same Apple M4 host, isolated level-29 processes reduced peak RSS from
60,620,800 bytes for the pre-compaction library to 12,779,520 and 13,205,504
bytes for the compact library. The compact median is 78.57% lower. The checked
distinct-`j` warm evaluation fell from 251,353 microseconds to an 85,607.5
microsecond compact median, a 2.936x speedup. At level 13, median RSS fell
42.22% and the distinct-`j` warm path improved 1.921x. Every measured result
was independently validated by Schoof outside the timed interval.

The same candidate binary also ran three `serial / four-worker / four-worker /
serial` brackets at levels 7 and 11. Median preparation speedups were 3.151x
and 3.260x, respectively. Cross-commit cold timings are deliberately excluded
from the compaction claim because the sustained level-29 legs were thermally
variable. Source: [compact direct-context artifact](../artifacts/local/p125-classical-direct-compact-20260803/result.json)
and [design note](direct_context_compaction.md).

## 8. Reproduction and interpretation rules

- The controlled polynomial-reducer commands, hashes, and environment are in
  [the 2026-08-01 benchmark](benchmark_20260801.md).
- The frozen-binary curve-window resources, canonical hashes, raw hashes,
  memory counters, and current Karatsuba curve replays are in
  [the curve-parallel artifact](../artifacts/local/p125-curve-parallel-20260801/result.json).
- The orbit ablation's exact command options, commit, binary digest, canonical
  hashes, and raw-output hashes are in
  [the compact artifact](../artifacts/local/p125-weber-root-orbits-20260801/result.json).
- The conjugate-eigenvalue off/on command, clean release identity, 42- and
  64-level canonical hashes, and raw timing hashes are in
  [the conjugate artifact](../artifacts/local/p125-conjugate-eigenvalue-20260801/result.json).
- The same-binary level-5/7 Weber/classical commands and negative timing
  boundary are in
  [the low-level comparison artifact](../artifacts/local/p125-weber-classical-lowlevel-20260801/result.json).
- The trained profile, held-out two-pair scheduling A/B, equality hashes, and
  negative result are in
  [the prime-scheduling artifact](../artifacts/local/p125-prime-schedule-20260801/result.json).
- The standard-library Dickman solver, observed-opportunity distinction, and
  throughput projections are in
  [the p125 yield artifact](../artifacts/local/p125-yield-model-20260801/result.json).
- The same-build Weber/X1(11)-point-four command identity, raw hashes,
  per-curve timing records, zero-certificate outcome, and modeled-versus-
  observed distinction are in
  [the family A/B artifact](../artifacts/local/p125-x1-11-family-ab-20260801/result.json).
- The same-binary reverse-order X1(11)/X1(27) command identity, repeat checks,
  raw hashes, zero-certificate outcome, and measured-versus-modeled distinction
  are in [the X1(27) family A/B artifact](../artifacts/local/p125-x1-27-family-ab-20260801/result.json).
- The paired before/after exact-prior records are in
  [the trace-prior artifact](../artifacts/local/p125-x1-11-trace-prior-ab-20260801/result.json),
  and the same-build 64/32/16/1 cap selection is in
  [the trace-cap artifact](../artifacts/local/p125-x1-11-trace-cap-20260801/result.json).
- The conditional modulus-176 proof, exhaustive admitted-generator sweep, and
  sharp p=1 mod 8/admission counterexamples are in
  [the point-four divisor artifact](../artifacts/local/p125-x1-11-point4-176-20260801/result.json).
- The generator-retained Weber-source validation, multi-orbit audit, exact
  projections, and paired one-/three-orbit timings are in
  [the known-source artifact](../artifacts/local/p125-known-source-lift-20260801/result.json).
- The exact but slower reciprocal-reduction prototype is recorded in
  [the negative A/B artifact](../artifacts/local/p125-reciprocal-reduction-20260801/result.json).
- The accepted post-Kronecker reciprocal reducer, independent differentials,
  threshold ablation, and B/S/S/B p125 timing are in
  [the reciprocal/Kronecker artifact](../artifacts/local/p125-reciprocal-kronecker-20260802/result.json).
- The archive digest, 166-level normalized source catalog, trust adversaries,
  and isolated p125 levels 409/997 are in
  [the Weber catalog artifact](../artifacts/local/p125-weber-catalog-20260802/result.json).
- The accepted redundant-normalization removal and exact paired p125 timing
  are in [the deferred-normalization artifact](../artifacts/local/p125-deferred-product-normalization-20260801/result.json).
- The exact-cost polynomial window, quotient-ring differential boundary,
  operation counts, and reverse-order p125 pairs are in
  [the Frobenius-window artifact](../artifacts/local/p125-frobenius-window-20260801/result.json).
- The closed polynomial-subring proof, general-Element fallback tests,
  isolated eigen timing, and full p125 replay are in
  [the Element-subring artifact](../artifacts/local/p125-element-subring-window-20260801/result.json).
- The specialized three-square recurrence, adversarial differential boundary,
  isolated production-degree timings, and exact full p125 replay are in
  [the polynomial-square artifact](../artifacts/local/p125-polynomial-square-20260801/result.json).
- The in-place square cross-term identity, same-input reverse-order p125 pairs,
  exact projection hashes, and full validation boundary are in
  [the square-addmul artifact](../artifacts/local/p125-polynomial-square-addmul-20260801/result.json).
- The exact odd-degree norm proof, independent full-y differential oracle,
  reverse-order p125 gate, and strict 1.05x rejection are in
  [the norm-sign artifact](../artifacts/local/p125-odd-degree-norm-sign-20260801/result.json).
- The prototype and final committed index-206 retained-state Schoof commands,
  exact CRT replays, fallback residues, raw hashes, and release gate are in
  [the fallback artifact](../artifacts/local/p125-index206-schoof-fallback-20260801/result.json).
- The fail-closed index-246 v1 attempt, v2 prototype, and final committed
  frozen-binary recovery, including raw identities, CRT transitions, and the
  closed release gate, are in
  [the v2 fallback artifact](../artifacts/local/p125-index246-schoof-fallback-v2-20260801/result.json).
- The early-abort commands and full-cache extraction parameters are in
  [the CPU benchmark](benchmark_20260730.md).
- AWS commands, instance identity, pricing, artifacts, and teardown evidence
  are in [the AWS benchmark](aws_benchmark_20260801.md).
- The RunPod CPU worker identity, cpuset, rate, exact command, matched SEA
  state, timing, and cross-commit comparison boundary are in
  [the 2026-08-02 RunPod CPU benchmark](runpod_cpu_benchmark_20260802.md).
- The clean candidate/baseline identities, same-binary parallel brackets,
  isolated compact-context RSS bracket, raw benchmark records, and aggregation
  checks are in
  [the compact direct-context artifact](../artifacts/local/p125-classical-direct-compact-20260803/result.json).

Timings from different commits are not combined into speedups.  A projected
time is labeled as such.  A table footprint, eliminated operation count, or
throughput indication is not relabeled as end-to-end wall acceleration.  The
live certificate search is outcome evidence and is deliberately excluded from
performance comparisons unless it is a frozen, identity-bound replay.
