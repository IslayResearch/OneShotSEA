# Batched modular-reduction benchmark, 2026-08-01

This record evaluates a focused polynomial hot-path change before committing
it.  It is not a production-search identity and does not advance the retained
`p125` checkpoint.  The change replaces per-term field reductions in dense
modular-product elimination with exact `mpz_submul` accumulation followed by
one normalization per pivot/final coefficient.  A permanent differential
test covers dense near-modulus coefficients and a non-monic modulus over the
416-bit target field.

## Synthetic A/B measurement

A degree-301 polynomial over the `p125` field was passed to `linear_roots`.
The baseline and optimized executables were statically linked against library
builds differing only in the reducer hunk.

| Reducer | Runs (seconds) | Median |
|---|---:|---:|
| Per-term modular reductions | 7.018, 6.855, 6.919 | 6.919 s |
| Batched exact accumulation | 3.337, 3.334, 3.391 | 3.337 s |

The synthetic median speedup was 2.07x.

## Deterministic `p125` replay

The stronger validation replayed production global index 1 in a fresh
`work/p125/poly-opt-index1` directory.  It used seed `202607300000`, the
filtered Weber schedule through level 401, trace cap 64, ten SEA workers, and
the same authenticated 5,400,760,038-byte exact-smooth cache as the original
run.  The baseline is the retained index-1 record in
`work/p125/filtered/progress.jsonl`.

Both runs produced the same evidence: 60 SEA levels, 31 exact levels, four
remaining traces, and a `sound_smoothness_reject`.  The optimization therefore
changed timing but not the curve outcome or trace-set cardinality.

| Stage | Baseline | Optimized | Speedup |
|---|---:|---:|---:|
| Modular roots | 603.731 s | 223.681 s | 2.70x |
| Eigenvalues | 155.147 s | 59.865 s | 2.59x |
| BMSS | 10.609 s | 9.098 s | 1.17x |
| Normalized codomain | 0.944 s | 0.865 s | 1.09x |
| SEA total | 770.594 s | 293.641 s | 2.62x |
| Complete curve work | 777.153 s | 299.917 s | 2.59x |
| Invocation wall time | 856.12 s | 369.18 s | 2.32x |

The curve record reports peak RSS of `5476122624` bytes; a separate structured
projection taken from the same progress file reported `5476073472` bytes.
The small observation-time difference does not affect the memory conclusion:
the optimization did not increase the existing roughly 5.5 GB
exact-smooth-cache peak.

The optimized evaluation binary SHA-256 was
`de8bbf3a177b5a2c234e2ee4fbf5e8a737393cd114ca0a5263418ea41075d3ea`.
The exact two-file source diff SHA-256 was
`b6c6f1b75bda46be18884448a69293d8a1663a721cff7e6e217d06dc7d956fc5`.
Because this was an intentionally dirty pre-commit evaluation, the build id
was explicitly `worktree:poly-opt-eval`; a clean committed production replay
must use the final Git and binary identities.

Retained evaluation artifacts:

| Artifact | SHA-256 |
|---|---|
| `checkpoint.json` | `5489f4d4bd0f8fb6498be63165faaed5abd72b5a15a8360a6c94130dcb9e7777` |
| `progress.jsonl` | `fb5d8c867626c9732bc21b711ee63035bac6277d464a4f5248b658823025f289` |
| `run.log` | `3e4db1fabfd713b0de1cfe44eb928722ea158c82df1eead202c3da26139d88b6` |

The host was the same Apple M4 Mac mini (10 cores, 16 GB) used by the earlier
records, running macOS 26.5.1 with Apple clang 21.0.0.

## Certified level-7 Atkin reduction

The first production-safe Atkin slice was measured on the exact `p125` curve
used by the retained AWS level-193 benchmark:

