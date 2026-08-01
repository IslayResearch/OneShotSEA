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
