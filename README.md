# OneShotSEA — direct classical-`j` SEA for one-shot proofs

This README describes the work unique to this branch. The branch turns the SEA
approach to one-shot primality proofs into a reviewable C++ implementation: it
builds specialized classical modular polynomials, feeds their trace information
into the real retained-state and early-abort path, and amortizes the
curve-independent work with authenticated caches.

The direct path is custom C++: it does not call PARI/GP, Magma, Sage, or another
SEA implementation. For a target invariant `j` and prime level `ell`, it
constructs only the two polynomials consumed by the point counter:

```text
Phi_ell(j,Y)       and       partial Phi_ell(X,Y) / partial X at X=j.
```

The current deliverable is a working point-counting and search-integration
checkpoint. Its largest completed validation is a full direct-SEA trace of a
fixed 416-bit p125 curve. It is not yet an end-to-end one-shot certificate, a
measured CM crossover, or a proof of the heuristic asymptotic claim.

## Delivered on this branch

The direct level producer:

- derives the required ring-class polynomials;
- selects auxiliary primes with explicit witnesses;
- constructs and authenticates CM isogeny surfaces;
- enumerates all `ell+1` cyclic quotients with Vélu maps;
- interpolates the two specialized classical modular polynomials; and
- reconstructs their integer coefficients with centered CRT under an explicit
  height bound.

The SEA consumer then classifies the level and records either:

- an exact Elkies trace residue, after normalized-codomain recovery, BMSS
  validation, and Frobenius eigenvalue recovery; or
- a certified Atkin trace constraint when the specialization has no root.

Those records enter the existing retained-trace state and exact smooth-part
early-abort test. Resource-cap exhaustion is an implementation limit; it is
never converted into a mathematical rejection.

Curve-independent preparation is reusable. A combined cache is authenticated
before use, structurally indexed without retaining its interpolation matrices,
and materialized one level at a time. Concurrent curve workers share a live
level; by default its payload is released after the last worker leaves it, or
an optional resource-only LRU retains it across curves. Cache generation
likewise prepares, writes, and releases one level at a time while preserving
the canonical v1 artifact format.

## Remaining work and non-claims

The branch still needs:

- a measured, curve-independent production schedule for direct levels;
- an end-to-end one-shot certificate for `nextprime(10^125)` or a p130 target;
- certificate-yield and CM-crossover measurements over a meaningful cohort;
- removal of the 64-bit auxiliary-prime restriction; and
- direct Weber specialization, if its smaller constants justify the extra
  implementation and review surface.

Direct levels are therefore opt-in. The local search accepts an explicit
schedule; the cloud launcher does not yet choose or submit one automatically.

## Build and test

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

Magma is not needed for the build or the native suite above. The broader
`/usr/bin/make test-all` target also runs the independent point-count oracle
and therefore requires `MAGMA=/path/to/magma` (or `magma` on `PATH`).

For a short p125 direct-path check:

```sh
/usr/bin/make test-p125-direct-trace
```

That target checks the exact level-5 residue and the expected incomplete
summary. A longer explicit partial schedule can be run directly:

```sh
./build/validate_p125_direct_trace --threads 4 5 23 29 31 37
```

Omitting the levels runs the complete retained p125 schedule:

```sh
./build/validate_p125_direct_trace --threads 4
```

## Prepare and use a direct-context cache

Preparation is independent of the target curve, so it can be amortized across
a search cohort:

```sh
./build/oneshotsea prepare-classical-direct-context \
  --p P \
  --classical-direct-levels 7,11 \
  --classical-direct-context-max-file-bytes 8589934592 \
  --sea-threads 4 \
  --output runs/direct-7-11.ctx
```

Record the emitted SHA-256. A matching search must receive the artifact and a
digest obtained through a trusted channel:

```sh
./build/oneshotsea search \
  ...existing search arguments... \
  --sea-threads 4 \
  --classical-direct-levels 7,11 \
  --classical-direct-context-cache runs/direct-7-11.ctx \
  --classical-direct-context-sha256 TRUSTED_SHA256 \
  --classical-direct-context-max-file-bytes 8589934592
```

Add `--sea-strategy direct-first` to try those authenticated cached levels
from the exact curve-family trace prior before loading Weber levels. This
nondefault policy requires the cache and a nonempty level list. A complete
direct trace set uses the ordinary sound smoothness and certificate gates; an
incomplete set is discarded before the unchanged Weber-first path restarts.
Cache/authentication/evaluation errors remain hard failures. The strategy and
cache digest are checkpoint-bound, while omitting the option preserves the
existing Weber-first schedule identity.

Do not derive the trusted digest from the untrusted artifact at load time. The
loader checks the whole-file digest, deterministic metadata, per-level
segments, mathematical witnesses, canonical matrices, interpolation
identities, and CRT bounds before allowing a level to affect search state. See
the [context-cache contract](docs/direct_context_cache.md) for the complete
trust boundary.

The cache file ceiling remains 4 GiB unless
`--classical-direct-context-max-file-bytes N` explicitly opts both preparation
and search into a larger bounded artifact. A nondefault ceiling is part of the
checkpoint identity; it does not weaken digest or structural authentication.

By default each authenticated level is released after its active curve workers
finish. A multi-curve run may instead retain recently used levels under a
resource-only logical matrix budget:

```sh
--classical-direct-cache-resident-bytes 5776130752
```

This example is approximately the compact payload of the retained 30-level
p125 schedule. Active evaluations and object/GMP overhead are additional, so
the value must be chosen together with curve concurrency and smooth-cache
memory. It affects performance and memory only, not checkpoint identity or
mathematical output.

Without a cache, search can prepare its explicit levels locally:

