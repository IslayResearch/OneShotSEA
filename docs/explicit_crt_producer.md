# Explicit-CRT direct-specialization scaffold

Status: implemented and differentially tested through a native classical
`j`-residue at one auxiliary prime.  Given validated `H_O mod p` state, the
producer admits trace-signed CM surface curves, enumerates every rational
`ell`-kernel and Vélu quotient, and interpolates both required channels without
loading the target-level bivariate modular polynomial.  HCP
generation/authentication, consistent Weber surface/floor signs, and the
proved Weber height bound are not implemented.

## Purpose

Sutherland's direct evaluation computes only the univariate specialization
needed by a curve, rather than materializing the full bivariate modular
polynomial.  For one target field `F_q`, level `ell`, and Weber source `f`, the
output used by this repository is

```text
Phi_ell^f(f,Y)
partial Phi_ell^f/partial X (f,Y).
```

`reconstruct_weber_specialization_algorithm1` now owns the orchestration from
a validated quadratic order through exact CRT reconstruction.  Its callback
receives one checked `SutherlandCrtPrime {p,t,v}` plus the canonical integer
lifts of all required target-field powers, and must return the two specialized
coefficient vectors modulo `p`.  `specialize_classical_from_cm_surfaces`
implements the geometric and interpolation core of that callback in the
classical `j` normalization.  The remaining producer seam is the independently
authenticated HCP input and signed Weber conversion required by production.

## Checked pipeline

### 1. Suitable order

`validate_sutherland_suitable_order` is the only constructor for
`SutherlandSuitableOrder`.  For `D=u^2 D0`, it recomputes and checks:

- `ell` is an odd prime;
- `D0 < -4` is a fundamental quadratic discriminant;
- `ell^2 <= |D| <= c2^2 ell^2` and `4 < |D0| <= c2^2`;
- `ell+2 <= h(D) <= c1 ell`, using the exact count of primitive reduced
  positive-definite binary quadratic forms;
- `gcd(u,2 ell |D0|)=1`; and
- every prime divisor of `u` is at most `min(c2,ell)`.

The defaults are `c1=3/2` and `c2=256`.  The bounded
`discover_sutherland_suitable_order` search enumerates fundamental
discriminants and conductors deterministically.  It uses the exact ring-class
number formula as a cheap filter, but constructs an order only after the
independent reduced-form enumerator agrees with the formula.  Explicit
fundamental-discriminant and conductor-candidate caps fail closed.  This is a
practical discovery engine for current SEA levels, not an optimized
class-group selector used to justify the asymptotic theorem.

The Weber-f wrapper additionally requires `D=1 mod 8` and `3` not dividing
`D`.  These guarantee the class-polynomial splitting condition in the cited
paper.  They do not by themselves prove that each auxiliary Weber value
enumerated by the future volcano producer is a root of the corresponding class
polynomial; that producer must enforce the membership condition.

### 2. Auxiliary CRT primes

`select_sutherland_crt_primes` fixes `v=2` for `D=1 mod 8` and `v=1`
otherwise, then enumerates positive `t=2 mod ell` with the parity required to
make

```text
p = (t^2 - ell^2 v^2 D) / 4
```

an integer.  A retained prime carries all three values `(p,t,v)`.  The code
checks `p=1 mod ell`, requires `p` not to divide `D`, excludes the target
modulus, and stops only once the
product of selected primes is strictly greater than `4H`, where `H` is the
declared absolute bound on every integer coefficient being reconstructed.
Exhausting the explicit candidate cap fails closed.

All current auxiliary primes must fit in 64 bits.  They are proved prime with
the deterministic seven-witness Miller--Rabin basis valid over the entire
unsigned 64-bit range.  GMP probable-prime status alone is not accepted at
this trust boundary.

This is the fixed-`v`, increasing-`t` practical heuristic described in the
paper.  It is deterministic and bounded here, but the paper explicitly does
not prove that fixed `v` will find any primes, even under GRH.  The
theorem-oriented randomized selector varies `(t,v)` and periodically increases
its search range.  Therefore this function is suitable for the present
practical scaffold, not yet evidence for the paper's prime-selection
complexity bound.

### 3. Target powers and streamed residues

Algorithm 1 requires powers to be computed in the target ring and then lifted:

```text
x_i = canonical integer representative of f^i in F_q.
```

