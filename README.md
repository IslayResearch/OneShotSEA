# OneShotSEA: direct specialization milestone

This branch is a correctness-first implementation milestone toward a custom,
unbounded SEA point counter for one-shot primality proofs.  Its new work is the
front half and back half of Sutherland's direct modular-polynomial evaluation
path: it discovers and validates suitable quadratic orders, selects witnessed
auxiliary primes, and reconstructs a Weber-f specialization with exact CRT.

It does **not** yet implement the Hilbert-class-polynomial/Weber-volcano
callback that produces the residue at each auxiliary prime.  Consequently the
production search still obtains modular-polynomial data from an authenticated,
finite table catalog.  No new `nextprime(10^125)` (`p125`, 416-bit) certificate
is claimed here.

## Branch status

| Component | Status on this branch | Production consequence |
|---|---|---|
| Specialized SEA consumer for `Phi_ell^f(f,Y)` and `Phi_X^f(f,Y)` | Implemented and differentially tested | SEA no longer requires a bivariate table at its consumer boundary |
| Suitable-order discovery and validation | Implemented as a bounded deterministic search | Correct for accepted results; not the theorem-oriented asymptotic selector |
| Auxiliary CRT-prime selection | Implemented with exact `(p,t,v,D)` witnesses and proved 64-bit primes | Safe bounded practical heuristic; fixed `v` has no GRH existence guarantee |
| Target-field power lifting | Implemented | Preserves the ordering required by Algorithm 1 |
| Explicit CRT reconstruction | Implemented with strict product and coefficient-bound checks | Reconstructs both required specialization channels and fails closed |
| Weber coefficient-height bound | Exact table-derived test evidence only | A proved normalization-specific bound is still required |
| Per-prime HCP/Weber-volcano residue producer | Not implemented | Direct specialization cannot yet replace tables in production |
| Unbounded SEA search or `p125` certificate | Not demonstrated | The intended asymptotic path remains incomplete |

## What this branch adds

The branch-specific implementation is concentrated in
[`include/oneshotsea/direct_modpoly.hpp`](include/oneshotsea/direct_modpoly.hpp)
and [`src/direct_modpoly.cpp`](src/direct_modpoly.cpp).  It provides:

1. `discover_sutherland_suitable_order`, a capped deterministic search over
   fundamental discriminants and conductors.  A ring-class-number formula is
   used as a filter, then independently checked by primitive reduced-form
   enumeration before an order is accepted.
2. `validate_sutherland_suitable_order`, the only construction path for the
   checked order object.  It enforces Sutherland's size, class-number,
   conductor, and prime-level conditions, plus the necessary Weber-f order
   congruences where the Weber wrapper is used.
3. `select_sutherland_crt_primes`, which retains every equation witness
   satisfying

   ```text
   4p = t^2 - ell^2 v^2 D
   ```

   and stops only when the product of distinct auxiliary primes is strictly
   greater than four times the declared coefficient bound.
4. `reconstruct_weber_specialization_algorithm1`, which computes powers in
   the target field before lifting them, streams one residue at a time, and
   uses exact integer CRT rounding to reconstruct `Phi_ell^f(f,Y)` and its X
   derivative.  Each centered coefficient is checked against the same bound
   and cross-checked modulo the target.

The residue provider is an explicit callback.  The table-backed implementation
used by the tests is an oracle for differential validation, not the missing
production evaluator.

## What is inherited

The repository already contains the table-backed Weber SEA and one-shot
certificate search path: curve generation, modular-polynomial authentication,
BMSS isogeny recovery, Frobenius trace residues, certified Atkin constraints,
complete-trace early abort, exact smoothness checks, certificate assembly, and
the pinned upstream verifier.

Those components are important integration context, but they are not the new
claim of this branch.  They supply the independently exercised consumer against
which the direct-specialization boundary is tested.  Production continues to
use them with the finite authenticated table schedule until the missing
per-prime producer and proved height bound are complete.

## Build and validate this milestone

The native build requires a C++20 compiler and GMP.  Run the focused test with:

```sh
make test-direct-modpoly
```

That test independently checks:

- known class numbers and invalid-order rejection;
- deterministic suitable-order discovery for all 166 admissible catalog
  levels through 997, including formula/enumeration agreement and cap failure;
