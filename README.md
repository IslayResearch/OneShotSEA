# OneShotSEA: direct-first specialized SEA

This branch is a research checkpoint for one narrow question: can a custom SEA
point counter make one-shot primality-proof searches practical beyond 400 bits?
It adds a direct classical-`j` level producer, certified Elkies and Atkin
consumers, authenticated reusable preparation, and a search policy that tries
those direct levels before continuing with the existing Weber tables.

The direct producer is native C++. It does not call PARI/GP, Magma, Sage, or
another SEA implementation. For a curve invariant `j` and prime level `ell`, it
constructs only the specializations the point counter consumes:

```text
Phi_ell(j,Y)       and       partial Phi_ell(X,Y) / partial X at X=j.
```

This is a working point-counting and search-integration result, not a finished
claim of an end-to-end one-shot proof. In particular, this branch has not yet
found a certificate for `nextprime(10^125)` or established the proposed
SEA-versus-CM crossover.

## What this branch contains

The review target is the following direct-first path:

```text
authenticated direct-level cache
    -> specialized classical-j SEA (exact Elkies or certified Atkin data)
    -> retained trace constraints
    -> Weber continuation for levels not already covered
    -> exact smoothness rejection or ordinary certificate gates
```

Implemented pieces:

- A specialized modular-polynomial producer based on CM isogeny surfaces,
  Vélu quotients, interpolation, and centered CRT under an explicit height
  bound.
- Exact Elkies residues with normalized-codomain recovery, BMSS validation,
  and Frobenius eigenvalue recovery.
- Certified Atkin trace sets with factored CRT counting and bounded
  meet-in-the-middle enumeration.
- Streaming, authenticated context caches whose level order is part of the
  search and checkpoint identity.
- A `direct-first` search policy. Incomplete direct work is retained when the
  search continues through Weber levels; already-covered levels are skipped.
  Retained constraints are bound to the exact curve coefficients, including
  across the cap-N to cap-one transition, so same-`j` twists cannot share
  trace state.
- Exact smooth-part early rejection. Exhausting a resource cap is reported as
  an implementation limit and is never treated as a mathematical rejection.

Direct SEA remains opt-in. The cloud launchers do not yet select or distribute
a production direct-level schedule automatically.

## Current evidence

Four retained p125 results exercise the implementation at 416 bits:

1. A 30-level direct run through `ell=271` reconstructed a unique trace. Every
   direct residue agreed with the authenticated table-backed route, and an
   independently retained Magma count agreed with the final point count. See
   the [full direct-trace evidence](artifacts/local/p125-direct-trace-777e293-20260803/README.md).
2. A fixed-curve direct-first A/B used 15 cached direct levels, retained their
   4 exact and 11 Atkin constraints, and needed 50 Weber levels rather than 70.
   SEA time fell from 44.952 s to 34.660 s (1.297x), while total curve time fell
   from 71.263 s to 62.309 s (1.144x). A separate table-backed cap-one run and
   an independent PARI/GP 2.17.4 `ellcard` count both reconstructed the same
   unique trace. See the
   [hybrid A/B evidence](artifacts/local/p125-direct-first-hybrid-20260803/README.md).
3. A four-curve cap-16 cohort found a coverage benefit, not only a timing
   benefit. Weber-only exhausted all 77 usable levels on one curve with 64
   candidates and failed closed. A measured 20-level direct-first schedule
   completed all four sound screenings with candidate counts 1, 13, 4, and 5.
   Exact PARI counts of the generated curve models lie in every retained set.
   The matched cap-64 cohort reduced aggregate SEA time by 1.081x; a bracketed
   same-curve cap-16 check measured 1.059x. See the
   [four-curve cohort evidence](artifacts/local/p125-direct-first-cohort-20260803/README.md).
4. The Atkin meet-in-the-middle work has a 240-record controlled A/B: direct
   evaluation improved by 10.96x and peak RSS by 28.71x with identical
   results. See the
   [Atkin performance evidence](artifacts/local/p125-direct-atkin-mitm-20260803/README.md).

These are implementation, coverage, and differential-validation results—not a
certificate-yield study. The common Weber downstream is not an independent
mathematical oracle; the retained PARI/Magma point counts are the independent
exact checks.

## Build and validate

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
  test-cli
