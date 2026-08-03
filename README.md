# OneShotSEA — direct classical-j SEA checkpoint

This branch is the implementation checkpoint for removing OneShotSEA's finite
modular-polynomial-table dependency.  It constructs the two polynomials needed
by one classical SEA level directly in the target field:

```text
Phi_ell(j,Y)       and       partial Phi_ell(X,Y) / partial X at X=j.
```

The producer uses explicit CRT and isogeny-volcano techniques.  It does not
call PARI/GP, Magma, Sage, or another SEA implementation, and it never builds
or loads the full target-level bivariate modular polynomial.  The surrounding
repository still contains the established table-backed search; this README is
about the direct-SEA work added on this branch.

## Branch status

Working now:

- A callback-free classical-`j` producer derives its own ring-class
  polynomials, selects witnessed auxiliary primes, authenticates complete CM
  isogeny surfaces, and performs exact centered CRT reconstruction under a
  proved coefficient bound.
- The resulting specialization feeds the real SEA retained-trace path.  Root
  levels produce exact Elkies residues through normalized codomain recovery,
  BMSS validation, and Frobenius eigenvalues; no-root levels produce certified
  Atkin factor-degree constraints.
- Search can append an opt-in direct tail after its authenticated table-backed
  levels and can reject curves early using the retained trace state.
- Curve-independent level preparation is immutable, bounded-parallel, and
  reusable across curves.  Its compact representation retains exactly two
  `(ell+2) x (ell+2)` `uint64_t` interpolation matrices per CRT prime.
- The preparation hot path proves a rational `E[ell]` basis, enumerates the
  projective line exactly once, and computes all quotient coefficients with
  batched Vélu sums in a checked 64-bit Montgomery field.  It does not build
  transient kernel polynomials when only codomain invariants are retained.
- Prepared contexts can be persisted in a portable, atomically published
  cache and shared across restarts or worker shards.  Loading requires an
  external trusted SHA-256, revalidates the mathematical structure, and binds
  the digest into checkpoint identity.
- Differential, Schoof, corruption, cap-exhaustion, concurrency, checkpoint,
  sanitizer, and CLI tests exercise the new path.

Not completed yet:

- a direct level schedule sufficient to finish the 416-bit `p125` trace;
- an end-to-end certificate for `nextprime(10^125)` or a `p130` run;
- direct Weber specialization, cloud-launcher admission, or an unbounded
  auxiliary-prime implementation; and
- a measured end-to-end crossover against the CM search.

In short: this branch establishes and integrates the direct specialization
primitive; it is not a large-prime certificate announcement.

## Build and run the focused checks

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

`/usr/bin/make test-all` runs the repository-wide suite.  Magma is optional
and is used only as an independent test oracle:

```sh
MAGMA=/path/to/magma /usr/bin/make test-all
```

## Run a direct SEA tail

Pass a strictly increasing list of distinct primes greater than three to a
normal local search command:

```sh
./build/oneshotsea search \
  ...existing search arguments... \
  --sea-threads 4 \
  --classical-direct-levels 7,11 \
  --classical-direct-max-prime-candidates 1000000 \
  --classical-direct-max-x-candidates 1000000
```

The level schedule and execution caps are part of the checkpoint digest.  Cap
exhaustion is an implementation limit, not a mathematical rejection.  These
options are currently admitted by the local CLI, not the cloud launcher.

Preparation dominates the current direct implementation at larger levels.  A
context can therefore be prepared once and authenticated on later runs:

```sh
./build/oneshotsea prepare-classical-direct-context \
  --p P \
  --classical-direct-levels 7,11 \
  --sea-threads 4 \
  --output runs/direct-7-11.ctx
```

Record the SHA-256 emitted by that command and supply both cache options to the
matching search:

```sh
--classical-direct-context-cache runs/direct-7-11.ctx \
--classical-direct-context-sha256 TRUSTED_SHA256
```

The digest must come from a trusted preparation step, not from the received
artifact itself.  The exact format and failure behavior are documented in
[the direct-context cache contract](docs/direct_context_cache.md).

## Reproduce the p125 level measurements

The focused harness prepares one direct level, evaluates two distinct
`j`-invariants, validates the residues independently with Schoof, and reports
payload size and peak RSS:

```sh
/usr/bin/make build/benchmark_p125_classical_direct
./build/benchmark_p125_classical_direct --threads 4 13
./build/benchmark_p125_classical_direct --threads 4 29
```

