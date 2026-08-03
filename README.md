# OneShotSEA: direct SEA specialization branch

This branch develops the part of OneShotSEA that must replace finite
modular-polynomial tables if the one-shot primality search is to scale past
the current catalog.  It is a correctness-first implementation of
Sutherland-style direct modular-polynomial evaluation, specialized to the two
univariate polynomials that SEA actually consumes:

```text
Phi_ell(x,Y)       and       d/dX Phi_ell(x,Y).
```

The current milestone reaches from checked quadratic-order selection through
table-free auxiliary-field `j`-specialization.  It does **not** yet provide the
signed Weber surface/floor evaluator or a proved Weber coefficient-height
bound, so the production point counter still uses authenticated tables.  This
branch does not claim a new `nextprime(10^125)` certificate.

## What this branch implements

For one SEA level `ell`, the new path now performs these steps:

1. Discover and independently validate a suitable imaginary quadratic order
   using the ring-class formula and primitive reduced-form enumeration.
2. Select deterministically proved 64-bit auxiliary primes with retained
   `(p,t,v,D)` witnesses satisfying

   ```text
   4p = t^2 - ell^2 v^2 D.
   ```

3. Compute powers in the target field first and lift their canonical integer
   representatives, as required by the direct-evaluation algorithm.
4. Validate a supplied `H_O mod p`, require complete square-free splitting,
   choose the unique trace-signed twist for each interpolation root, and check
   the expected horizontal/descending edge counts.
5. Enumerate all `ell+1` rational cyclic kernels and construct every Vélu
   quotient over the auxiliary field without consulting `Phi_ell`.
6. Build each neighbor polynomial and apply only the two Lagrange linear
   functionals needed for `Phi_ell(x,Y)` and its X derivative.  The producer
   never constructs or loads the bivariate target-level modular polynomial.
7. Stream checked residues into exact centered CRT reconstruction, with strict
   coverage, normalization, and coefficient-bound checks.

The level-5 fixture exercises steps 1--6 end to end.  The CRT layer is
independently exercised with signed synthetic data, the 416-bit `p125` target,
and authenticated-table differential oracles.

## Current boundary

| Component | State |
|---|---|
| Suitable-order discovery and validation | Implemented, bounded, and deterministic |
| Witnessed auxiliary-prime selection | Implemented for proved 64-bit primes |
| Target-field power lifting | Implemented |
| Complete rational-kernel enumeration and Vélu quotients | Implemented |
| CM `j`-surface admission and trace-sign selection | Implemented |
| Table-free classical `j` residue at an auxiliary prime | Implemented and differentially tested at `ell=5` |
| Exact CRT reconstruction | Implemented and corruption-tested |
| Hilbert class polynomial generation/authentication | Not implemented; current API validates caller-supplied state |
| Signed Weber surface/floor enumeration | Not implemented |
| Proved normalization-specific Weber height bound | Not implemented |
| Production replacement of the table catalog | Not enabled |
| Unbounded search or new `p125` certificate | Not demonstrated |

“Table-free” here refers to the native auxiliary-prime producer.  Tests still
load an authenticated classical `Phi_5` as an independent oracle and demand
coefficient-for-coefficient agreement; production code does not use that
oracle.

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
  descending edges at every interpolation point; and
- exact agreement of the native value and X-derivative residues with an
  authenticated classical `Phi_5` oracle.

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
reason, the new path fails closed and remains outside production until the
Weber sign rules, class-invariant provenance, and coefficient-height proof are
implemented and differentially validated.

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
establish the end-to-end claim.  Its order and auxiliary-prime searches are
bounded practical selectors, and the missing HCP/Weber producer and proved
height bound still have to meet the cited polynomial-time analysis.

## Why this milestone is worth reviewing

This is a useful, bounded review point for Drew because the branch now has a
complete auxiliary-field `j` fixture and a narrow producer contract.  A review
can confirm the mathematical invariants before Weber sign propagation makes
the implementation substantially larger:

1. Are the suitable-order and `(p,t,v,D)` predicates faithful to the direct
   evaluation algorithm?
2. Does complete HCP splitting plus unique trace-signed twist admission place
   every interpolation curve on the intended CM surface?
3. Are all `ell+1` kernels complete, distinct, and converted to the correct
   Vélu codomains?
4. Do the two interpolation functionals compute exactly the required value
   and X-derivative channels?
5. Are target-field lifting, strict CRT coverage, centered reconstruction, and
   the no-root trust boundary sound?

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
- [`include/oneshotsea/cm_surface.hpp`](include/oneshotsea/cm_surface.hpp) and
  [`src/cm_surface.cpp`](src/cm_surface.cpp): HCP splitting, trace-sign CM
  admission, edge classification, and table-free interpolation.
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
