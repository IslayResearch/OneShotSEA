# OneShotSEA

OneShotSEA is a custom C++20/GMP implementation of the SEA route to one-shot
elliptic-curve primality proofs.  The immediate targets are
`nextprime(10^125)` (`p125`, 416 bits) and then `nextprime(10^130)` (`p130`,
432 bits).  The production point-count and search path does not call PARI/GP,
Sage, Magma, or another SEA implementation.

## Current status

This is a working, sound search implementation over an authenticated finite
catalog of Weber-f modular polynomials.  It is not yet a completed unbounded
direct-evaluation implementation, and no new `p125` certificate has been found.

The branch now contains both sides of the direct-specialization seam:

- the SEA consumer accepts only `Phi_ell^f(f,Y)` and
  `partial Phi_ell^f/partial X (f,Y)`, without needing a bivariate table; and
- the new Algorithm 1 scaffold validates suitable quadratic orders, selects
  witnessed auxiliary CRT primes, streams per-prime specializations, and
  performs bounded explicit CRT reconstruction into that consumer object.

The remaining major subsystem is the callback that computes each auxiliary-
prime specialization from Hilbert-class-polynomial and Weber isogeny-volcano
state.  Until it exists and is connected to production, the authenticated
table catalog remains the actual specialization source.

## Direct specialization implemented on this branch

[`src/direct_modpoly.cpp`](src/direct_modpoly.cpp) implements the checkable
orchestration around Sutherland's direct-evaluation algorithm:

1. count primitive reduced quadratic forms and validate every suitable-order
   condition, including the exact class-number interval;
2. enforce the necessary order congruences for the Weber-f class polynomial;
3. deterministically select primes satisfying
   `4p = t^2 - ell^2 v^2 D`, retaining `(p,t,v)` for the residue producer;
4. compute powers in the target field and then lift their canonical
   representatives, rather than incorrectly exponentiating modulo each CRT
   prime;
5. consume `Phi_ell^f(f,Y)` and its X derivative from a streamed per-prime
   provider; and
6. reconstruct every coefficient with exact CRT rounding, require the CRT
   product to exceed four times a declared coefficient bound, and reject any
   centered lift outside that bound.

Auxiliary primes are currently restricted to 64 bits and proven prime by a
deterministic Miller--Rabin basis.  The large target modulus is only subjected
to a probable-prime precondition because proving it is the purpose of the
overall program.

The table-backed residue adapter is a test oracle, not the production direct
evaluator.  The exact contract, equations, validation evidence, and remaining
work are in [`docs/explicit_crt_producer.md`](docs/explicit_crt_producer.md)
and [`docs/direct_specialization_boundary.md`](docs/direct_specialization_boundary.md).

## Existing SEA and certificate path

For the authenticated level schedule, the repository can:

- generate certificate-compatible curves from optimized X1(27), X1(11), or
  Weber families while retaining the exact Weber source coordinate;
- specialize authenticated Weber tables, reconstruct normalized isogenies
  with BMSS, and prove Frobenius trace residues in kernel quotient algebras;
- combine exact Elkies residues with separately certified Atkin constraints;
- enumerate the complete Hasse-compatible trace set and apply a sound early
  abort only when every curve/twist order candidate fails the exact smoothness
  threshold;
- construct an exact-order Montgomery point and emit the canonical one-line
  proof; and
- accept a result only after the pinned, unmodified upstream verifier returns
  true.

The checked-in schedule has 77 Weber-f levels through 401.  An authenticated
source catalog can selectively materialize 166 levels through 997.  This
extends the practical range but is still finite source data, not the intended
asymptotic direct evaluator.

## Build and focused validation

The native build requires a C++20 compiler and GMP.  Smooth-engine tests also
require OpenMP (`libomp` with Apple Clang or the GCC runtime on Linux).

```sh
make all
make test-direct-modpoly
make test test-poly-square test-atkin test-factor test-certificate \
  test-search-pipeline test-cli test-verifier test-performance-artifacts
```

`test-direct-modpoly` independently checks:

- known quadratic-order class numbers and rejected invalid orders;
- suitable-order bounds and both trace-parity branches;
- the exact `(p,t,v,D)` prime equation and deterministic prime selection;
- deterministic rejection of composites, a strong pseudoprime, duplicate
  primes, and auxiliary inputs outside the proven 64-bit range;
- signed explicit-CRT reconstruction over `F_1009` and the 416-bit `p125`
  field, including corruption and insufficient-height failures;
- the target-power lift subtlety in Algorithm 1; and
- coefficient-for-coefficient agreement of the complete validated-order,
  selected-prime, streamed-provider, CRT path with a Weber `Phi_5` table oracle.

The broader suite includes native arithmetic and certificate tests plus
independent Magma point-count differentials when a Magma executable is
provided:

