# OneShotSEA: specialized SEA for one-shot proofs

This branch is a reviewable research checkpoint for one question: can a custom
SEA point counter make one-shot primality-proof searches practical beyond 400
bits?

The work here is the `direct-first` SEA path. It constructs only the
specialized classical modular-polynomial data needed for the current curve,
uses it to produce certified Elkies or Atkin trace constraints, and retains
those constraints when the search continues through the existing Weber path.
Prepared direct contexts can now be authenticated, reused, and bound into an
immutable AWS worker invocation.

This is not a completed p125 proof or a measured SEA-versus-CM crossover. It is
the implementation and validation foundation needed before spending serious
compute on those questions.

## Branch scope

The review target is:

```text
authenticated direct-level context
    -> specialized classical-j SEA
    -> exact Elkies residue or certified Atkin trace set
    -> retained CRT constraints
    -> Weber continuation for uncovered levels
    -> sound smoothness rejection or ordinary certificate gates
```

The branch adds or materially changes:

- A native C++ producer for `Phi_ell(j,Y)` and its specialized `X` derivative.
  It uses CM isogeny surfaces, Vélu quotients, interpolation, and centered CRT;
  it does not call PARI/GP, Magma, Sage, or another SEA implementation.
- Exact Elkies recovery with normalized-codomain and BMSS checks.
- Certified Atkin trace sets with factored CRT counting and bounded
  meet-in-the-middle enumeration.
- A `direct-first` search policy that keeps valid direct constraints across
  Weber fallback and the cap-N to cap-one transition. Retained state is bound
  to the exact curve, so same-`j` twists cannot share it.
- Streaming direct-context caches with canonical encodings, mathematical
  witnesses, trusted SHA-256 identity, bounded resident memory, and ordered
  level schedules.
- An authenticated AWS preparation and launch path. Deployment commit, binary,
  prime, level order, construction caps, thread count, cache size, digest, and
  exact preparation command are checked before a worker starts.
- Sound early rejection: an exhausted resource cap remains an implementation
  limit and is never reclassified as a mathematical rejection.

The repository's older CM-based one-shot route and Weber tables remain useful
as comparison points and fallback machinery, but they are not the feature
under review here. The use of CM data to construct a specialized modular
polynomial is also distinct from using the CM approach to find the one-shot
certificate curve.

## Current status

What has been demonstrated at the 416-bit p125 target:

- A 30-level direct run through `ell=271` reconstructed one trace. Every
  direct residue matched the authenticated table-backed route, and an
  independently retained Magma point count matched the result. See the
  [direct-trace evidence](artifacts/local/p125-direct-trace-777e293-20260803/README.md).
- On one fixed curve, 15 direct levels were retained into Weber continuation.
  Weber usage fell from 70 to 50 levels, SEA time improved by 1.297x, and total
  curve time improved by 1.144x. A separate table-backed run and PARI/GP count
  reconstructed the same trace. See the
  [hybrid A/B](artifacts/local/p125-direct-first-hybrid-20260803/README.md).
- In a four-curve cap-16 cohort, Weber-only failed closed on one curve after
  exhausting all usable levels. A measured 20-level direct schedule completed
  all four screenings with candidate counts 1, 13, 4, and 5; independent PARI
  counts were contained in every retained set. See the
  [cohort evidence](artifacts/local/p125-direct-first-cohort-20260803/README.md).
- On the cohort's difficult curve at cap one, the old policy continued past a
  certified effective singleton and exhausted the table. The audited policy
  stops at `ell=379`, avoids four Weber levels, recovers the independently
  counted PARI trace, and reaches sound smoothness screening while reporting
  221,262 exact-only candidates separately. See the
  [certified-singleton evidence](artifacts/local/p125-certified-atkin-singleton-20260803/README.md).
- A 240-record Atkin A/B produced identical results while improving direct
  evaluation by 10.96x and peak RSS by 28.71x. See the
  [Atkin evidence](artifacts/local/p125-direct-atkin-mitm-20260803/README.md).

These are implementation, coverage, and differential-validation results. They
are not a certificate-yield study. In particular, this branch has not yet:

- found a one-shot certificate for `nextprime(10^125)` or a larger target;
- measured selected-schedule throughput and yield over a production cohort;
- established the practical crossover against the CM search; or
- removed the current 64-bit auxiliary-field and fixed-selector qualifications
  from the asymptotic discussion.

## Validate the checkpoint

The native build needs a C++20 compiler and GMP.

```sh
/usr/bin/make -j4
/usr/bin/make \
  test-direct-modpoly \
  test-prime-isogeny \
  test-cm-surface \
  test-atkin \
  test-search-checkpoint \
  test-search-pipeline \
  test-cli \
  test-aws \
  test-yield-model
```