```sh
./build/oneshotsea search \
  ...existing search arguments... \
  --sea-threads 4 \
  --classical-direct-levels 7,11 \
  --classical-direct-max-prime-candidates 1000000 \
  --classical-direct-max-x-candidates 1000000
```

The schedule, caps, cache identity, and trusted digest are bound into checkpoint
identity.

## Validation at 416 bits

The complete fixed-curve validation used 30 direct levels through `ell=271`.
Every residue agreed with the authenticated table-backed specialization and
the accumulated constraints reconstructed the unique signed trace

```text
-534284869337319737295513917655253909609824180266230842767530862
```

The retained four-thread run took 1,221.3 seconds of level preparation and
60.5 seconds of curve evaluation, with 1,284.15 seconds (about 21.4 minutes) of
wall time. The largest single compact context payload was 614,118,960 bytes.

This is strong differential evidence, with two important qualifications. The
table-backed oracle selected the completing level schedule, and both routes
share the downstream BMSS, Frobenius, and trace-constraint code. The direct
producer does not receive the expected residues or final trace before each
comparison. An [independent retained Magma point count](artifacts/local/p125-weber-catalog-magma-20260802/README.md)
on the exact target curve corroborates the final trace.

See the [complete p125 validation](docs/p125_direct_trace_validation.md) for
the curve identity, all residues, timings, and independence limits. The
[retained evidence bundle](artifacts/local/p125-direct-trace-777e293-20260803/README.md)
contains the frozen source identity, commands, raw stream, hashes, and audit.

## What the asymptotic claim means

The expected `p^(1/8+o(1))` factor is the heuristic number of curves needed to
find an order with the smooth divisor required by the one-shot certificate. It
is not the running time of one SEA point count. Under the usual assumptions on
small SEA levels and auxiliary-prime selection, the point-counting work is
polynomial in `log p` and is absorbed into the `o(1)` term.

This implementation is consistent with that intended shape, but it does not
yet establish the asymptotic claim for unbounded inputs. Its 64-bit auxiliary
field, fixed-`v` selector, explicit schedule, and unmeasured certificate yield
are real qualifications. Compact and lazy contexts remove large polynomial
memory factors; they do not change the outer exponent. Early abort reduces
average work on rejected curves; it does not reduce the heuristic curve count.

The comparison with a `p^(1/4+o(1))` CM search, including a crossover near 400
bits, remains an engineering hypothesis until both paths are benchmarked on
the same targets and success criterion. See
[asymptotic scope and evidence](docs/asymptotic_scope.md).

## Why expert review is worthwhile

The mathematical producer now reaches the real SEA consumer, has completed a
416-bit trace, and has a deterministic authenticated cache boundary. A review
can therefore evaluate concrete proof obligations before scheduling and
performance choices turn them into production assumptions.

The highest-value questions for Drew are:

1. Are the suitable-order and auxiliary-prime predicates sufficient?
2. Do the ring-class resultants and surface checks authenticate exactly the
   intended CM surface?
3. Are Vélu enumeration, interpolation normalization, and the coefficient
   height bound correct?
4. Do Elkies and Atkin records enter retained state without enabling an
   unsound early rejection?
5. Does lazy authenticated cache loading preserve every proof obligation of
   fresh construction?

A positive review would validate the custom direct-SEA foundation. It would
not, by itself, validate certificate yield, the claimed CM crossover, or the
unbounded asymptotic heuristic.

## Review map

- [`src/direct_modpoly.cpp`](src/direct_modpoly.cpp): suitable orders,
  auxiliary-prime witnesses, coefficient bounds, and CRT.
- [`src/class_polynomial.cpp`](src/class_polynomial.cpp): ring-class polynomial
  construction.
- [`src/prime_isogeny.cpp`](src/prime_isogeny.cpp): auxiliary-field arithmetic,
  rational kernels, and Vélu quotients.
- [`src/cm_surface.cpp`](src/cm_surface.cpp): surface authentication,
  interpolation, and compact prepared contexts.
- [`src/direct_context_cache.cpp`](src/direct_context_cache.cpp): canonical
  encoding, streaming publication, authenticated indexing, and lazy loading.
- [`src/sea.cpp`](src/sea.cpp): direct-level classification and retained-state
  consumption.
- [`src/trace.cpp`](src/trace.cpp): exact factored Atkin CRT counting and
  bounded meet-in-the-middle enumeration.
- [`src/early_abort.cpp`](src/early_abort.cpp): exact smooth-part rejection.
- [`src/search_pipeline.cpp`](src/search_pipeline.cpp): search integration,
  concurrency, checkpoint identity, and telemetry.
- [`tools/validate_p125_direct_trace.cpp`](tools/validate_p125_direct_trace.cpp):
  fixed-curve p125 differential harness.

Detailed contracts:

- [Explicit-CRT producer](docs/explicit_crt_producer.md)
- [Direct-specialization trust boundary](docs/direct_specialization_boundary.md)
- [Compact prepared contexts](docs/direct_context_compaction.md)
- [Authenticated context cache](docs/direct_context_cache.md)
- [Complete p125 validation](docs/p125_direct_trace_validation.md)
- [Factored Atkin constraints](docs/direct_atkin_mitm.md)
- [Asymptotic scope and evidence](docs/asymptotic_scope.md)
- [Search integration](docs/search_pipeline.md)

Algorithm references:

- R. Bröker, K. Lauter, and A. Sutherland,
  [*Modular polynomials via isogeny volcanoes*](https://arxiv.org/abs/1001.0402).
- A. Sutherland,
  [*On the evaluation of modular polynomials*](https://arxiv.org/abs/1202.3985).
