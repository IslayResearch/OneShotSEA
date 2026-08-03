# OneShotSEA

OneShotSEA is a custom, direct-first SEA implementation for searching for
short one-shot primality proofs. The current engineering target is the
416-bit prime `nextprime(10^125)` and, after that, targets such as
`nextprime(10^130)`.

This repository currently contains a working native C++ SEA point-counting
path, its integration into the one-shot search, authenticated prepared
contexts, sound early-abort policies, and retained p125 validation evidence.
It does **not** yet contain a p125 certificate, a certificate-yield study, or
a measured proof of the SEA/CM crossover.

Some CM-surface and Weber code remains because the specialized modular-
polynomial producer uses CM isogeny surfaces and the search can continue with
Weber levels. Those components support the SEA path; they are not a second
project described by this README.

## The implemented path

```text
prepare and authenticate an ordered direct-level context
    -> specialize Phi_ell(j,Y) for the curve
    -> recover an exact Elkies residue or certify an Atkin trace set
    -> retain and combine direct CRT constraints
    -> stop on a sound rejection, or continue through Weber levels
    -> test the resulting curve/twist orders at the ordinary proof gates
```

The search does not invoke PARI/GP, Magma, Sage, or another general-purpose
SEA implementation. Independent point counters appear only in retained
validation artifacts.

The branch implements:

- specialized classical modular-polynomial construction using auxiliary
  fields, CM isogeny surfaces, Vélu quotients, interpolation, coefficient
  bounds, and centered CRT;
- authenticated, bounded-residency context caches whose schedule, inputs,
  witnesses, and mathematical payload are part of the search identity;
- exact Elkies recovery with normalized-codomain and BMSS checks;
- complete polynomial-bound root evidence and certified Atkin factor-degree,
  projective-order, and trace-set recovery;
- fixed-inner modular-composition plans that reuse Frobenius baby powers
  across an Atkin factor proof;
- retained set-valued CRT state across direct levels, cap-N to cap-one
  transitions, optional pre-smooth suffix levels, and Weber continuation;
- sound smoothness rejection and ordinary certificate gates; and
- preparation and worker commands that bind the deployed commit, binary,
  prime, schedule, construction limits, and cache digest.

All resource limits fail closed. Exhausting a table, candidate limit,
construction budget, or memory budget is not reported as a mathematical
rejection.

## Build and validate

The native build requires a C++20 compiler and GMP.

```sh
/usr/bin/make -j4
/usr/bin/make \
  test \
  test-factor \
  test-atkin \
  test-direct-modpoly \
  test-cm-surface \
  test-search-pipeline \
  test-search-checkpoint \
  test-cli \
  test-aws
```

The retained proof-state and performance evidence has executable audits:

```sh
/usr/bin/make test-performance-artifacts
/usr/bin/make test-p125-direct-trace
```

The native suite does not require Magma. `/usr/bin/make test-all` also runs an
optional independent point-count oracle and needs `MAGMA=/path/to/magma` or a
`magma` executable on `PATH`.

## Run the direct-first search

Preparation is curve-independent and should be amortized over a cohort. This
is the current measured 20-level screening prefix followed by a two-level
cap-one suffix:

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
inputs and part of checkpoint identity. Set the pre-smooth threshold to zero
to test every cap-N trace before trying the suffix. Direct-first SEA is
opt-in; no launcher silently selects this schedule.

Operational details are in [direct context caches](docs/direct_context_cache.md),
[search integration](docs/search_pipeline.md), and the [AWS guide](docs/aws.md).

## Current p125 evidence

| What was validated | Retained result |
| --- | --- |
| Complete direct trace | Thirty prepared levels through `ell=271` reconstructed the exact trace and matched authenticated residues plus an independent Magma trace. [Audit](artifacts/local/p125-direct-trace-777e293-20260803/README.md) |
| Direct/Weber composition | Fifteen direct constraints survived Weber continuation and reconstructed the same PARI/GP trace as the table-backed route. [Audit](artifacts/local/p125-direct-first-hybrid-20260803/README.md) |
| Four-curve sound screening | A measured 20-level prefix completed four fixed p125 screenings; every set contained the independent PARI trace. [Audit](artifacts/local/p125-direct-first-cohort-20260803/README.md) |
| Cap-one and deferred suffix | The difficult cohort curve reached a certified singleton, while deferred levels 89/97 preserved all traces and shortened continuation. [Singleton audit](artifacts/local/p125-certified-atkin-singleton-20260803/README.md), [suffix audit](artifacts/local/p125-cap-one-direct-tail-20260803/README.md) |
| Pre-smooth promotion | Five suffix evaluations reduced smoothness inputs from 46 orders to 8 and preserved all four independent traces. [Audit](artifacts/local/p125-pre-smooth-direct-tail-20260803/README.md) |
| Interpolation hot path | Thirty-two real p125 evaluations remained identical while the isolated timer fell 19.52% against bracketing baselines. [Audit](artifacts/local/p125-direct-batched-interpolation-20260803/README.md) |
| Atkin Frobenius reuse | Reusing complete-root evidence removed one duplicate quotient-ring exponentiation per Atkin level; a production replay preserved all four PARI traces. [Audit](artifacts/local/p125-direct-atkin-frobenius-reuse-20260803/README.md) |
| Prepared Frobenius composition | Reusing fixed-inner baby powers removed 420 known table-building multiplications in the four level-89 proofs; an exact B/A/A/B comparison improved the isolated target timer 5.25% despite an adverse generation control, and the production replay preserved all proof semantics. [Audit](artifacts/local/p125-direct-frobenius-composition-20260803/README.md) |
| Bounded Atkin combination | A 240-record differential run was identical while the bounded combiner improved direct evaluation 10.96x and peak RSS 28.71x. [Audit](artifacts/local/p125-direct-atkin-mitm-20260803/README.md) |