The auxiliary residue uses `x_i mod p`; it must not replace this with
`(f mod p)^i`.  `lifted_target_powers` makes this distinction explicit, and the
checked wrapper passes these lifts to every per-prime callback.

For each selected record, the producer callback returns canonical residues for
`ell+2` coefficients of the value polynomial and the X derivative.  The value
must be monic and the derivative's coefficient of `Y^(ell+1)` must be zero.
Wrong sizes, noncanonical coefficients, the wrong returned prime, or invalid
leading coefficients are rejected before reconstruction.

The displayed optional X-derivative summand in Algorithm 1 of the v5 paper
uses `i*a_ij*x_i`, but differentiating `X^i` and the proposition immediately
following the algorithm require `i*a_ij*x_(i-1)`.  The implementation uses the
algebraic derivative with `x_(i-1)` and differentially checks it against direct
symbolic table differentiation.

### 4. CM surface admission and direct interpolation

`enumerate_cm_interpolation_surfaces` accepts a checked suitable order and
auxiliary-prime witness plus a caller-supplied `H_O mod p`.  It revalidates the
prime equation, field, degree, monicity, square-freeness, and complete splitting
into `h(O)` roots.  For the first `ell+2` roots it constructs the canonical
`j`-curve and its quadratic twist, requires exactly one twist to expose all
`ell+1` rational kernels for order `p+1-t`, and checks the expected
`1+(D/ell)` horizontal-edge count.

`specialize_classical_from_cm_surfaces` forms every neighbor polynomial from
the table-free Vélu codomains.  It does not materialize the interpolated
bivariate polynomial: for each Lagrange basis row it computes only evaluation
against the lifted target powers and the corresponding algebraic derivative
functional.  The result is the two classical `j` residue vectors expected by
the CRT interface.

The function validates the finite-field consequences of the supplied class
polynomial but does not establish its mathematical provenance.  An opaque,
authenticated HCP producer type is still required before this state can cross
a production no-root trust boundary.

### 5. Exact CRT and height check

The high-level Weber wrapper accepts a `CrtCoefficientBound`, not a raw
integer.  Its constructor is private.  At present, the only available evidence
is `exact_table_reference`, obtained by evaluating every table term after
target-power lifting and bounding both the value and X-derivative channels.
This makes table differential tests exact while making it impossible to pass a
guessed empirical maximum through the production-facing API.  A future proved
Weber height formula must add a distinct evidence kind and checked constructor.

Let `M` be the product of the distinct primes `p_i`, `M_i=M/p_i`, and
`a_i=M_i^-1 mod p_i`.  For one residue coefficient `c_i`, the implementation
forms

```text
z_i = c_i a_i mod p_i
T   = sum_i z_i M_i
r   = nearest integer to T/M
C   = T-rM.
```

All terms are exact GMP integers; there is no floating-point approximation.
Because the prime product is required to exceed `4H`, the intended integer
coefficient has a unique centered representative.  Every computed `C` is
checked against `|C| <= H`.  The implementation independently accumulates the
explicit-CRT expression modulo `q` and verifies that it agrees with `C mod q`
before constructing `ModularPolynomialSpecialization`.

The derivative with respect to `Y` is derived locally from the reconstructed
value polynomial.  The per-prime producer therefore needs to emit only the
value and X-derivative channels.

## Validation evidence

`tests/test_direct_modpoly.cpp` covers:

- published small discriminant/class-number fixtures;
- valid and invalid suitable orders, including a Weber-incompatible but
  otherwise suitable order;
- deterministic suitable-order discovery at every odd prime level from 5
  through 997 (all 166 catalog levels), agreement between the ring-class
  formula and reduced-form enumeration, stable evidence counts, and
  bounded-search failures;
- even- and odd-trace prime-selection parity;
- exact prime equations, deterministic selection, product coverage, and
  candidate-cap failure;
- deterministic rejection of a strong pseudoprime and inputs above the
  64-bit proof range;
- signed synthetic coefficients over `F_1009` and over the 416-bit `p125`
  target;
- auxiliary-prime permutation invariance;
- insufficient product, duplicate/composite moduli, wrong provider identity,
  malformed leading terms, and deliberate residue corruption; and
- an end-to-end `ell=5`, `D=-71` reconstruction whose suitable-order
  validation, selected `(p,t,v)` records, lifted powers, streamed table oracle,
  value, X derivative, and derived Y derivative all agree with direct
  target-field table evaluation.

