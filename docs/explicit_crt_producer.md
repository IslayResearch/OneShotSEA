# Explicit-CRT direct-specialization scaffold

Status: the classical `j` path is implemented end to end for the suitable
family `D=-7*3^(2n)`, including internally derived class polynomials, both
residue channels, the proved height, and exact CRT.  It is differentially
tested against authenticated classical tables through `ell=7` and against an
independent Schoof residue at `ell=11` on the 416-bit target. The signed Weber
conversion is implemented at one auxiliary prime, but Weber
class-polynomial/relation authentication and the proved Weber height bound are
not implemented.

## Purpose

Sutherland's direct evaluation computes only the univariate specialization
needed by a curve, rather than materializing the full bivariate modular
polynomial.  For one target field `F_q`, level `ell`, and Weber source `f`, the
output used by this repository is

```text
Phi_ell^f(f,Y)
partial Phi_ell^f/partial X (f,Y).
```

`reconstruct_classical_specialization_from_cm` owns the checked classical
orchestration from a validated quadratic order through exact CRT
reconstruction.  At each retained prime it internally derives `H_O mod p`,
enumerates the CM surface, and emits the two specialization channels.
`reconstruct_weber_specialization_algorithm1` retains a narrow callback while
the signed Weber producer's class state and small orientation relations remain
outside the authenticated boundary.

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

For the classical path, `derive_three_power_suitable_order` directly selects
the family `D=-7*3^(2n)` that the paper states is suitable for every
`ell>3` with `c1=4,c2=16`.  The ordinary validator still recomputes every
Definition 1 predicate and the exact class number.  This removes bounded
search from the classical order-selection path while preserving a separate
independent check.

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

The checked overload of `enumerate_cm_interpolation_surfaces` accepts an opaque
`ClassicalCmClassPolynomial`, not caller coefficients.  For the three-power
family, `derive_three_power_class_polynomial_mod_prime` starts from
`H_-7=X+3375` and the fixed exact classical `Phi_3`.  It follows the ring-class
tower with the exact identities

```text
Res_X(H_1(X), Phi_3(X,Y)) = H_3(Y)
Res_X(H_f(X), Phi_3(X,Y))
  = H_(f/3)(Y)^[h(f)/h(f/3)] H_(3f)(Y),  3 | f.
```

The resultant is computed directly in `F_p[Y]`: `H_f` is first reduced modulo
the monic quartic `Phi_3` in `X`, then a Sylvester matrix of size at most seven
is evaluated with fraction-free Bareiss elimination.  Every ascending-factor
division, expected class number, degree, and monic normalization is checked.
This avoids floating CM approximation and the much larger integer HCP.

The surface layer revalidates the prime equation, field, degree, monicity,
square-freeness, and complete splitting into `h(O)` roots.  At every root it
constructs the canonical `j`-curve and its quadratic twist, requires exactly
one twist to expose all `ell+1` rational kernels for order `p+1-t`, and checks
the expected `1+(D/ell)` horizontal-edge count, including the ramified value
one.

`specialize_classical_from_cm_surfaces` forms every neighbor polynomial from
the table-free Vélu codomains.  It does not materialize the interpolated
bivariate polynomial: for each Lagrange basis row it computes only evaluation
against the lifted target powers and the corresponding algebraic derivative
functional.  The result is the two classical `j` residue vectors expected by
the CRT interface.

The reusable search context performs the same checked surface admission once,
then stores only each witness's square Lagrange and neighbor-coefficient
matrices. Full auxiliary curves, kernels, and isogenies are released. Later
curves evaluate the two linear functionals from those immutable matrices, so
the compact representation changes neither interpolation order nor CRT
residues.

The raw-polynomial overload remains for isolated fixtures, but the complete
classical entry point cannot receive it.  It generates the opaque object for
each witnessed prime and immediately feeds it to the checked surface overload.

### 5. Signed Weber torsors and relative orientation

For the Weber path, `select_sutherland_weber_crt_primes` retains only
`p=11 mod 12`.  BLS Lemma 7.3 then guarantees that each admitted surface or
floor `j`-invariant has exactly the pair `f,-f` over the auxiliary field.
`specialize_weber_from_cm_surfaces` checks this consequence explicitly.

A supplied Weber surface class polynomial must split square-freely and map
bijectively to the complete classical surface.  One or more independently
authenticated, target-independent small Weber modular polynomials must connect
that signed surface and split the doubled floor candidates into exactly two
connected components, each containing one lift of every floor invariant.  The
components must be global negatives.  The target-level relation is rejected.

The producer constructs both possible surface/floor orientations and computes
only the interpolated coefficient of `X^ell Y^ell`.  Exactly one must equal
`-1`, the normalization specified by BLS Section 7.3.  If neither or both do,
the auxiliary prime is rejected.  Once selected, the same Lagrange linear
functionals produce the Weber value and X-derivative residues directly.

This implements the paper's heuristic ambiguity check faithfully and detects
the ambiguity it warns about.  Authenticity of the supplied class polynomial
and small relations remains an explicit caller obligation until opaque pinned
producer types are connected.

### 6. Exact CRT and height check

`CrtCoefficientBound` has a private constructor, so no high-level wrapper can
accept a guessed empirical maximum.  `exact_table_reference` evaluates every
table term after target-power lifting and exists only for differential tests.

The classical wrapper instead uses
`proved_classical_algorithm1`.  Sutherland's Algorithm 1 proves the logarithmic
bound

```text
B = 6*ell*log(ell) + 18*ell + log(q) + 3*log(ell+2)
```

