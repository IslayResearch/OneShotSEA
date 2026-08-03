# OneShotSEA: the direct-first SEA research branch

This branch contains a custom SEA point-counting path for the one-shot
primality-proof search. Its immediate target is the 416-bit prime
`nextprime(10^125)` and then larger targets such as `nextprime(10^130)`.

The deliverable here is the point-counting and search machinery needed to run
that experiment. It is not yet a p125 certificate, a general-purpose SEA
library, or a measured proof of the SEA-versus-CM crossover.

Some CM-search and Weber code is inherited from the parent project. On this
branch it is support machinery—for modular-polynomial construction, fallback,
and differential validation—not the feature being proposed for review.

## What this branch implements

```text
authenticated prepared context
    -> specialized classical Phi_ell(j,Y)
    -> exact Elkies residue or certified Atkin trace set
    -> retained CRT state
    -> measured early-abort / cap-one policy
    -> Weber continuation only where still needed
    -> sound smoothness rejection or ordinary certificate gates
```

The SEA path is native C++; it does not call PARI/GP, Magma, Sage, or a
general-purpose SEA implementation during a search. The branch adds:

- specialized classical modular-polynomial construction using CM isogeny
  surfaces, Vélu quotients, interpolation, coefficient bounds, and centered
  CRT;
- exact Elkies recovery with normalized-codomain and BMSS checks;
- certified Atkin trace sets with factored CRT counting and bounded
  meet-in-the-middle enumeration;
- retained direct constraints across Weber fallback and the cap-N-to-cap-one
  transition;
- an optional pre-smooth direct suffix, so a small multi-trace set can be made
  exact before repeatedly scanning the smoothness cache;
- streaming authenticated context caches with canonical encoding,
  mathematical witnesses, bounded resident memory, and ordered schedules; and
- an AWS preparation and worker path that binds the deployed commit, binary,
  prime, schedule, construction limits, cache digest, and command.

Resource limits fail closed. Running out of a table, candidate budget, memory
budget, or construction budget is never reported as a mathematical rejection.

## Current evidence

The retained p125 artifacts exercise the same proof state used by the search,
while independent point counters are used only as validation oracles.

| Property under test | Retained result |
| --- | --- |
| Complete specialized SEA trace | A 30-level run through `ell=271` matched every authenticated table-backed residue and an independent Magma trace. [Direct-trace audit](artifacts/local/p125-direct-trace-777e293-20260803/README.md) |
| Direct-to-Weber composition | Fifteen direct levels were retained into Weber continuation; the reconstructed trace matched the table-backed route and PARI/GP. [Hybrid A/B audit](artifacts/local/p125-direct-first-hybrid-20260803/README.md) |
| Sound cap-16 screening | A measured 20-level prefix completed four fixed screenings, including one curve on which Weber-only exhausted its usable levels; every set contained the independent PARI trace. [Four-curve cohort](artifacts/local/p125-direct-first-cohort-20260803/README.md) |
| Certified cap-one completion | The difficult cohort curve stopped at the certified effective singleton at `ell=379`, recovered the PARI trace, and reached sound smoothness screening. [Singleton audit](artifacts/local/p125-certified-atkin-singleton-20260803/README.md) |
| Deferred direct suffix | Levels 89/97 were absent from four ordinary p125 rejections, but cap-one replays preserved all three independent traces and shortened Weber continuation. [Deferred-suffix audit](artifacts/local/p125-cap-one-direct-tail-20260803/README.md) |
| Pre-smooth suffix promotion | On the same four curves, five cached suffix evaluations reduced smoothness inputs from 46 orders to 8, resolved every multi-trace set to the independent PARI trace, and reduced the single-run summed total by 7.17%. [Pre-smooth A/B](artifacts/local/p125-pre-smooth-direct-tail-20260803/README.md) |
| Bounded Atkin combiner | A 240-record differential A/B was identical while improving direct evaluation by 10.96x and peak RSS by 28.71x. [Atkin MITM audit](artifacts/local/p125-direct-atkin-mitm-20260803/README.md) |

These results validate the implementation path and its proof-state composition.
They do not establish certificate yield or a practical crossover against the
CM search.

## Validate this branch

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

The retained performance/proof-state audits are checked with:

```sh
/usr/bin/make test-performance-artifacts
/usr/bin/make test-p125-direct-trace
```

