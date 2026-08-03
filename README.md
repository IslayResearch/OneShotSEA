# OneShotSEA: direct SEA specialization branch

This branch is a focused implementation checkpoint for the SEA route to
one-shot elliptic-curve primality proofs. Its main contribution is a custom,
table-free producer for the two target-field polynomials needed at one
classical SEA level:

```text
Phi_ell(j,Y)       and       partial Phi_ell(X,Y) / partial X at X=j
```

The producer follows the explicit-CRT and isogeny-volcano methods of
Bröker–Lauter–Sutherland and Sutherland. It does not delegate point counting
to PARI/GP, Magma, Sage, or another SEA implementation, and it does not load a
full target-level bivariate modular polynomial.

CM orders and CM isogeny surfaces are used internally to construct the
specialization. Curves are still sampled and point-counted by SEA; this is not
the alternative CM curve-search algorithm.

## What this branch contains

- Callback-free classical-`j` specialization with internally derived ring
  class polynomials, witnessed auxiliary primes, proved coefficient bounds,
  and exact centered CRT reconstruction.
- Exact Elkies residues checked through normalized codomain recovery, BMSS
  isogeny validation, and Frobenius eigenvalues.
- Certified Atkin factor-degree constraints for no-root levels.
- An opt-in direct-SEA tail in the local search pipeline that continues the
  retained trace state after authenticated table-backed levels.
- Lazy, bounded-parallel preparation of immutable per-level contexts, reused
  across all curves in a process.
- Compact warm contexts: after a CM surface is fully checked, only two
  `(ell+2) x (ell+2)` interpolation matrices per auxiliary prime remain. Their
  canonical residues are stored as `uint64_t` and evaluated with exact
  128-bit modular products.
- Independent differential, Schoof, corruption, cap-exhaustion, concurrency,
  and checkpoint tests.

This is a working implementation checkpoint, not a completed large-prime
demonstration. It does not yet provide a direct level schedule sufficient to
finish the 416-bit `p125` trace, a new `nextprime(10^125)` certificate, an
authenticated direct Weber producer, or cloud-launcher admission for the new
options.

## Build and validate

The native build requires a C++20 compiler and GMP.

```sh
/usr/bin/make -j4
/usr/bin/make \
  test-direct-modpoly \
  test-prime-isogeny \
  test-cm-surface \
  test-atkin \
  test-search-pipeline \
  test-cli \
  test-progress-audit \
  test-x1-27-probe
```

`make test-all` runs the repository-wide suite. Magma can be supplied as an
optional independent point-count oracle, but it is never used by the
production path:

```sh
MAGMA=/path/to/magma /usr/bin/make test-all
```

## Exercise the direct search tail

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

The level list and execution caps are included in the checkpoint schedule
digest. Cap exhaustion is reported as an implementation limit, never as a
mathematical rejection. The direct options are currently admitted only by the
local CLI.

## Reproduce the focused benchmark

The checked `p125` harness measures lazy preparation, a cold curve, a second
distinct-`j` curve using the prepared context, independent Schoof validation,
matrix payload, and process peak RSS:

```sh
/usr/bin/make build/benchmark_p125_classical_direct
./build/benchmark_p125_classical_direct --threads 4 13
./build/benchmark_p125_classical_direct --threads 4 29
```

In the same-host level-29 bracket, compact contexts reduced peak RSS from
60.6 MB to 12.8–13.2 MB and reduced distinct-`j` warm evaluation from 251 ms
to 84–87 ms. Cold timings were thermally noisy, so this branch makes no cold
speedup claim. Both cold and warm residues matched independent Schoof. The
full audited raw bracket and measurement limits are in
[the compact-context note](docs/direct_context_compaction.md).

## Correctness and asymptotic scope

The trusted classical path admits only complete square-free CM surfaces,
enumerates all `ell+1` cyclic quotients, checks the expected horizontal-edge
count and interpolation identities, and reconstructs only after the CRT
modulus exceeds a proved height bound. Any inconsistent witness or exhausted
execution bound fails closed.

The intended one-shot search heuristic is `p^(1/8+o(1))`: a suitable random
curve is expected after that many trials, while direct SEA point counting is
polynomial in `log p` and is absorbed in the `o(1)` term. This branch removes
the finite target-level table dependency needed for that argument. It does
not yet prove the end-to-end claim for unbounded inputs: auxiliary primes are
currently limited to proved 64-bit values, their fixed-`v` selection is
heuristic, and large level schedules and certificate yield remain unmeasured.

The compact-context change improves retained field storage from
`O(K ell^3)` to exactly `2 K (ell+2)^2` 64-bit coefficients for `K` CRT
primes. That is a real polynomial memory improvement, but it does not change
the outer `p^(1/8+o(1))` search exponent.

## Why this is ready for expert review

The branch now connects its novel producer to the real retained-state search
and emits evidence that can be checked independently at the target size. A
review by Drew is therefore useful now: it can validate the mathematical and
trust-boundary decisions before larger-level engineering and persistent
context formats make them more expensive to change.

The highest-value review points are:

1. suitable-order and auxiliary-prime predicates;
2. exact ring-class resultants and complete CM-surface admission;
3. Vélu enumeration and interpolation normalization;
4. coefficient-height bounds and centered CRT reconstruction;
5. Elkies and Atkin evidence consumption; and
6. retained-state, early-screen, and checkpoint semantics.

A positive review would confirm that this is a sound foundation for the
intended SEA search. It would not imply that the large-prime search,
`p^(1/8+o(1))` engineering premise, crossover estimate, or direct Weber path
has already been demonstrated.

## Review map

- [`src/direct_modpoly.cpp`](src/direct_modpoly.cpp): suitable orders,
  auxiliary-prime witnesses, height bounds, and CRT.
- [`src/class_polynomial.cpp`](src/class_polynomial.cpp): exact three-power
  ring-class polynomials.
- [`src/prime_isogeny.cpp`](src/prime_isogeny.cpp): auxiliary-field arithmetic,
  rational kernels, and Vélu quotients.
- [`src/cm_surface.cpp`](src/cm_surface.cpp): CM-surface authentication,
  interpolation, and compact prepared contexts.
- [`src/sea.cpp`](src/sea.cpp): prepared direct levels and Elkies/Atkin
  retained-state consumption.
- [`src/search_pipeline.cpp`](src/search_pipeline.cpp): local search
  integration, concurrency, identity, and telemetry.
- [`tools/benchmark_p125_classical_direct.cpp`](tools/benchmark_p125_classical_direct.cpp):
  checked cold/warm benchmark with separate Schoof validation.

Detailed contracts and evidence:

- [Explicit-CRT producer](docs/explicit_crt_producer.md)
- [Compact context design and benchmark](docs/direct_context_compaction.md)
- [Direct-specialization trust boundary](docs/direct_specialization_boundary.md)
- [SEA proof obligations](docs/sea_design.md)
- [Search integration and operations](docs/search_pipeline.md)

## Algorithm references

- R. Bröker, K. Lauter, and A. Sutherland,
  [*Modular polynomials via isogeny volcanoes*](https://arxiv.org/abs/1001.0402).
- A. Sutherland,
  [*On the evaluation of modular polynomials*](https://arxiv.org/abs/1202.3985).
