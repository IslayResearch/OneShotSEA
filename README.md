# OneShotSEA: the direct-first SEA branch

This branch implements and validates a custom SEA point-counting path for the
one-shot primality-proof search. Its purpose is narrow: make rejected curves
cheap enough, and surviving traces precise enough, to test the proposed SEA
search beyond 400 bits.

The older CM certificate search and the Weber implementation are still present
in the repository. They are comparison and fallback machinery; they are not the
feature under review on this branch.

## What this branch contains

```text
authenticated prepared context
    -> specialized classical Phi_ell(j,Y)
    -> exact Elkies residue or certified Atkin trace set
    -> retained CRT state
    -> Weber continuation only where still needed
    -> sound smoothness rejection or ordinary certificate gates
```

The implementation includes:

- a native C++ producer for the specialized classical modular-polynomial data
  needed by the current curve, using CM isogeny surfaces, Velu quotients,
  interpolation, and centered CRT;
- exact Elkies recovery with normalized-codomain and BMSS checks;
- certified Atkin trace sets with factored CRT counting and bounded
  meet-in-the-middle enumeration;
- a `direct-first` policy that retains certified direct constraints across
  Weber fallback and the cap-N to cap-one transition;
- streaming, authenticated prepared-context caches with a canonical encoding,
  mathematical witnesses, bounded resident memory, and ordered schedules;
- AWS preparation and worker launch scripts that bind the deployed commit,
  binary, prime, schedule, construction limits, cache digest, and command; and
- fail-closed early aborts: resource exhaustion is never reported as a
  mathematical rejection.

The SEA path is implemented here rather than delegated to PARI/GP, Magma, Sage,
or another general-purpose SEA implementation. Independent systems are used as
oracles in retained validation artifacts, not in the search hot path.

## What is working now

At the 416-bit `nextprime(10^125)` target, this branch has demonstrated:

- **Complete direct trace recovery.** A 30-level run through `ell=271`
  reconstructed a unique trace, with every residue matching the authenticated
  table-backed route and the final trace matching an independently retained
  Magma count. See the
  [direct-trace audit](artifacts/local/p125-direct-trace-777e293-20260803/README.md).
- **Direct-to-Weber retained continuation.** On a fixed curve, 15 direct levels
  reduced Weber use from 70 to 50 levels; a separate table-backed run and
  PARI/GP recovered the same trace. See the
  [hybrid A/B audit](artifacts/local/p125-direct-first-hybrid-20260803/README.md).
- **Sound cap-16 coverage.** A measured 20-level direct schedule completed all
  four screenings in a fixed cohort, including a curve on which Weber-only
  exhausted its usable levels. Independent PARI traces were contained in every
  retained set. See the
  [four-curve cohort](artifacts/local/p125-direct-first-cohort-20260803/README.md).
- **Certified Atkin singleton completion.** On the difficult cohort curve, the
  search now stops at `ell=379` when the certified effective set becomes a
  singleton, recovers the PARI trace, and reaches sound smoothness screening.
  It reports the 221,262 exact-only candidates separately. See the
  [singleton audit](artifacts/local/p125-certified-atkin-singleton-20260803/README.md).
- **A faster bounded Atkin combiner.** A 240-record A/B produced identical
  results while improving direct evaluation by 10.96x and peak RSS by 28.71x.
  See the
  [Atkin MITM audit](artifacts/local/p125-direct-atkin-mitm-20260803/README.md).

These results validate the implementation path and its proof-state composition.
They do **not** yet establish certificate yield or a practical crossover against
the CM search.

## What is not claimed

This branch has not yet:

- found a one-shot certificate for `nextprime(10^125)` or a larger target;
- measured yield and throughput over a production-sized selected cohort;
- established the finite-size SEA-versus-CM crossover; or
- removed the current 64-bit auxiliary-field and fixed-selector qualifications
  from the asymptotic argument.

It is a reviewable SEA research checkpoint, not a finished general-purpose SEA
library or a completed p125 proof.

## Validate it locally

The native build requires a C++20 compiler and GMP.

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
`magma` on `PATH`). The short retained p125 path is:

```sh
/usr/bin/make test-p125-direct-trace
```

## Run the branch-specific path

Preparation is curve-independent and can be amortized over a cohort:

```sh
./build/oneshotsea prepare-classical-direct-context \
  --p P \
  --classical-direct-levels 7,5,11,13,19,17 \
  --classical-direct-context-max-file-bytes 8589934592 \
  --sea-threads 4 \
  --output runs/direct.ctx
```

Record the emitted SHA-256 through a trusted channel, then search with the same
ordered schedule and construction limits:

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

Level order is a measured policy input and part of checkpoint identity. Direct
SEA remains opt-in; the launcher does not silently choose a production
schedule. The full contracts are in
[direct context caches](docs/direct_context_cache.md),
[search integration](docs/search_pipeline.md), and the
[AWS operator guide](docs/aws.md).

## Why Drew's review is useful now

The branch has crossed the threshold from a design proposal to auditable proof
code: the custom producer reaches the real search pipeline, certified state
survives both fallback and trace-cap transitions, fixed 416-bit traces complete,
independent oracle results are retained, and prepared contexts have a
deterministic authenticated boundary.

The highest-value review questions are therefore concrete:

1. Are auxiliary-prime selection and suitable-order claims sufficient?
2. Do the ring-class resultants and CM-surface checks authenticate each
   specialization?
3. Do Velu enumeration, normalization, interpolation, and coefficient bounds
   justify the reconstructed polynomial data?
4. Are Elkies and Atkin constraints composed soundly across retained state,
   Weber fallback, and the cap-N/cap-one transition?
5. Is a cached construction mathematically and operationally equivalent to a
   fresh construction with the same declared inputs?

A positive review would justify a larger p125/p130 yield experiment. It would
not by itself validate the yield heuristic, the crossover point, or unbounded
scaling.

## Review map

- [`src/direct_modpoly.cpp`](src/direct_modpoly.cpp): auxiliary primes,
  coefficient bounds, and CRT reconstruction.
- [`src/class_polynomial.cpp`](src/class_polynomial.cpp),
  [`src/prime_isogeny.cpp`](src/prime_isogeny.cpp), and
  [`src/cm_surface.cpp`](src/cm_surface.cpp): ring-class construction, Velu
  quotients, surface authentication, and interpolation.
- [`src/direct_context_cache.cpp`](src/direct_context_cache.cpp): canonical
  encoding, streaming publication, and authenticated lazy loading.
- [`src/sea.cpp`](src/sea.cpp), [`src/trace.cpp`](src/trace.cpp), and
  [`src/early_abort.cpp`](src/early_abort.cpp): certified trace consumption and
  sound early rejection.
- [`src/search_pipeline.cpp`](src/search_pipeline.cpp): retained continuation,
  concurrency, telemetry, and checkpoint identity.
- [`scripts/aws/`](scripts/aws/): authenticated cache preparation and immutable
  worker launch.

The asymptotic target is documented separately in
[asymptotic scope](docs/asymptotic_scope.md). Briefly, the proposed
`p^(1/8+o(1))` term is the heuristic number of curves searched, not the cost of
one SEA point count; per-curve polylogarithmic work is absorbed into the
`o(1)` term under the stated assumptions. Cache reuse and early abort improve
constants and rejected-curve cost, not that outer exponent.

The specialized-modular-polynomial direction follows
[Broker, Lauter, and Sutherland](https://arxiv.org/abs/1001.0402) and
[Sutherland](https://arxiv.org/abs/1202.3985). Retained benchmark inputs,
outputs, commands, and digests live under [`artifacts/local/`](artifacts/local/).