- both trace-parity branches and every retained CRT-prime equation;
- deterministic rejection of composites, a strong pseudoprime, duplicates,
  and auxiliary inputs outside the proved 64-bit range;
- signed exact-CRT reconstruction over `F_1009` and the 416-bit `p125` field;
- insufficient coverage, malformed or corrupted provider output, and the
  target-power lift subtlety; and
- coefficient-for-coefficient agreement of the complete level-5 path with an
  authenticated Weber table oracle.

The inherited core, Atkin, eigenvalue, search, and certificate suites remain
available through the Makefile.  The full suite can additionally use Magma as
an independent point-count oracle:

```sh
MAGMA=/path/to/magma make test-all
```

Magma is optional test infrastructure; the production point counter does not
call Magma, Sage, PARI/GP, or another SEA implementation.

## Soundness boundary

A positive Elkies result is protected downstream by normalized-codomain,
BMSS-isogeny, and Frobenius identities.  A no-root/Atkin result cannot carry
the same local witness and therefore depends on the authenticity of the
specialization coefficients.  The new CRT path must not influence production
no-root conclusions until its volcano producer and proved Weber height bound
pass the differential gates.

Likewise, search heuristics may schedule work but may not reject a curve.
Early smoothness rejection is sound only after enumerating the complete
Hasse-compatible trace set and computing exact smooth parts for both the curve
and its twist.

## Asymptotic scope

Let `n = ceil(log2 p)`, use the verifier's smoothness bound `B=n^4`, and require
a certified divisor of size `p^(1/2+o(1))`.  Under the usual
Dickman--Mertens model, a random order succeeds with probability
`p^(-1/8+o(1))`, giving an expected `p^(1/8+o(1))` curves.  A custom SEA point
count whose cost is polynomial in `n` is absorbed into the `o(1)` exponent;
this is the intended heuristic advantage over the CM search term
`p^(1/4+o(1))`.

This branch does not yet establish that end-to-end property.  Its order search
is bounded and correctness-oriented, its practical fixed-`v` prime selector is
not the randomized selector used in Sutherland's GRH analysis, and the
HCP/volcano producer plus a proved Weber height bound are absent.  Until those
pieces are implemented with polynomial-in-`n` cost, the exponent is a design
target supported by the smoothness heuristic—not a measured or proved scaling
property of the current production program.

## Why review is useful now

This is a useful point for Drew to review because the branch isolates a small
mathematical trust boundary before the much larger volcano evaluator is built.
The review can answer five concrete questions:

1. Are the suitable-order predicates and independent class-number checks
   faithful to the cited algorithm?
2. Are the parity cases and `(p,t,v,D)` prime witnesses correct?
3. Are target powers computed in the target field and only then lifted, as
   Algorithm 1 requires?
4. Do the strict CRT coverage, centered rounding, and common coefficient bound
   justify every reconstructed coefficient?
5. Does the callback return exactly the value and X-derivative polynomials
   required by the already validated BMSS/Frobenius consumer?

Agreement on these invariants now fixes the producer API and prevents the
remaining implementation from being built against a wrong normalization.  It
is not a request to endorse a completed direct evaluator, an unbounded
complexity claim, or a `p125` proof.

## Review map

- [`include/oneshotsea/direct_modpoly.hpp`](include/oneshotsea/direct_modpoly.hpp):
  public checked types and the residue-provider contract.
- [`src/direct_modpoly.cpp`](src/direct_modpoly.cpp): order discovery,
  validation, prime selection, power lifts, and exact CRT.
- [`tests/test_direct_modpoly.cpp`](tests/test_direct_modpoly.cpp): focused
  positive, negative, corruption, and table-differential evidence.
- [`docs/explicit_crt_producer.md`](docs/explicit_crt_producer.md): equations,
  proof obligations, limitations, and the next implementation gate.
- [`docs/direct_specialization_boundary.md`](docs/direct_specialization_boundary.md):
  exact handoff to Weber/BMSS/Frobenius SEA.
- [`docs/sea_design.md`](docs/sea_design.md): inherited as-built SEA/search
  design and the detailed asymptotic model.

## Primary references

- R. Bröker, K. Lauter, and A. Sutherland,
  [*Modular polynomials via isogeny volcanoes*](https://arxiv.org/abs/1001.0402).
- A. Sutherland,
  [*On the evaluation of modular polynomials*](https://arxiv.org/abs/1202.3985).