```sh
./build/oneshotsea sea-weber-count \
  --p "$p" --a "$a" --b "$b" \
  --max-level 7 --table-dir data/modpoly/weber_f \
  --trace-cap 64 --sea-threads 1
```

Level 5 supplied the exact residue `t = 3 (mod 5)`.  The independently
generated classical `Phi_7` specialization was square-free with uniform
irreducible factor degree `r=4`, proving the two allowed Atkin residues at
level 7.  The exact-only and effective Hasse counts were:

```text
exact-only = 252982212813470346559911483554617482697564411146017346148600388
effective  =  72280632232420099017117566729890709342161260327433527471028682
```

This removes exactly `5/7` of the ambiguity, a factor of `3.5` or
`1.807354922` bits, in 0.34 seconds wall time for both levels on the Apple M4.
It is not a complete count or a search-speedup claim.  Only levels 5 and 7 are
enabled, and a trace-cap-one run still requires exact Elkies uniqueness.

## Exact Weber 24th-root orbit reuse

Every nonzero term `X^a Y^b` in the 77 authenticated Weber tables through
level 401 satisfies

```text
a + ell*b = ell + 1 (mod 24).
```

Consequently, for `zeta^24=1`,

```text
Phi_ell(zeta*f, zeta^ell*y) = zeta^(ell+1) Phi_ell(f,y).
```

The production path now verifies this covariance from the loaded table,
groups source lifts by `f^24`, evaluates and factors one specialization per
orbit, and transports its roots by `y -> zeta^ell*y`.  If the identity is
disabled or cannot be verified, the old exact per-lift path remains active.
The CLI exposes `--root-orbit-reuse 0|1` solely for an explicit ablation.

Two interleaved runs of the same `p125` curve through level 193 used the same
binary and ten-thread configuration.  Both modes emitted identical canonical
mathematical projections with SHA-256
`2f498e0079d09a4433815c528affe5f868ce8466ed0ca7cf71a7192919b5ba77`.

| Mode | Wall runs | Modular-root runs | Root evaluations |
|---|---:|---:|---:|
| Exact per lift | 154.52, 154.62 s | 125.714, 125.871 s | 504 |
| Verified orbit reuse | 64.88, 65.76 s | 36.207, 36.825 s | 42 |

The median wall speedup was 2.366x and the mean modular-root speedup was
3.445x.  Eigenvalue time stayed at 24.5--24.8 seconds, confirming that the
change affected the intended stage.

The stronger replay used retained production global index 4.  All 64
per-level projections matched the old production evidence exactly, including
31 exact residues, accumulated moduli, Atkin state, and the final 15 trace
candidates.  Modular-root time fell from 1,252.390 to 207.917 seconds (6.023x),
while eigenvalue time changed only from 145.323 to 148.418 seconds.  The full
optimized SEA invocation took 377.42 seconds, versus 1,418.823 seconds of SEA
in the retained baseline (3.759x).  This replay performed 192 root evaluations
and reused 2,112 source lifts.

The tested implementation is commit `b41311a`, with clean binary SHA-256
`ba10fe7f7887e98d67e704e5322d740c0345122899e3faff339e2066a02fde48`.
The compact benchmark record, commands, identities, raw-output hashes, and
canonical comparison hashes are retained in
`artifacts/local/p125-weber-root-orbits-20260801/result.json`.

## Exact conjugate Frobenius eigenvalue reuse

At an Elkies prime, the Frobenius eigenvalues satisfy

```text
lambda * mu = p (mod ell).
```

After one validated kernel supplies `lambda`, a second distinct rational
degree-`ell` isogeny has the forced eigenvalue
`mu=(p mod ell)*lambda^-1 mod ell`.  The optimized path retains complete BMSS
rational-map validation for every kernel and performs the quotient-ring
Frobenius recovery for the first kernel only.  If Frobenius is scalar then
`lambda=mu` is valid on every stable line; more than two distinct stable lines
in the non-scalar case hard-fail.  The lower-level CLI exposes
`--conjugate-eigenvalue-reuse 0|1` as an exact ablation.