for the value and optional derivative channels.  The implementation converts
this to an absolute integer bound without floating point:

```text
H = ceil(q*(ell+2)^3*ell^(6ell)*(11/4)^(18ell)).
```

The elementary series estimate `e < 11/4` proves `H >= exp(B)`.  This is
slightly conservative but cannot round downward.  The checked classical
orchestrator derives `H` internally, selects enough witnessed primes, and does
not accept external bound evidence.  A separate normalization-specific proof
is still required for the much smaller Weber bound; the paper's stated Weber
formula is explicitly heuristic and is not admitted here.

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
- exact agreement of the theorem-derived classical bound with its rational
  integer formula, plus end-to-end classical reconstruction of both channels;
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
both native classical residue channels with an authenticated `Phi_5` oracle.
The Weber fixture uses the published degree-seven Weber class polynomial for
`D=-71` and the target-independent `Phi_37^f` relation to orient all 28 floor
invariants.  Exactly one global floor sign yields `X^5Y^5=-1`, and both emitted
channels match the authenticated `Phi_5^f` oracle on every lifted-power basis
probe, thereby checking the complete interpolated coefficient matrix.  Missing generators,
`Phi_19^f`'s eight insufficient components, mixed surface signs, duplicate
relations, and target-level `Phi_5^f` input are rejected.

The same test independently fixes every integer coefficient of `H_-567` and
requires the native fixed-`Phi_3` tower to reproduce its reduction modulo
`27847`.  This exercises the paper's `ell=7,D=-567` suitable order, where 7
ramifies and every surface has one horizontal plus seven descending edges.
The native value and X-derivative residues agree with the independent
classical `Phi_7` oracle, and the callback-free multi-prime CM/CRT entry point
agrees with the final target-field specialization.  That specialization then
feeds the table-free classical normalized-codomain, BMSS, and Frobenius
consumer and returns the same positive trace residue as the full `Phi_7` path.
The same complete path runs over the 416-bit `p125` field: 37 witnessed CRT
primes reconstruct both channels coefficient for coefficient, and two Elkies
roots yield the independently checked exact residue 5.
An independent no-root fixture at `ell=7` reconstructs the specialization for
`j=4` over `F_193`; its square-free equal-degree factors produce exactly the
same Atkin projective order and admissible trace residues as the full-table
classifier.  A specialization tied to another source curve is rejected.

The target-level full-table adapters exist only as independent fixtures; they
are not used by the native producers and cannot supply a production claim.

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

- the general/Weber order discovery remains a bounded practical search, while
  the classical path directly selects the cited three-power family;
- the fixed-`v` practical prime search is not the randomized selector used by
  the asymptotic theorem;
- the classical HCP is generated internally modulo each auxiliary prime, but
  the Weber class polynomial is still supplied;
- signed Weber rows are produced with supplied small relations, but selecting
  and authenticating a sufficient class-group generator set is not wired; and
- the high-level API has a proved classical bound and rejects untyped/empirical
  bounds, but a proved normalization-specific Weber bound is not yet
  implemented.

These missing operations are the ones that must satisfy Sutherland's
polynomial-in-`log q` bounds.  Until they do, production still depends on the
finite authenticated table catalog, so the `p^(1/8+o(1))` search exponent is a
design target supported by the smoothness heuristic, not a completed
implementation theorem.

## Next implementation gate

Generalize the authenticated producer beyond its first classical family:

1. compute/authenticate the Weber surface class polynomial and a sufficient
   target-independent small-relation generator set for each selected order;
2. derive a normalization-specific proved Weber height and pass the signed
   residues through its separately typed CRT wrapper;
3. replace or augment fixed-`v` prime selection with the theorem-aligned
   randomized selector for the unbounded complexity claim; and
4. compare every per-prime residue and final target specialization with the
   authenticated table oracle before permitting any no-root result.

The classical family now has a callback-free, proved-bound connection.
Enabling the faster Weber path still requires a distinct proved Weber evidence
kind; exact-table evidence remains a bounded differential oracle.

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
orders.  The checked CM-surface layer consumes the internally derived
classical HCP, selects the correct trace twist, classifies every edge, and
interpolates both classical `j` channels.  The Weber layer consumes a supplied Weber class
polynomial, uses target-independent small relations to obtain the two global
floor orientations, and uniquely selects the normalized sign.  It does not
generate or authenticate the Weber class polynomial or relation set.

## Focused review checklist

A mathematical review can remain narrow:

- Definition 1 predicates and class-number enumeration;
- necessary Weber order congruences and the separate auxiliary-value
  class-membership obligation;
- the fixed-`Phi_3` ring-class resultant identities and exact factor removal;
- the ramified `1+(D/ell)=1` surface case;
- choice and parity of `t` and `v` in the prime equation;
- target-field power/lift ordering;
- completeness of the two signed torsors and the `X^ell Y^ell=-1` ambiguity
  rule;
- strict CRT coverage and centered rounding;
- the exact rational derivation of the classical Algorithm 1 bound; and
- whether the two output channels match the normalization required by
  [`docs/direct_specialization_boundary.md`](direct_specialization_boundary.md).

Primary references:

- R. Bröker, K. Lauter, and A. Sutherland, [*Modular polynomials via
  isogeny volcanoes*](https://arxiv.org/abs/1001.0402).
- A. Sutherland, [*On the evaluation of modular
  polynomials*](https://arxiv.org/abs/1202.3985), especially Definition 1,
  Algorithm 1, Theorem 1, and Section 3.9.
