# OneShotSEA — direct classical-`j` SEA

This branch implements on-demand modular-polynomial specialization for SEA. For
an input curve invariant `j` and prime level `ell`, it constructs exactly the
two target-field polynomials used by the point counter:

```text
Phi_ell(j,Y)       and       partial Phi_ell(X,Y) / partial X at X=j.
```

It uses explicit CRT, class polynomials, isogeny volcanoes, and Vélu quotients;
it does not call PARI/GP, Magma, Sage, or another SEA implementation, and it
does not load the full target-level bivariate classical modular polynomial.

The purpose of this checkpoint is narrower than the repository's historical
table-backed search: establish that direct classical-`j` specialization is
correct, practical beyond 400 bits, reusable across curves, and safe to feed
into the real retained-trace and early-abort path.

## Current result

Implemented on this branch:

- A callback-free direct producer derives its ring-class polynomials, selects
  witnessed auxiliary primes, authenticates complete CM isogeny surfaces, and
  performs centered CRT reconstruction under a proved coefficient bound.
- Exact Elkies levels pass through normalized-codomain recovery, BMSS
  validation, and Frobenius eigenvalue recovery. No-root levels produce
  certified Atkin constraints.
- Prepared level contexts are immutable, bounded-parallel, persistable, and
  reusable across curves. The compact form stores exactly two
  `(ell+2) x (ell+2)` `uint64_t` matrices per CRT prime.
- The preparation hot path proves a rational `E[ell]` basis, enumerates the
  projective line once, batches Vélu sums, and uses a checked 64-bit Montgomery
  field for auxiliary arithmetic.
- Local search can append explicitly selected direct levels to the retained
  SEA state and apply the existing sound early-abort screen.
- A one-context-at-a-time run completed the trace of a fixed 416-bit p125
  X1(27) curve through `ell=271`. All 30 direct residues agreed with the
  authenticated table-backed reference and reconstructed the same unique
  signed trace.

Still open:

- choosing a production, curve-independent direct-level schedule by measured
  cost and expected information yield;
- producing an end-to-end one-shot certificate for `nextprime(10^125)` or a
  p130 target;
- lifting the 64-bit auxiliary-prime restriction and implementing direct
  Weber specialization; and
- measuring certificate yield and the crossover against the CM search.

This is therefore a complete direct point-counting checkpoint for one real
416-bit curve, not a large-prime certificate announcement.

## Build and smoke test

The native build requires a C++20 compiler and GMP.

```sh
/usr/bin/make -j4
/usr/bin/make \
  test-direct-modpoly \
  test-prime-isogeny \
  test-cm-surface \
  test-atkin \
  test-search-pipeline \
  test-cli
```

`/usr/bin/make test-all` runs the repository-wide suite. Magma is optional and
is used only as an independent test oracle when `MAGMA=/path/to/magma` is set.

For a short p125 direct-path check:

```sh
/usr/bin/make -j4 build/validate_p125_direct_trace
./build/validate_p125_direct_trace --threads 4 5 23 29 31 37
```

An explicit partial schedule exits successfully with `complete:false`. The
full default validation is:

```sh
./build/validate_p125_direct_trace --threads 4
```

The retained four-thread run took 1,203.9 seconds of preparation and 59.8
seconds of curve evaluation, about 21.1 minutes total. Its largest single
context payload was 614,118,960 bytes; contexts are discarded between levels.
See [the complete p125 validation](docs/p125_direct_trace_validation.md) for
the curve identity, full residue list, timings, and independence limits.

## Use the direct path in search

Direct levels are opt-in and must be distinct increasing primes greater than
three:

```sh
./build/oneshotsea search \
  ...existing search arguments... \
  --sea-threads 4 \
  --classical-direct-levels 7,11 \
  --classical-direct-max-prime-candidates 1000000 \
  --classical-direct-max-x-candidates 1000000
```

The schedule and caps are bound into checkpoint identity. Cap exhaustion is
reported as an implementation limit, never as a mathematical rejection. The
local CLI admits these options; the cloud launcher does not yet do so.

Because preparation is curve-independent, a context can be prepared once:

```sh
./build/oneshotsea prepare-classical-direct-context \
  --p P \
  --classical-direct-levels 7,11 \
  --sea-threads 4 \
  --output runs/direct-7-11.ctx
```

Record the emitted SHA-256 and provide both options to a matching search:

```sh
--classical-direct-context-cache runs/direct-7-11.ctx \
--classical-direct-context-sha256 TRUSTED_SHA256
```

The digest must come from a trusted preparation step, not from the received
artifact. The format and validation rules are in
[the context-cache contract](docs/direct_context_cache.md).

## What the validation establishes

The complete p125 run is a differential test between two ways of supplying an
SEA level:

