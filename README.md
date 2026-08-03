# OneShotSEA: direct SEA specialization branch

This branch develops the part of OneShotSEA that must replace finite
modular-polynomial tables if the one-shot primality search is to scale past
the current catalog.  It is a correctness-first implementation of
Sutherland-style direct modular-polynomial evaluation, specialized to the two
univariate polynomials that SEA actually consumes:

```text
Phi_ell(x,Y)       and       d/dX Phi_ell(x,Y).
```

The current milestone includes a callback-free classical `j` specialization
for the suitable family `D=-7*3^(2n)`, differentially validated end to end at
level 7.  It also reaches sign-consistent auxiliary-field Weber specialization
at level 5, but that faster path still relies on caller-supplied Weber class
state and does **not** yet have a proved Weber coefficient-height bound.  The
production point counter therefore still uses authenticated target-level
tables.  This branch does not claim a new `nextprime(10^125)` certificate.

## What this branch implements

For one SEA level `ell`, the new path now performs these steps:

1. Select the cited `D=-7*3^(2n)` classical family directly, or discover a
   general suitable order, then independently validate every Definition 1
   predicate and its class number.
2. Select deterministically proved 64-bit auxiliary primes with retained
   `(p,t,v,D)` witnesses satisfying

   ```text
   4p = t^2 - ell^2 v^2 D.
   ```

3. Derive the classical `H_O mod p` internally from fixed exact `Phi_3`
   ring-class resultants, with exact ascending-factor division and degree
   checks.
4. Compute powers in the target field first and lift their canonical integer
   representatives, as required by the direct-evaluation algorithm.
5. Require complete square-free HCP splitting,
   choose the unique trace-signed twist for every surface root, and check the
   expected horizontal/descending edge counts.
6. Enumerate all `ell+1` rational cyclic kernels and construct every Vélu
   quotient over the auxiliary field without consulting `Phi_ell`.
7. Validate a supplied Weber surface class polynomial, use authenticated small
   Weber relations other than the target level to connect both torsors, and
   require exactly two globally opposite floor orientations.
8. Select the unique relative surface/floor sign whose interpolated
   `X^ell Y^ell` coefficient is `-1`; zero or two matches fail closed.
9. Build each signed neighbor polynomial and apply only the two Lagrange linear
   functionals needed for `Phi_ell(x,Y)` and its X derivative.  The producer
   never constructs or loads the bivariate target-level modular polynomial.
10. Stream checked residues into exact centered CRT reconstruction, with strict
   coverage, normalization, and coefficient-bound checks.  The classical `j`
   wrapper derives its bound directly from Sutherland's theorem using exact
   integer arithmetic; Weber remains gated on a normalization-specific proof.
11. Feed a classical specialization directly into normalized-codomain, BMSS
    isogeny reconstruction, and Frobenius eigenvalue validation without
    recovering a bivariate table; when it has no root, certify the Atkin
    projective order from its square-free equal-degree factorization.

The classical level-7 fixture exercises the complete callback-free path over
both a small field and the 416-bit `p125` target; the latter reconstructs from
37 auxiliary primes and yields the independently checked trace residue 5.  The
Weber level-5 fixture exercises the signed auxiliary producer.

## Current boundary

| Component | State |
|---|---|
| Three-power classical suitable order | Implemented directly for every `ell>3` |
| General suitable-order discovery and validation | Implemented, bounded, and deterministic |
| Witnessed auxiliary-prime selection | Implemented for proved 64-bit primes |
| Target-field power lifting | Implemented |
| Complete rational-kernel enumeration and Vélu quotients | Implemented |
| Complete CM `j` surface/floor admission and trace-sign selection | Implemented |
| Fixed-`Phi_3` classical HCP generation mod each auxiliary prime | Implemented for `D=-7*3^(2n)` |
| Callback-free classical `j` specialization, CRT, and BMSS/Frobenius consumer | Implemented and differentially tested at `ell=7` |
| Direct classical Atkin/no-root certification | Implemented and differentially tested at `ell=7` |
| Signed, target-table-free Weber residue | Implemented and differentially tested at `ell=5`, from caller-supplied class/orientation state |
| Exact CRT reconstruction | Implemented and corruption-tested |
| Proved classical Algorithm 1 coefficient bound | Implemented without floating-point logarithms |
| Weber class-polynomial generation and authentication | Not implemented; current Weber API validates caller-supplied state |
| Authentication/selection of small Weber orientation relations | Not wired to the pinned catalog trust type |
| Proved normalization-specific Weber height bound | Not implemented |
| Production replacement of the table catalog | Not enabled |
| Unbounded search or new `p125` certificate | Not demonstrated |