The native suite does not require Magma. `/usr/bin/make test-all` also runs the
optional independent point-count oracle and needs `MAGMA=/path/to/magma` (or
`magma` on `PATH`).

For the short fixed-p125 direct-path check:

```sh
/usr/bin/make test-p125-direct-trace
```

For the retained complete direct schedule:

```sh
./build/validate_p125_direct_trace --threads 4
```

## Run direct-first locally

Preparation is curve-independent and can be amortized over a search cohort:

```sh
./build/oneshotsea prepare-classical-direct-context \
  --p P \
  --classical-direct-levels 7,5,11,13,19,17 \
  --classical-direct-context-max-file-bytes 8589934592 \
  --sea-threads 4 \
  --output runs/direct.ctx
```

Record the emitted SHA-256 through a trusted channel, then supply the same
ordered schedule and construction limits to the search:

```sh
./build/oneshotsea search \
  ...ordinary search arguments... \
  --sea-strategy direct-first \
  --sea-threads 4 \
  --classical-direct-levels 7,5,11,13,19,17 \
  --classical-direct-context-cache runs/direct.ctx \
  --classical-direct-context-sha256 TRUSTED_SHA256 \
  --classical-direct-context-max-file-bytes 8589934592
```

The level order is a measured policy input and part of checkpoint identity,
not merely a set. Direct SEA remains opt-in; no launcher chooses a production
schedule automatically.

For the complete cache contract and AWS workflow, see
[direct context caches](docs/direct_context_cache.md),
[search integration](docs/search_pipeline.md), and the
[AWS operator guide](docs/aws.md).

## Asymptotic claim

The proposed `p^(1/8+o(1))` term is the heuristic number of curves needed to
encounter the smooth divisor required by a one-shot certificate. It is not the
cost of one SEA point count. With the usual small-level and auxiliary-prime
assumptions, per-curve SEA work is polynomial in `log p` and is absorbed into
the `o(1)` term.

This implementation has the intended decomposition, but finite p125 timings do
not prove unbounded scaling. Cache reuse and early abort improve constants and
average rejected-curve work; neither changes the outer heuristic exponent.
The derivation and its implementation qualifications are in
[asymptotic scope](docs/asymptotic_scope.md).

## Why expert review is worth the time

Review can now test concrete proof obligations rather than a proposed design.
The custom producer reaches the real search path, direct and Atkin state
survives fallback, fixed 416-bit traces complete, independent point counts are
retained, and prepared contexts have a deterministic authenticated boundary.

The highest-value review questions are:

1. Are auxiliary-prime selection and suitable-order claims sufficient?
2. Are the ring-class resultants and CM-surface authentication sound?
3. Do Velu enumeration, normalization, interpolation, and coefficient bounds
   justify every reconstructed specialization?
4. Are Elkies and Atkin constraints composed soundly across retained state?
5. Is a cached construction mathematically and operationally equivalent to a
   fresh construction with the same declared inputs?

A positive review would validate the direct-SEA foundation and justify larger
p125/p130 searches. It would not, by itself, validate certificate yield, the
CM crossover, or the unbounded heuristic.

## Review map

- [`src/direct_modpoly.cpp`](src/direct_modpoly.cpp): auxiliary primes,
  coefficient bounds, and CRT reconstruction.
- [`src/class_polynomial.cpp`](src/class_polynomial.cpp),
  [`src/prime_isogeny.cpp`](src/prime_isogeny.cpp), and
  [`src/cm_surface.cpp`](src/cm_surface.cpp): ring-class construction, Vélu
  quotients, surface authentication, and specialized interpolation.
- [`src/direct_context_cache.cpp`](src/direct_context_cache.cpp): canonical
  encoding, streaming publication, and authenticated lazy loading.
- [`src/sea.cpp`](src/sea.cpp), [`src/trace.cpp`](src/trace.cpp), and
  [`src/early_abort.cpp`](src/early_abort.cpp): certified trace consumption and
  sound early rejection.
- [`src/search_pipeline.cpp`](src/search_pipeline.cpp): direct-first retained
  continuation, concurrency, telemetry, and checkpoint identity.
- [`scripts/aws/prepare-direct-cache.sh`](scripts/aws/prepare-direct-cache.sh),
  [`scripts/aws/launch-worker.sh`](scripts/aws/launch-worker.sh), and
  [`scripts/aws/remote_worker.py`](scripts/aws/remote_worker.py): immutable
  prepared-context deployment and provenance checks.

The specialized-modular-polynomial direction follows
[Bröker, Lauter, and Sutherland](https://arxiv.org/abs/1001.0402) and
[Sutherland](https://arxiv.org/abs/1202.3985). Detailed proof contracts are in
[`docs/`](docs/); retained benchmark provenance lives beside each artifact in
[`artifacts/local/`](artifacts/local/).
