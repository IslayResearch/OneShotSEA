# Direct SEA specialization for OneShotSEA

This branch implements the missing direct-specialization producer for the SEA
route to one-shot elliptic-curve primality proofs.  For a target field
`F_q`, curve invariant `j`, and SEA prime `ell`, it constructs only

```text
Phi_ell(j,Y)       and       d/dX Phi_ell(j,Y)
```

using the explicit-CRT/isogeny-volcano algorithms of Bröker--Lauter--
Sutherland and Sutherland.  It does not load a target-level bivariate modular
polynomial and does not call PARI/GP, Magma, Sage, or another SEA
implementation.

The construction uses CM orders and CM isogeny surfaces internally to produce
modular-polynomial values.  That is not the alternative CM primality-proof
search: curves are still sampled and point-counted by SEA.  This distinction
is central to the purpose of the branch.

## Current status

| Status | Capability |
|---|---|
| Working | Callback-free classical-`j` specializations with internally derived class state, witnessed auxiliary primes, a proved coefficient bound, and exact CRT reconstruction |
| Working | BMSS/Frobenius Elkies residues and certified Atkin factor-degree constraints from those specializations |
| Integrated | An opt-in direct tail in the local production search, continuing retained SEA state after authenticated Weber-table levels |
| Optimized | Lazy per-level preparation, immutable reuse across curves, and bounded parallel preparation under the existing SEA thread budget |
| Independently checked | Exact residues against Schoof, coefficients against authenticated tables where available, Atkin sets against a separate oracle, and fail-closed behavior under malformed evidence |
| Not complete | A direct schedule large enough to finish the `p125` trace, a new primality certificate, an authenticated direct Weber producer, or admission by the RunPod/AWS wrappers |

The default search remains the inherited authenticated table-backed path.  No
`nextprime(10^125)` or `nextprime(10^130)` certificate is claimed here.

## Build and run the focused validation

The native build requires a C++20 compiler and GMP.

```sh
/usr/bin/make -j4
/usr/bin/make \
  test-direct-modpoly \
  test-prime-isogeny \
  test-cm-surface \
  test-search-pipeline \
  test-cli \
  test-progress-audit \
  test-x1-27-probe
```

`make test-all` runs the inherited repository-wide suite as well.  Magma is
an optional, independent point-count oracle:

```sh
MAGMA=/path/to/magma /usr/bin/make test-all
```

The focused suite includes positive, differential, corruption, cap-exhaustion,
and transactional-state tests. This checkpoint was also run under ASan/UBSan
and ThreadSanitizer; Magma is never part of the production execution path.

## Use the direct tail locally

Add a strictly increasing list of distinct primes greater than three to an
otherwise normal search command:

```sh
./build/oneshotsea search \
  ...existing search arguments... \
  --sea-threads 4 \
  --classical-direct-levels 7,11 \
  --classical-direct-max-prime-candidates 1000000 \
  --classical-direct-max-x-candidates 1000000
```

The direct policy, ordered levels, and both search caps are bound into the
schedule digest.  Changing them invalidates an existing checkpoint.  Cap
exhaustion is an implementation limit, never a mathematical rejection.

Each curve-independent level context is prepared on first use and shared
read-only by all curve workers in that process.  Unused levels are not
prepared.  `--sea-threads` bounds independent auxiliary-prime preparation;
`0` selects the hardware default.  Preparation output and failure ordering
remain deterministic.

This path is admitted only by the local CLI.  The checked cloud launchers and
artifact auditors deliberately reject these options until that operational
trust boundary is extended and requalified.

## Evidence on the 416-bit target

- The classical path reconstructs levels 5, 7, and 11 on the `p125` fixture
  from 34, 37, and 43 witnessed auxiliary primes.  It obtains trace residues
  3, 5, and 10; the level-11 residue is independently checked by Schoof, and
  levels 5 and 7 also agree coefficient-for-coefficient with authenticated
  full-table paths.
- The product `5*7*11 = 385` is intentionally too small to determine the full
  trace.  The implementation reports incomplete evidence and cannot turn it
  into a certificate.
- A no-root level-7 fixture agrees with an independent full-table oracle on
  both the Atkin projective order and the resulting trace-residue set.
- A search fixture continues ten retained Hasse-compatible traces through
  direct levels 7 and 11, isolates its trace, and passes the unchanged
  canonical certificate verifier without repeating the table pass.