Run it with:

```sh
make test-direct-modpoly test-prime-isogeny test-cm-surface
```

`tests/test_cm_surface.cpp` additionally checks complete splitting of
`H_-71 mod 1811`, all seven surface invariants, unique trace-sign admission,
two horizontal and four descending edges in every row, and exact agreement of
both native residue channels with an authenticated classical `Phi_5` oracle.
The full-table adapter exists only as this independent fixture; it is not used
by the native producer and cannot supply a production claim.

## Complexity and the asymptotic claim

This scaffold removes several architectural blockers to polynomial-time direct
evaluation: it does not require a bivariate table at its public boundary, it
streams one auxiliary specialization at a time, and its exact CRT work is
polynomial in the coefficient count and total CRT bit length.  Its exact
integer accumulation is deliberately conservative and may later be replaced
by the faster explicit-CRT approximation from the paper after a differential
and error-bound proof.

That does not yet establish the repository's end-to-end asymptotic claim.  In
particular:

- suitable orders are discovered only by a bounded practical search, and the
  class-number validator is not an optimized class-group routine;
- the fixed-`v` practical prime search is not the randomized selector used by
  the asymptotic theorem;
- no Hilbert class polynomial state is generated or authenticated (a supplied
  split polynomial is checked and its `j` roots are consumed);
- classical `j` surface/floor rows are enumerated, but consistent Weber lifts
  and their relative sign rules are not; and
- the high-level API rejects untyped/empirical bounds, but a proved,
  normalization-specific Weber coefficient bound `H` is not yet implemented.

These missing operations are the ones that must satisfy Sutherland's
polynomial-in-`log q` bounds.  Until they do, production still depends on the
finite authenticated table catalog, so the `p^(1/8+o(1))` search exponent is a
design target supported by the smoothness heuristic, not a completed
implementation theorem.

## Next implementation gate

Complete the Weber callback for one small level first:

1. implement a proved Weber-specific height derivation, then derive or load
   independently authenticated Hilbert-class-polynomial state
   for the validated order;
2. convert the admitted classical surface and floor rows to Weber invariants
   with consistent surface/floor signs, failing closed on ambiguity;
3. apply the already implemented two interpolation functionals in the Weber
   normalization without loading `Phi_ell^f(X,Y)`; and
4. compare every per-prime residue and final target specialization with the
   authenticated table oracle before permitting any no-root result.

The first production connection must use the future proved-Weber evidence kind;
the existing exact-table evidence remains a bounded differential oracle.

### Implemented surface-edge primitive

`enumerate_rational_prime_isogenies` implements the subgroup step used in BLS
Algorithm 2.1.  Given a surface curve and its exact CM group order, it removes
the prime-to-`ell` component of sampled rational points, reduces each surviving
point to exact order `ell`, and constructs the monic kernel polynomial.  It
returns only after finding the `ell+1` distinct kernels that prove full
rational `E[ell]`; exhausting the explicit x-coordinate cap is an error.

For every subgroup, the codomain is computed from direct point-sum Vélu
formulas, without a target-level modular polynomial.  Level-5 tests over the
CM discriminants `-19` and `-11` exercise both modular-square-root branches and
compare all twelve quotients with the independent division-kernel Vélu path,
authenticated classical modular-polynomial roots, and brute-force group
orders.  The CM-surface layer now consumes all roots of a supplied split HCP,
selects the correct trace twist, classifies the edges, and interpolates both
classical `j` channels.  It does not generate or authenticate the HCP or choose
consistent Weber surface/floor signs.

## Focused review checklist

A mathematical review can remain narrow:

- Definition 1 predicates and class-number enumeration;
- necessary Weber order congruences and the separate auxiliary-value
  class-membership obligation;
- choice and parity of `t` and `v` in the prime equation;
- target-field power/lift ordering;
- strict CRT coverage and centered rounding; and
- whether the two output channels match the normalization required by
  [`docs/direct_specialization_boundary.md`](direct_specialization_boundary.md).

Primary references:

- R. Bröker, K. Lauter, and A. Sutherland, [*Modular polynomials via
  isogeny volcanoes*](https://arxiv.org/abs/1001.0402).
- A. Sutherland, [*On the evaluation of modular
  polynomials*](https://arxiv.org/abs/1202.3985), especially Definition 1,
  Algorithm 1, Theorem 1, and Section 3.9.
