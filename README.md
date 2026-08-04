# OneShotSEA: direct-first p125 branch

This branch develops a custom SEA path for finding short one-shot primality
proofs, with `nextprime(10^125)` (416 bits) as the current engineering target.
It contains a native point counter, specialized classical modular-polynomial
construction, sound early-abort policies, authenticated prepared contexts,
and retained p125 validation evidence.

It does **not** yet contain a p125 certificate, a certificate-yield study, a
measured SEA/CM crossover, or an unbounded implementation of the proposed
asymptotic algorithm. Those are the next experimental and engineering gates,
not claims made by this branch.

## Implemented path

```text
authenticated direct-level context
    -> specialize Phi_ell(j,Y)
    -> certify an Elkies residue or complete Atkin trace set
    -> combine direct constraints with sound early screening
    -> optionally continue through deferred direct and Weber levels
    -> test the resulting curve/twist orders at the ordinary proof gates
```

The production path is project-native C++20 and does not call PARI/GP, Magma,
Sage, or another external CAS/SEA implementation. Independent point counters
appear only in retained validation artifacts.

This branch includes:

- specialized classical modular-polynomial construction from auxiliary
  fields, CM isogeny surfaces, Vélu quotients, interpolation, and centered CRT;
- authenticated, bounded-residency prepared contexts whose mathematical
  inputs and construction witnesses are part of the search identity;
- exact Elkies recovery and certified Atkin factor-degree, projective-order,
  and trace-set recovery;
- reusable Frobenius evidence and composition plans, a monomial-aware
  fixed-base window, and hybrid quotient reduction for the degree-80--90 hot
  path;
- retained set-valued CRT state across cap-N screening, deferred/pre-smooth
  suffix levels, cap-one recovery, and Weber continuation;
- sound smoothness rejection and ordinary certificate gates; and
- immutable preparation/worker commands that bind source, binary, prime,
  schedule, construction limits, and cache digest.

All resource limits fail closed. Exhausting a table, candidate limit,
construction budget, or memory budget is never reported as a mathematical
rejection.

## Build and validate

The native build requires a C++20 compiler and GMP.

```sh
/usr/bin/make -j4
/usr/bin/make \
  test test-factor test-atkin test-direct-modpoly test-cm-surface \
  test-search-pipeline test-search-checkpoint test-cli test-aws
```

Retained proof-state and performance artifacts have executable audits:

```sh
/usr/bin/make test-performance-artifacts
/usr/bin/make test-p125-direct-trace
```

The native suite does not require Magma. `/usr/bin/make test-all` additionally
runs an optional independent point-count oracle and needs
`MAGMA=/path/to/magma` or a `magma` executable on `PATH`.

Preparation is curve-independent and should be amortized over a cohort. See
the [direct-context guide](docs/direct_context_cache.md) for authenticated
cache construction, the [search guide](docs/search_pipeline.md) for the
current direct-first invocation and policy controls, and the
[AWS guide](docs/aws.md) for deployment. Direct-first SEA is opt-in; no
launcher silently chooses its level schedule or early-abort policy.

## Current checkpoint

The retained p125 evidence now covers four related boundaries:

- **Point-count correctness.** Thirty independently prepared direct levels
  through `ell=271` reconstruct one exact 416-bit trace. A separate four-curve
  production replay preserves all exact traces against independent PARI/GP
  counts. See the [complete-trace audit](artifacts/local/p125-direct-trace-777e293-20260803/README.md)
  and [cohort audit](artifacts/local/p125-direct-first-cohort-20260803/README.md).
- **Early abort and continuation.** Certified Atkin singleton recovery,
  cap-one deferred levels, and a pre-smooth suffix preserve every independent
  trace while reducing unnecessary continuation work. See the
  [singleton](artifacts/local/p125-certified-atkin-singleton-20260803/README.md),
  [deferred-suffix](artifacts/local/p125-cap-one-direct-tail-20260803/README.md),
  and [pre-smooth](artifacts/local/p125-pre-smooth-direct-tail-20260803/README.md)
  audits.
- **Quotient-ring arithmetic.** Batched interpolation, Frobenius-evidence
  reuse, fixed-inner composition, the specialized `X` window, and hybrid
  quotient reduction all pass exact differential and production-replay gates.
  The latest [hybrid-reduction audit](artifacts/local/p125-direct-hybrid-reduction-20260803/README.md)
  links the preceding arithmetic evidence.
- **Bounded trace combination.** A 240-record differential preserves exact
  output while improving the bounded Atkin combiner by 10.96x in evaluation
  time and 28.71x in peak RSS. See the
  [combiner audit](artifacts/local/p125-direct-atkin-mitm-20260803/README.md).

The latest arithmetic change replaces part of dense degree-90 long reduction
with a 48-coefficient reciprocal prefix. In an interleaved degree-90/degree-79
B/A/A/B control it reduces the normalized CPU ratio by 3.949% and the wall
ratio by 3.911%. The exact four-curve production replay is unchanged. Because
an unrelated generation control drifted in the broader profiler bracket, this
is an isolated hot-path result, not a claimed cohort or end-to-end speedup.

Together these runs validate the implementation and composition of its proof
state. They do not measure certificate yield or establish the practical
crossover against the CM search.

## Why expert review is worthwhile

The implementation is now concrete enough for expert review to resolve
bounded mathematical questions rather than review a proposed architecture:
the specialized producer reaches the real search, fixed 416-bit traces
complete, exact and set-valued constraints survive every continuation path,
independent oracle results are retained, and prepared contexts have a
deterministic authenticated boundary.

A positive review would justify Drew's time only insofar as it unlocks the
next expensive step: a larger p125/p130 certificate-yield experiment. It would
not by itself validate the yield heuristic, finite-size crossover, or
unbounded scaling. The precise questions, source map, and evidence map are in
the [direct-first review guide](docs/review_guide.md).

## Asymptotic boundary

The proposed `p^(1/8+o(1))` term describes the heuristic number of curves
needed to obtain the required smooth divisor, not the cost of a single SEA
point count. Under the small-level and auxiliary-prime assumptions, per-curve
SEA work is polynomial in `log p` and is absorbed into the `o(1)` term.

This branch has that intended decomposition, but its finite schedule, fixed
selector, and 64-bit auxiliary-field ceiling prevent an unqualified
asymptotic claim. Early abort, context reuse, interpolation batching, and the
Frobenius/quotient optimizations are constant-factor changes; none changes the
outer exponent. See [asymptotic scope](docs/asymptotic_scope.md) for the full
claim and the remaining validation gates.

## Documentation map

- [As-built SEA design](docs/sea_design.md)
- [Direct context caches](docs/direct_context_cache.md)
- [Search pipeline and current invocation](docs/search_pipeline.md)
- [Expert review guide](docs/review_guide.md)
- [Asymptotic scope](docs/asymptotic_scope.md)
- [Dependency and bottleneck registry](docs/bottleneck_registry.md)
- [Retained local artifacts](artifacts/local/)

The specialized modular-polynomial design follows
[Bröker, Lauter, and Sutherland](https://arxiv.org/abs/1001.0402) and
[Sutherland](https://arxiv.org/abs/1202.3985).