```sh
MAGMA=/path/to/magma make test-all
```

The production search requires a separately built authenticated smooth cache.
See [`docs/search_pipeline.md`](docs/search_pipeline.md) for the exact command,
trust boundary, and checkpoint semantics.

## Soundness boundary

Positive Elkies results are checked by normalized-codomain, BMSS-isogeny, and
Frobenius identities.  A no-root classification cannot receive the same local
witness, so it depends on authentic modular-polynomial coefficients.  For this
reason the direct path must not influence production Atkin/no-root conclusions
until its per-prime volcano producer and a proved Weber coefficient-height
bound have passed differential tests.

Likewise, early smoothness rejection is permitted only from a complete trace
set and exact smooth parts for both signs.  Heuristic scheduling and learned
scores may prioritize work, but cannot reject a candidate or produce a proof.

## Asymptotic claim—precisely scoped

Let `n = ceil(log2 p)`, let the verifier smoothness bound be `B=n^4`, and let a
certificate require a divisor of size `p^(1/2+o(1))`.  The usual
Dickman--Mertens model predicts success probability `p^(-1/8+o(1))` per random
order, so the expected number of curves is `p^(1/8+o(1))`.  If each curve is
point-counted in time polynomial in `n`, that polynomial is absorbed by the
`o(1)` exponent.  This is the heuristic asymptotic advantage over the CM
search term `p^(1/4+o(1))`.

The new suitable-order selection and exact CRT layers are polynomial-size
operations and do not introduce a new power of `p`.  But the repository does
**not yet demonstrate the full asymptotic claim**: production still reads a
finite modular-polynomial catalog, suitable-order discovery is not implemented,
the class-number routine is a correctness-first enumerator, and the
Hilbert-class-polynomial/volcano residue producer is missing.  The claim becomes
an implementation property only after those gaps close while keeping the
per-curve cost polynomial in `n`.

The current fixed-`v`, increasing-`t` prime selector is specifically the
paper's practical heuristic.  Its explicit cap makes failure safe, but it is
not the randomized prime-selection algorithm used for the paper's GRH
complexity theorem.  A theorem-aligned selector or a justified hybrid is still
needed before attaching that bound to this implementation.

The exponents themselves remain heuristic even then.  Curve/twist dependence,
torsion conditioning, group-exponent restrictions, and exact-order
representability affect constants and potentially lower-order terms.  See
[`docs/sea_design.md`](docs/sea_design.md#05-asymptotic-expectation-and-measured-boundary)
for the derivation.

## Why focused mathematical review is worthwhile now

The current milestone isolates a small, consequential trust boundary before
the much larger volcano implementation is written.  A reviewer can now check
five concrete questions without reviewing the entire search engine:

1. Are the suitable-order and necessary Weber order-congruence predicates
   faithful to the cited algorithm?
2. Does prime selection cover the required parity cases and preserve valid
   `(p,t,v)` witnesses?
3. Is target-field powering followed by integer lifting implemented in the
   correct order?
4. Is the strict `product > 4H` termination condition sufficient and is every
   centered coefficient checked against the same declared bound?
5. Does the callback return precisely the value and X-derivative polynomials
   needed by the already validated BMSS/Frobenius consumer?

Confirming these now is valuable: the answers determine the API and invariants
for the remaining volcano producer, and an error here could otherwise create
plausible but unsound no-root evidence.  Review is not being requested as a
claim that the direct evaluator or a `p125` proof is finished.

## Repository map

- [`docs/explicit_crt_producer.md`](docs/explicit_crt_producer.md): new
  Algorithm 1 scaffold, proof obligations, tests, and open producer work.
- [`docs/direct_specialization_boundary.md`](docs/direct_specialization_boundary.md):
  exact handoff into Weber/BMSS/Frobenius SEA.
- [`docs/sea_design.md`](docs/sea_design.md): authoritative as-built design and
  asymptotic analysis.
- [`docs/search_pipeline.md`](docs/search_pipeline.md): production search,
  checkpoint, cache, and verifier boundaries.
- [`docs/bottleneck_registry.md`](docs/bottleneck_registry.md): current outcome
  gates and measured bottlenecks.
- [`docs/weber_implementation.md`](docs/weber_implementation.md): Weber
  normalization and point-count details.

## References and upstream context

- Bröker, Lauter, and Sutherland,
  [*Modular polynomials via isogeny volcanoes*](https://arxiv.org/abs/1001.0402).
- Sutherland,
  [*On the evaluation of modular polynomials*](https://arxiv.org/abs/1202.3985).
- [AndrewVSutherland/OneShotPrimalityProofs](https://github.com/AndrewVSutherland/OneShotPrimalityProofs)
- [AndrewVSutherland2/OneShotFastECPP](https://github.com/AndrewVSutherland2/OneShotFastECPP)