1. the branch's explicit-CRT/isogeny-volcano specialization; and
2. the repository's authenticated table-backed specialization.

Each direct result is committed before comparison. The X1(27) group prior
independently constrains the sign, and separate Schoof checks cover two p125
level-29 residues. The two full paths nevertheless share downstream
BMSS/Frobenius and trace-constraint code, and the completing level schedule was
selected using the table-backed oracle. The run is strong implementation
evidence, not a formally independent proof of every layer.

Direct level construction fails closed unless it verifies the CM surface,
enumerates all `ell+1` cyclic quotients, checks horizontal-edge counts and
interpolation identities, and exceeds the CRT height bound. Cache loading
rechecks mathematical structure and requires an externally trusted digest.

## Scaling claim

The `p^(1/8+o(1))` figure is the heuristic number of curves needed to find an
order with the smooth divisor required by the one-shot certificate. It is not
the measured cost of this point counter. SEA contributes only a
polynomial-in-`log p` factor under the usual small-level and prime-selection
assumptions, so it is absorbed into the `o(1)` term.

This branch preserves that intended shape, but does not yet prove it for
unbounded inputs. In particular, auxiliary arithmetic is restricted to
64-bit primes, the practical fixed-`v` selector is heuristic, and production
level scheduling has not been validated. Compact contexts improve a large
polynomial factor—from retained `O(K ell^3)` field elements to exactly
`2 K (ell+2)^2` 64-bit coefficients—but do not change the outer search
exponent. Sound early abort lowers average work per rejected curve; it does
not improve the heuristic number of curves.

The comparison with a `p^(1/4+o(1))` CM search and a crossover near 400 bits
also remains a heuristic engineering hypothesis, not a result of the p125
trace run. See [asymptotic scope and evidence](docs/asymptotic_scope.md) for
the claim decomposition and the measurements still needed.

## Why expert review is worthwhile now

The novel producer is no longer an isolated scaffold: it feeds the real SEA
consumer, has completed a 416-bit trace, has deterministic context artifacts,
and exposes narrow proof obligations. Drew's review can now confirm whether
the mathematical boundary is sound before scheduling and performance work
turn these interfaces into production assumptions.

The highest-value review questions are:

1. Are the suitable-order and auxiliary-prime predicates sufficient?
2. Do the ring-class resultants and CM-surface checks admit exactly the
   intended surface?
3. Are Vélu enumeration, interpolation normalization, and the coefficient
   height bound correct?
4. Do Elkies and Atkin records enter retained state without permitting an
   unsound early rejection?
5. Does cached-context authentication preserve the proof obligations of fresh
   construction?

A positive review would validate the direct-SEA foundation. It would not by
itself validate certificate yield, the CM crossover, or the unbounded
asymptotic claim.

## Review map

- [`src/direct_modpoly.cpp`](src/direct_modpoly.cpp): suitable orders,
  auxiliary-prime witnesses, height bounds, and CRT.
- [`src/class_polynomial.cpp`](src/class_polynomial.cpp): three-power
  ring-class polynomial construction.
- [`src/prime_isogeny.cpp`](src/prime_isogeny.cpp): auxiliary-field arithmetic,
  rational kernels, and Vélu quotients.
- [`src/cm_surface.cpp`](src/cm_surface.cpp): surface authentication,
  interpolation, and compact prepared contexts.
- [`src/direct_context_cache.cpp`](src/direct_context_cache.cpp): portable
  encoding, atomic publication, authenticated loading, and revalidation.
- [`src/sea.cpp`](src/sea.cpp): direct levels and retained-state consumption.
- [`src/early_abort.cpp`](src/early_abort.cpp): exact smooth-part rejection.
- [`src/search_pipeline.cpp`](src/search_pipeline.cpp): local integration,
  concurrency, checkpoint identity, and telemetry.
- [`tools/validate_p125_direct_trace.cpp`](tools/validate_p125_direct_trace.cpp):
  complete p125 trace harness.

Detailed contracts:

- [Explicit-CRT producer](docs/explicit_crt_producer.md)
- [Direct-specialization trust boundary](docs/direct_specialization_boundary.md)
- [Compact prepared contexts](docs/direct_context_compaction.md)
- [Authenticated context cache](docs/direct_context_cache.md)
- [Complete p125 validation](docs/p125_direct_trace_validation.md)
- [Asymptotic scope and evidence](docs/asymptotic_scope.md)
- [Search integration](docs/search_pipeline.md)

## Algorithm references

- R. Bröker, K. Lauter, and A. Sutherland,
  [*Modular polynomials via isogeny volcanoes*](https://arxiv.org/abs/1001.0402).
- A. Sutherland,
  [*On the evaluation of modular polynomials*](https://arxiv.org/abs/1202.3985).