These runs validate the implementation and the composition of its proof state.
They do not establish certificate yield or a practical crossover against the
CM search.

## What expert review should decide

This is worth reviewing now because the custom producer reaches the real
search pipeline, fixed 416-bit traces complete, exact and set-valued state
survives every fallback and early-abort transition, independent oracle results
are retained, and prepared contexts have a deterministic authenticated
boundary.

The highest-value questions for Drew are bounded:

1. Do auxiliary-prime selection, ring-class resultants, CM-surface checks,
   interpolation bounds, and CRT reconstruction authenticate each specialized
   modular polynomial?
2. Do Vélu enumeration, codomain normalization, and BMSS checks justify the
   exact Elkies residues?
3. Do complete-root evidence, Frobenius composition, factor degrees,
   projective orders, and residue enumeration justify every Atkin set?
4. Are fixed-inner composition plans lifetime-safe, correctly bounded, and
   exactly equivalent to generic quotient-ring composition?
5. Are direct constraints composed soundly through cap-N screening, the
   deferred suffix, Weber continuation, cap-one recovery, and smoothness
   rejection?
6. Is loading an authenticated prepared context mathematically and
   operationally equivalent to constructing it fresh with the same inputs?

A positive review would justify a larger p125/p130 yield experiment. It would
not by itself validate the yield heuristic, finite-size crossover, or
unbounded scaling.

## Asymptotic scope

The proposed `p^(1/8+o(1))` term is the heuristic number of curves searched to
find the smooth divisor needed by a one-shot certificate. It is not the cost
of one SEA point count. Under the small-level and auxiliary-prime assumptions,
the per-curve SEA work is polynomial in `log p` and is absorbed into the
`o(1)` term.

This implementation has that intended decomposition, but the measurements in
this repository do not prove the asymptotic claim. Context reuse, early abort,
batched interpolation, Frobenius evidence reuse, and prepared composition are
constant-factor improvements; none changes the outer exponent. The current
finite schedule, fixed selector, and 64-bit auxiliary-field ceiling also keep
this from being an unqualified unbounded implementation of the heuristic.
See [the detailed asymptotic scope](docs/asymptotic_scope.md).

## Review map

- [`src/direct_modpoly.cpp`](src/direct_modpoly.cpp),
  [`src/class_polynomial.cpp`](src/class_polynomial.cpp),
  [`src/prime_isogeny.cpp`](src/prime_isogeny.cpp), and
  [`src/cm_surface.cpp`](src/cm_surface.cpp): specialized modular-polynomial
  production and authentication.
- [`src/poly.cpp`](src/poly.cpp), [`src/factor.cpp`](src/factor.cpp),
  [`src/elkies.cpp`](src/elkies.cpp), and [`src/atkin.cpp`](src/atkin.cpp):
  quotient-ring arithmetic and certified Elkies/Atkin recovery.
- [`src/sea.cpp`](src/sea.cpp), [`src/trace.cpp`](src/trace.cpp), and
  [`src/early_abort.cpp`](src/early_abort.cpp): trace-state composition and
  sound early rejection.
- [`src/direct_context_cache.cpp`](src/direct_context_cache.cpp) and
  [`src/search_pipeline.cpp`](src/search_pipeline.cpp): authenticated context
  loading, continuation policy, telemetry, and checkpoint identity.
- [`scripts/aws/`](scripts/aws/): immutable preparation and worker launch.

The specialized-modular-polynomial design follows [Bröker, Lauter, and
Sutherland](https://arxiv.org/abs/1001.0402) and
[Sutherland](https://arxiv.org/abs/1202.3985). Reproducible inputs, outputs,
and digests live under [`artifacts/local/`](artifacts/local/).