```

Magma is not required for that native suite. `/usr/bin/make test-all` also
runs the optional independent point-count oracle and needs
`MAGMA=/path/to/magma` (or `magma` on `PATH`).

For a short fixed-p125 direct-path check:

```sh
/usr/bin/make test-p125-direct-trace
```

For the retained complete direct schedule:

```sh
./build/validate_p125_direct_trace --threads 4
```

## Run the direct-first search path

Preparation is curve-independent and can be amortized over a search cohort:

```sh
./build/oneshotsea prepare-classical-direct-context \
  --p P \
  --classical-direct-levels 7,5,11,13,19,17 \
  --classical-direct-context-max-file-bytes 8589934592 \
  --sea-threads 4 \
  --output runs/direct.ctx
```

Record the emitted SHA-256 through a trusted channel. The search will reject a
cache whose digest, schedule, construction caps, mathematical witnesses,
canonical matrices, interpolation identities, or CRT bounds do not match.

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

The ordered level list is intentional: it is a measured policy input and part
of the checkpoint identity, not merely a set. The optional
`--classical-direct-cache-resident-bytes N` controls an LRU of inactive level
payloads. It changes memory/performance only; active evaluations and object/GMP
overhead are additional.

See the [cache contract](docs/direct_context_cache.md) and
[search integration](docs/search_pipeline.md) before using a prepared artifact
in a production search.

## What the asymptotic claim does and does not say

The proposed `p^(1/8+o(1))` factor is the heuristic number of curves needed to
encounter the smooth divisor required by a one-shot certificate. It is not the
cost of one SEA point count. Under the usual small-level and auxiliary-prime
assumptions, per-curve SEA work is polynomial in `log p` and is absorbed into
the `o(1)` term.

This implementation is consistent with that intended decomposition, but it
does not prove it for unbounded inputs. The current 64-bit auxiliary field,
fixed-`v` selector, explicit level schedule, and unmeasured certificate yield
remain material qualifications. Cache compaction and early abort improve
constants and average rejected-curve work; neither changes the outer heuristic
exponent. See [asymptotic scope](docs/asymptotic_scope.md).

## Why this is worth expert review

The branch is far enough along that review is no longer about a proposed
architecture. The custom producer reaches the real search path, direct and
Atkin evidence survive fallback, fixed 416-bit traces complete, and the cache
boundary is deterministic and authenticated. Drew can now assess concrete
proof obligations whose failure would invalidate later performance work:

1. auxiliary-prime and suitable-order selection;
2. ring-class resultants and CM-surface authentication;
3. Vélu enumeration, normalization, interpolation, and coefficient bounds;
4. Elkies/Atkin constraint soundness and retained-state composition; and
5. equivalence of cached construction and fresh construction.

A positive review would validate the direct-SEA foundation and justify larger
p125/p130 searches. It would not by itself validate certificate yield, the CM
crossover, or the unbounded asymptotic heuristic.

## Review map

- [`src/direct_modpoly.cpp`](src/direct_modpoly.cpp): auxiliary primes,
  coefficient bounds, and CRT reconstruction.
- [`src/class_polynomial.cpp`](src/class_polynomial.cpp) and
  [`src/prime_isogeny.cpp`](src/prime_isogeny.cpp): ring-class construction and
  Vélu quotients.
- [`src/cm_surface.cpp`](src/cm_surface.cpp): surface authentication and
  specialized interpolation.
- [`src/direct_context_cache.cpp`](src/direct_context_cache.cpp): canonical
  encoding, streaming publication, and authenticated lazy loading.
- [`src/sea.cpp`](src/sea.cpp), [`src/trace.cpp`](src/trace.cpp), and
  [`src/early_abort.cpp`](src/early_abort.cpp): certified trace consumption and
  sound early rejection.
- [`src/search_pipeline.cpp`](src/search_pipeline.cpp): direct-first retained
  continuation, concurrency, telemetry, and checkpoint identity.

The producer follows the specialized-modular-polynomial direction of
[Bröker, Lauter, and Sutherland](https://arxiv.org/abs/1001.0402) and
[Sutherland](https://arxiv.org/abs/1202.3985). Detailed proof contracts live in
[`docs/`](docs/); benchmark provenance lives with each retained artifact under
[`artifacts/local/`](artifacts/local/).