On the retained same-host optimized runs with four preparation workers, level
29 prepared in 0.326 seconds.  Its persisted 1.17 MB context loaded in 33.9 ms;
the two independently checked exact residues remained `23 mod 29` and
`12 mod 29`.  Higher preparation-only brackets were 5.08 seconds and 35.65 MB
at level 101, and 28.40 seconds and 124.99 MB at level 157.  The level-29,
level-101, and level-157 context SHA-256 values are byte-identical to contexts
made before the arithmetic optimization, providing a deterministic
coefficient-level differential in addition to the level-29 Schoof checks.

The retained pre-basis same-binary level-29 preparation took 19.97 seconds;
the current result is about 61x faster.  A retained intermediate level-101
build took 33.82 seconds, about 6.7x the current time.  These are local
engineering brackets, not crossover or certificate-yield measurements, and
they still do not constitute a complete p125 point count.  Raw historical
brackets and their limitations are recorded in
[the compaction note](docs/direct_context_compaction.md) and
[the cache note](docs/direct_context_cache.md).

## Correctness and asymptotic claim

A direct level is admitted only after the implementation has checked a
complete square-free CM surface, enumerated all `ell+1` cyclic quotients,
verified the expected horizontal-edge count and interpolation identities, and
exceeded a proved CRT height bound.  Inconsistent witnesses and exhausted
execution caps fail closed.

The intended one-shot search heuristic remains `p^(1/8+o(1))`: finding a curve
whose order supports a short certificate is expected to take that many trials,
while SEA point counting should cost only factors polynomial in `log p` under
the standard small-Elkies-prime heuristic.  The direct producer removes the
finite target-level table dependency that prevented that asymptotic story from
being literal.

This branch does **not** yet establish the end-to-end asymptotic claim.  Its
auxiliary primes are restricted to proved 64-bit values, fixed-`v` selection is
heuristic, and the preparation cost of a p125-completing level schedule has not
been demonstrated.  Compact contexts improve retained storage from
`O(K ell^3)` field elements to exactly `2 K (ell+2)^2` 64-bit coefficients;
that changes an important polynomial factor, not the outer `p^(1/8+o(1))`
search exponent.

## What an expert review can confirm

This is worth reviewing now because the novel producer is connected to the
real search and emits evidence that can be checked independently at 416 bits.
A mathematical review can validate the trust boundary before larger-level
scheduling and performance work harden the current interfaces.  The
highest-value questions are:

1. Are the suitable-order and auxiliary-prime predicates sufficient?
2. Do the ring-class resultants and CM-surface checks admit exactly the
   intended surface?
3. Are Vélu enumeration, interpolation normalization, and the coefficient
   height bound correct?
4. Does Elkies/Atkin evidence enter the retained trace state without creating
   an unsound early rejection?
5. Does the authenticated context cache preserve the same proof obligations
   as fresh construction?

A positive review would confirm that this is a sound foundation for the
intended direct-SEA search.  It would not, by itself, confirm the crossover
estimate, certificate-yield heuristic, or completion of the p125 objective.

## Review map

- [`src/direct_modpoly.cpp`](src/direct_modpoly.cpp): suitable orders,
  auxiliary-prime witnesses, coefficient bounds, and CRT.
- [`src/class_polynomial.cpp`](src/class_polynomial.cpp): exact three-power
  ring-class polynomials.
- [`src/prime_isogeny.cpp`](src/prime_isogeny.cpp): auxiliary-field arithmetic,
  rational kernels, and Vélu quotients.
- [`src/cm_surface.cpp`](src/cm_surface.cpp): CM-surface authentication,
  interpolation, and compact prepared contexts.
- [`src/direct_context_cache.cpp`](src/direct_context_cache.cpp): portable
  encoding, atomic publication, authenticated loading, and revalidation.
- [`src/sea.cpp`](src/sea.cpp): direct levels and Elkies/Atkin retained-state
  consumption.
- [`src/search_pipeline.cpp`](src/search_pipeline.cpp): local integration,
  concurrency, checkpoint identity, and telemetry.
- [`tools/benchmark_p125_classical_direct.cpp`](tools/benchmark_p125_classical_direct.cpp):
  the checked p125 level harness.

Detailed contracts:

- [Explicit-CRT producer](docs/explicit_crt_producer.md)
- [Compact prepared contexts](docs/direct_context_compaction.md)
- [Authenticated context cache](docs/direct_context_cache.md)
- [Direct-specialization trust boundary](docs/direct_specialization_boundary.md)
- [SEA proof obligations](docs/sea_design.md)
- [Search integration](docs/search_pipeline.md)

## Algorithm references

- R. Bröker, K. Lauter, and A. Sutherland,
  [*Modular polynomials via isogeny volcanoes*](https://arxiv.org/abs/1001.0402).
- A. Sutherland,
  [*On the evaluation of modular polynomials*](https://arxiv.org/abs/1202.3985).