“Target-table-free” here refers to the native auxiliary-prime producer.  It
uses a small target-independent `Phi_37^f` relation to propagate signs and
explicitly rejects `Phi_5^f` as orientation input.  Tests load authenticated
classical and Weber `Phi_5` tables only as independent oracles and demand
coefficient-for-coefficient agreement; production code does not use those
oracles.

## Build and validate

The native build requires a C++20 compiler and GMP.  Run the branch-focused
tests with:

```sh
make test-direct-modpoly test-prime-isogeny test-cm-surface
```

These tests independently check:

- suitable orders at every odd prime level through 997, including agreement
  between two class-number computations and explicit cap failures;
- both trace-parity branches and every retained `(p,t,v,D)` equation;
- rejection of composites, a strong pseudoprime, duplicates, malformed
  residues, insufficient CRT coverage, and inputs outside the proved range;
- exact reconstruction over `F_1009` and the 416-bit `p125` field;
- all six level-5 cyclic kernels on two unrelated CM fixtures, using an
  independent division-polynomial kernel path and an independent Vélu
  coefficient computation;
- complete splitting of `H_-71 mod 1811`, the seven expected surface
  invariants, unique trace-sign admission, and two horizontal plus four
  descending edges at every interpolation point;
- exact reproduction of `H_-567 mod 27847` from the fixed `Phi_3` tower,
  including the ramified level-7 surface with one horizontal and seven
  descending edges, plus final multi-prime CRT and positive trace-residue
  agreement with the independent `Phi_7` path; and
- complete 416-bit `p125` reconstruction of both level-7 channels from 37
  witnessed primes, two Elkies roots, and exact BMSS/Frobenius residue 5; and
- direct level-7 no-root classification over `F_193`, with the same certified
  Atkin projective order and trace-residue set as the full-table oracle; and
- exact agreement of the native classical and signed Weber value/X-derivative
  residues with authenticated `Phi_5` oracles on every lifted-power basis
  probe, including fail-closed tests for
  insufficient torsor generators, mixed surface signs, duplicate relations,
  and attempted use of the target-level table.

The inherited integration, Atkin, eigenvalue, search, certificate, and verifier
tests remain available through `make test-all`.  Magma can optionally be used
as a separate point-count oracle:

```sh
MAGMA=/path/to/magma make test-all
```

Magma, Sage, PARI/GP, and external SEA implementations are not called by the
native producer.

## Soundness boundary

A positive Elkies result has strong downstream BMSS-isogeny and Frobenius
checks.  An Atkin/no-root classification cannot carry the same local witness,
so it depends directly on the authenticity of the specialization.  For that
reason, the new path fails closed.  The classical three-power route now has an
internally derived class polynomial and proved coefficient bound, but remains
outside the production point counter until broader level/scale validation and
integration are complete.  The Weber wrapper additionally awaits a proved
normalization-specific height and authenticated class/relation provenance.

Search heuristics may reorder work, but they may not reject curves.  A
smoothness early abort is sound only after the complete Hasse-compatible trace
set has been enumerated and both the curve and twist orders have been checked.

## Asymptotic status

Let `n = ceil(log2 p)`, use the verifier's smoothness bound `B=n^4`, and require
a certified divisor of size `p^(1/2+o(1))`.  Under the usual
Dickman--Mertens heuristic, a random curve order succeeds with probability
`p^(-1/8+o(1))`, so the expected search is `p^(1/8+o(1))` curves.  If each SEA
count costs only `poly(n)`, that cost is absorbed into the `o(1)` exponent.
This is the intended advantage over the CM search term `p^(1/4+o(1))`.