The same binary was run with reuse off and on for the level-193 `p125` slice:

| Mode | Wall | Eigenvalue stage | Independent / derived |
|---|---:|---:|---:|
| Independent recovery | 62.44 s | 23.494 s | 42 / 0 |
| Conjugate reuse | 47.64 s | 9.644 s | 21 / 21 |

All 42 canonical level records were identical with SHA-256
`22832ac4528da2264c6712d8853ecd7d78933e2bc1260b061d24d0ed7c9f5b0f`.
The eigenvalue speedup was 2.436x and the wall speedup was 1.311x.

The full production index-4 replay resolved 61 kernels as 31 independent
recoveries plus 30 derivations.  All 64 canonical level records and final 15
trace candidates again matched the retained production evidence, with SHA-256
`7c4a5c85ac716d6d8a3ea11b588381c79c0d8c5909b81273ef22802397d30ecb`.
Eigenvalue time fell from the orbit-only 148.418 seconds to 70.196 seconds
(2.114x), and invocation wall fell from 377.42 to 291.43 seconds (1.295x).
Against the pre-orbit production SEA time of 1,418.823 seconds, the combined
exact improvements give a 4.868x comparison.

The implementation is commit `164814b`.  The measured incremental binary
SHA-256 was
`476958f77860e0ed4a65c5d08eda82c8751651c8a6548d2b85f8a85d3a356d62`;
the subsequent clean release build that passed `make test-all` hashes to
`83392eb8d362e21e894a0fad3c38f77c9ec68ca9264d124e2695e20f673c1421`.
The compact commands, identities, timing fields, and raw-output hashes are in
`artifacts/local/p125-conjugate-eigenvalue-20260801/result.json`.

## Tractable Weber/classical-j boundary

The only checked-in levels shared by the production Weber path and the
classical-j BMSS reference are 5 and 7.  The clean release binary above was
run 20 times per mode on retained production index 4.  Both implementations
returned exactly two kernels and trace residue 2 at both levels.

| Level | Classical-j wall (20) | Weber-f wall (20) | Weber/classical |
|---:|---:|---:|---:|
| 5 | 0.49 s | 6.10 s | 12.449x slower |
| 7 | 0.56 s | 6.15 s | 10.982x slower |

The negative result is expected at these tiny levels: exhaustive Weber
source-lift discovery dominates, whereas the classical specialization is
already known.  It must not be inverted into a speed claim from table size.
The production justification is that Weber supplies 77 authenticated sparse
levels and admits the measured orbit and eigenvalue optimizations; the
repository has no corresponding classical-j production schedule.  Exact
curve identity, commands, table sizes, and interpretation are retained in
`artifacts/local/p125-weber-classical-lowlevel-20260801/result.json`.

## Measured prime scheduling

Commit `b684461` adds an opt-in `sea-weber-count` schedule that ranks a strict,
complete profile by measured information/cost using exact cross-products.  It
is benchmark-only; production search retains increasing order.

The 42-level profile through 193 trained information on unique production
indices 0--7 and current optimized cost on indices 4 and 8.  A disjoint
held-out `p125` curve was run in the order increasing, scored, scored,
increasing, using the same binary and all exact hot-path optimizations.

| Schedule | Wall runs | Median wall | Median user |
|---|---:|---:|---:|
| Increasing | 58.57, 57.32 s | 57.945 s | 49.805 s |
| Expected information/cost | 57.94, 58.78 s | 58.36 s | 50.15 s |

The scored order was 0.716% slower.  All four final constraints and all sorted
intrinsic/operation-count records were identical, so this is a negative
performance result rather than a correctness difference.  The increasing
production default remains justified.  The profile, exact order, curve,
binary identity, equality projections, and raw hashes are in
`artifacts/local/p125-prime-schedule-20260801/result.json`.