On the local `p125` X1(27) regression, a reverse-bracketed run measured
3.03--3.21 seconds for serial level-7/11 setup and 0.924--0.931 seconds with
four preparation workers. The main cold evaluation took 0.969 seconds and a
second curve using the prepared contexts 0.099 seconds. Both curves are checked
against independent Schoof residues. This is a bounded engineering regression,
not a throughput distribution or an asymptotic benchmark.

## Correctness boundary

The classical producer derives its class polynomials internally from fixed
exact `Phi_3` resultants, admits only complete square-free CM surfaces,
enumerates all `ell+1` cyclic quotients, and reconstructs coefficients only
after the CRT modulus exceeds a proved integer height bound.  Elkies evidence
then receives a local BMSS/Frobenius check.  Atkin evidence is admitted only
after square-free equal-degree factorization.

Early screening is sound only after enumerating every Hasse-compatible trace
and checking exact smooth parts for both the curve and twist orders.  Atkin
constraints may narrow that complete set but cannot satisfy the unique-trace
gate required for certificate construction.  Any inconsistency or exhausted
implementation bound fails closed.

The experimental direct Weber code is outside this trust boundary.  It still
needs authenticated class/orientation state and a normalization-specific
coefficient-height proof.

## Asymptotic status

Let `n=ceil(log2 q)`, use the verifier smoothness bound `B=n^4`, and seek a
certified divisor of size `q^(1/2+o(1))`.  Under the usual Dickman--Mertens
heuristic, a random curve order succeeds with probability
`q^(-1/8+o(1))`, so the expected search examines `q^(1/8+o(1))` curves.  If
direct SEA point counting costs only `poly(n)` per curve, it is absorbed by
the `o(1)` term.  This is the intended asymptotic advantage over the CM-search
term `q^(1/4+o(1))`.

The branch removes the finite target-level table catalog from the classical
path, which is necessary for that argument, but it does not prove the full
claim.  Auxiliary primes are currently restricted to proved 64-bit values and
selected by the paper's heuristic fixed-`v` method; larger level schedules,
memory growth, and end-to-end certificate yield remain unmeasured.  Parallel
preparation and cross-curve reuse improve constants only and do not change the
claimed exponent.

## Why expert review is useful now

The novel path now produces independently checkable evidence at the target
size and is connected to the actual retained-state search.  A focused review
can therefore validate the mathematical core before higher-level performance
work fixes more interfaces in place:

1. suitable-order and auxiliary-prime predicates;
2. exact ring-class resultants and complete CM-surface admission;
3. Vélu enumeration and interpolation normalization;
4. coefficient-height and centered-CRT reconstruction;
5. Elkies/Atkin consumption; and
6. retained-state, early-screen, and checkpoint semantics.

A positive review would establish that this is a sound foundation for the
intended SEA search.  It would not endorse a completed large-prime search, the
heuristic crossover estimate, or the unfinished Weber producer.

## Code map

- [`src/direct_modpoly.cpp`](src/direct_modpoly.cpp): suitable orders,
  auxiliary-prime witnesses, target lifts, height bounds, and CRT.
- [`src/class_polynomial.cpp`](src/class_polynomial.cpp): exact three-power
  ring-class polynomials from fixed `Phi_3` resultants.
- [`src/prime_isogeny.cpp`](src/prime_isogeny.cpp): auxiliary-field arithmetic,
  rational kernels, and Vélu quotients.
- [`src/cm_surface.cpp`](src/cm_surface.cpp): CM surfaces and direct
  specialization.
- [`src/sea.cpp`](src/sea.cpp): direct levels, prepared contexts, and
  Elkies/Atkin retained-state consumption.
- [`src/search_pipeline.cpp`](src/search_pipeline.cpp): local search
  integration, identity, concurrency, and telemetry.
- [`tools/audit_sea_progress.py`](tools/audit_sea_progress.py): independent
  replay of retained table and direct evidence.

Detailed contracts:

- [Explicit-CRT producer](docs/explicit_crt_producer.md)
- [Direct-specialization boundary](docs/direct_specialization_boundary.md)
- [SEA proof obligations](docs/sea_design.md)
- [Search integration and operations](docs/search_pipeline.md)

## References

- R. Bröker, K. Lauter, and A. Sutherland,
  [*Modular polynomials via isogeny volcanoes*](https://arxiv.org/abs/1001.0402).
- A. Sutherland,
  [*On the evaluation of modular polynomials*](https://arxiv.org/abs/1202.3985).