The branch architecture is compatible with that target: it streams
specializations, avoids a bivariate target-level table, and uses work
polynomial in the interpolation and CRT sizes.  The repository does not yet
establish the repository-wide claim.  The classical family selector and
fixed-size resultant construction are polynomial, but auxiliary primes remain
64-bit and use the paper's unproved fixed-`v` practical selector.  Production
benchmarks, theorem-aligned prime selection, authenticated Weber class/relation
production, and the proved Weber height are still open.

## Why this milestone is worth reviewing

This is a useful, bounded review point for Drew because the branch now has a
complete auxiliary-field classical/Weber fixture and a narrow producer
contract.  A review can confirm the mathematical invariants before the Weber
producer and generalized class-group presentation make the implementation
substantially larger:

1. Are the suitable-order and `(p,t,v,D)` predicates faithful to the direct
   evaluation algorithm?
2. Do the fixed-`Phi_3` ring-class resultants and exact factor removals produce
   the intended `H_O mod p`, including the ramified level-7 case?
3. Does complete HCP splitting plus unique trace-signed twist admission place
   every interpolation curve on the intended CM surface?
4. Are all `ell+1` kernels complete, distinct, and converted to the correct
   Vélu codomains?
5. Do the small Weber relations yield two global floor signs, and does the
   `X^ell Y^ell=-1` test choose the unique correct relative orientation?
6. Do the two interpolation functionals compute exactly the required value
   and X-derivative channels?
7. Are target-field lifting, strict CRT coverage, centered reconstruction, and
   the exact-integer classical height/no-root trust boundary sound?
8. Does square-free equal-degree factorization of a direct no-root
   specialization certify exactly the intended Atkin trace set?

If those answers are yes, the milestone validates the geometric and CRT core
needed by the intended one-shot SEA path.  It does not ask the reviewer to
endorse a finished Weber evaluator, the claimed asymptotic crossover, or a new
large certificate.

## Code map

- [`include/oneshotsea/direct_modpoly.hpp`](include/oneshotsea/direct_modpoly.hpp)
  and [`src/direct_modpoly.cpp`](src/direct_modpoly.cpp): checked orders,
  auxiliary primes, target lifts, residue contracts, and exact CRT.
- [`include/oneshotsea/prime_isogeny.hpp`](include/oneshotsea/prime_isogeny.hpp)
  and [`src/prime_isogeny.cpp`](src/prime_isogeny.cpp): auxiliary-field point
  arithmetic, complete cyclic-kernel enumeration, and Vélu edges.
- [`include/oneshotsea/class_polynomial.hpp`](include/oneshotsea/class_polynomial.hpp)
  and [`src/class_polynomial.cpp`](src/class_polynomial.cpp): three-power
  suitable orders and fixed-`Phi_3` ring-class resultants modulo CRT primes.
- [`include/oneshotsea/cm_surface.hpp`](include/oneshotsea/cm_surface.hpp) and
  [`src/cm_surface.cpp`](src/cm_surface.cpp): HCP splitting, trace-sign CM
  admission, edge classification, and table-free interpolation.
- [`include/oneshotsea/weber_cm_surface.hpp`](include/oneshotsea/weber_cm_surface.hpp)
  and [`src/weber_cm_surface.cpp`](src/weber_cm_surface.cpp): signed Weber
  torsors, relative-orientation rejection, and direct Weber residues.
- [`tests/test_direct_modpoly.cpp`](tests/test_direct_modpoly.cpp),
  [`tests/test_prime_isogeny.cpp`](tests/test_prime_isogeny.cpp), and
  [`tests/test_cm_surface.cpp`](tests/test_cm_surface.cpp): focused positive,
  negative, and independent differential evidence.
- [`docs/explicit_crt_producer.md`](docs/explicit_crt_producer.md): equations,
  proof obligations, and the remaining Weber producer gate.
- [`docs/sea_design.md`](docs/sea_design.md): the integrated SEA/search design
  outside this branch-specific milestone.

## Primary references

- R. Bröker, K. Lauter, and A. Sutherland,
  [*Modular polynomials via isogeny volcanoes*](https://arxiv.org/abs/1001.0402).
- A. Sutherland,
  [*On the evaluation of modular polynomials*](https://arxiv.org/abs/1202.3985).