## Thresholded Karatsuba and rolling curve scaling

The next frozen evaluation combined commit `f8347c6`'s shared-cache rolling
curve coordinator with a threshold-32 exact Karatsuba convolution backend.
The evaluated executable SHA-256 was
`01d9cabac89660094545c7cd42b83b1c6d06d74b5c979aec3d52ea14a01eee0e`.

The isolated degree-194 kernel result was 2.70x for raw multiplication, 1.53x
for `mulmod` and `squaremod`, and 1.554x for quotient Frobenius.  Current-path
serial replays reduced warm work on indices 8 and 9 from 329.264/254.163 to
110.340/90.010 seconds, while their complete canonical mathematical hashes
remained identical to retained production.

The same frozen executable then scaled one initial window across the ten-core
M4:

| Curve slots | SEA threads/curve | Smooth threads/curve | Warm seconds/curve | Peak RSS |
|---:|---:|---:|---:|---:|
| 1 | 10 | 8 | 100.175 | 6.02 GB |
| 2 | 5 | 4 | 57.800 | 6.29 GB |
| 3 | 3 | 2 | 40.501 | 5.79 GB |
| 5 | 2 | 1 | 34.185 | 6.39 GB |
| 10 | 1 | 1 | 27.071 | 6.19 GB |

All overlapping curve projections match across modes.  The K=10 window
recorded 68.9 MiB of system-wide swapouts, already reflected in its wall time.
Concurrency therefore stops at the ten physical cores and remains subject to
long-run memory telemetry.  Commands, source and executable identities,
per-curve timings, canonical hashes, raw hashes, and `vm_stat` deltas are in
`artifacts/local/p125-curve-parallel-20260801/result.json`.
This was a pre-commit scaling evaluation, not a production identity: its
observations at indices 12--17 do not advance the authenticated production
cursor and the final committed worker must begin again at 12.

A subsequent two-line exact cleanup removed repeated `mpz_class` coefficient
copies from the quadratic quotient/reduction loops.  Five interleaved
degree-194 quotient-Frobenius pairs improved from 1.183812 to 0.964617 seconds
median (1.227x) with identical outputs.  It postdates the frozen scaling
binary, so the table above does not project or multiply that gain.

## Bounded X1(11) family probe

The pinned MIT X1(11) equation was exercised on `p125`, seed
`202607300000`, indices `[0,16)`, with a 64-x-coordinate bound per index.
The full-`E[2]` mode admitted 13 indices from 411 x samples in 5.296 seconds.
Requiring a point of order four admitted 7 indices from 817 x samples in
11.176 seconds.  No sample failed the nonsingularity, exact-order-11, or full-
`E[2]` validation gates.  Magma 2.29-1 independently confirmed the retained
order divisibility by 44/88 and the Tate-point and Montgomery identities.

This measurement is generation-only and did not advance the production
cursor.  The construction is now available as an explicit schedule-bound
search family, but an end-to-end same-build K=10 comparison remains the gate
before selecting it for the long run.  Exact commands, counters, formula and
binary identities, Magma orders, and raw hashes are in
`artifacts/local/p125-x1-11-probe-20260801/result.json`.

## Exact-smooth cross-curve microbatch follow-up

A later no-delay coordinator grouped exact-smooth requests that arrived during
an active primorial scan. It passed correctness, concurrency, checkpoint, and
adversarial review gates, then failed the same-range p125 performance gate.
Across the balanced B1/A1/A2/B2 order on indices `[435,445)`, the frozen
baseline mean was 218.845 seconds and the microbatch mean was 276.905 seconds.
All non-timing outcomes matched, but microbatch throughput was only
`0.790325202x` baseline. The source was removed and production remained on
the frozen implementation. See [the full gate](exact_smooth_microbatch_ab.md)
and `artifacts/local/p125-exact-smooth-microbatch-20260802/result.json`.