The native suite does not require Magma. `/usr/bin/make test-all` also runs the
optional independent point-count oracle and needs `MAGMA=/path/to/magma` (or
`magma` on `PATH`).

## Run the branch-specific path

Preparation is curve-independent and can be amortized over a cohort. This is
the current measured 20-level screening prefix plus a two-level cap-one suffix:

```sh
LEVELS=7,5,11,13,19,17,23,29,31,37,41,43,47,53,67,71,79,61,73,59,89,97

./build/oneshotsea prepare-classical-direct-context \
  --p P \
  --classical-direct-levels "$LEVELS" \
  --classical-direct-max-prime-candidates 10000000 \
  --classical-direct-max-x-candidates 1000000 \
  --classical-direct-context-max-file-bytes 1000000000 \
  --sea-threads 1 \
  --output runs/direct.ctx
```

Record the emitted SHA-256 through a trusted channel, then search with the
same ordered schedule and construction limits:

```sh
./build/oneshotsea search \
  ...ordinary search arguments... \
  --sea-strategy direct-first \
  --trace-cap 16 \
  --sea-threads 1 \
  --classical-direct-levels "$LEVELS" \
  --classical-direct-cap-one-tail-count 2 \
  --classical-direct-pre-smooth-tail-min-traces 2 \
  --classical-direct-max-prime-candidates 10000000 \
  --classical-direct-max-x-candidates 1000000 \
  --classical-direct-context-cache runs/direct.ctx \
  --classical-direct-context-sha256 TRUSTED_SHA256 \
  --classical-direct-context-max-file-bytes 1000000000 \
  --classical-direct-cache-resident-bytes 1000000000
```

The level order, suffix boundary, and pre-smooth threshold are measured policy
inputs and part of checkpoint identity. Set the pre-smooth threshold to zero to
retain the conservative policy that tests every cap-N trace before attempting
the suffix. Direct SEA remains opt-in; no launcher silently selects this
schedule.

The operational contracts are in [direct context
caches](docs/direct_context_cache.md), [search
integration](docs/search_pipeline.md), and the [AWS operator
guide](docs/aws.md).

## Why expert review is worth the time

This is now reviewable proof code rather than a design proposal. The custom
producer reaches the real search pipeline, certified Elkies and Atkin state
survives fallback and trace-cap transitions, fixed 416-bit traces complete,
independent oracle results are retained, and prepared contexts have a
deterministic authenticated boundary.

The highest-value review questions are concrete:

1. Are the auxiliary-prime selection and suitable-order claims sufficient?
2. Do the ring-class resultants and CM-surface checks authenticate every
   specialization?
3. Do Vélu enumeration, normalization, interpolation, and coefficient bounds
   justify the reconstructed polynomial data?
4. Are exact and set-valued trace constraints composed soundly through early
   abort, the deferred suffix, Weber continuation, and cap-one recovery?
5. Is a cached construction mathematically and operationally equivalent to a
   fresh construction with the same declared inputs?

Those are bounded proof obligations with retained witnesses and differential
evidence, which is why Drew's review is worth the time now. A positive review
would justify a larger p125/p130 yield experiment; it would not, by itself,
validate the yield heuristic, the finite-size crossover, or unbounded scaling.

## Scope of the asymptotic claim

The proposed `p^(1/8+o(1))` term is the heuristic number of curves searched to
encounter the smooth divisor needed by a one-shot certificate. It is not the
cost of one SEA point count. Under the stated small-level and auxiliary-prime
assumptions, per-curve SEA work is polynomial in `log p` and is absorbed into
the `o(1)` term.

This branch has the intended decomposition, but finite p125 measurements do
not prove that asymptotic behavior. Context reuse, retained state, and early
abort improve constants and rejected-curve cost; they do not change the outer
heuristic exponent. The assumptions and current 64-bit auxiliary-field and
fixed-selector qualifications are listed in [asymptotic
scope](docs/asymptotic_scope.md).

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
  early-abort policy, concurrency, telemetry, and checkpoint identity.
- [`scripts/aws/`](scripts/aws/): authenticated cache preparation and immutable
  worker launch.

The specialized-modular-polynomial direction follows [Bröker, Lauter, and
Sutherland](https://arxiv.org/abs/1001.0402) and
[Sutherland](https://arxiv.org/abs/1202.3985). Reproducible commands, inputs,
outputs, and digests live under [`artifacts/local/`](artifacts/local/).
