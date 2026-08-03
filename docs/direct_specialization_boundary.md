# Direct modular-polynomial specialization boundary

Status: the classical `D=-7*3^(2n)` route is implemented end to end through an
internally derived HCP, native auxiliary-prime residues, a proved height, and
exact CRT.  The signed Weber specialization still uses caller-supplied class
state and target-independent small relations.  Weber input provenance and a
proved Weber height bound remain; production still uses target-level tables.

## Contract

For an odd prime level `ell`, target field `F_q`, and one retained Weber-f
source `f`, `ModularPolynomialSpecialization` contains

```text
level          = ell
source_x       = f in [0,q)
value(Y)       = Phi_ell^f(f,Y)
x_derivative(Y)= (partial Phi_ell^f / partial X)(f,Y)
y_derivative(Y)= d value(Y) / dY
```

The constructor rejects a level below two, mixed fields, a noncanonical
source, a value polynomial that is not monic of exact degree `ell+1`, and an
X-derivative with excessive degree.  Root-specific exceptional conditions are
checked later: the candidate must be a root, both modular derivatives and both
Weber-to-j derivatives must be nonzero, the source must map to the curve's
`j`, and the characteristic must be prime and different from `ell`.

These are structural checks, not by themselves an authenticity proof for the coefficients.
A wrong but well-shaped specialization can still omit a rational root.  A
positive kernel is protected by the independent BMSS map validation and
Frobenius check; a no-root/Atkin conclusion necessarily depends on the direct
producer's CRT reconstruction being correct.  The new explicit-CRT layer
enforces a strict product bound, validates every centered lift, and exposes the
product and prime count.  Authenticity is still incomplete until the
per-prime volcano producer and a proved Weber height bound are connected.  The
constructor alone must never be cited as a certificate of a negative
classification.

This is intentionally a small interface.  Sutherland's Algorithm 1 outputs
`phi(Y)=Phi_ell(j(E),Y)` and optionally `phi_X(Y)` and `phi_XX(Y)`.  The
normalized-isogeny formula used here needs `phi`, `phi_X`, and
`phi_Y=d phi/dY`, but not `phi_XX`.  The same shape applies after replacing
`j` by the BLS Weber invariant and applying the Weber-to-j chain rule.

## Execution path

`compute_weber_elkies_level_specialized_reference` consumes one such object
and performs the already validated remainder of the SEA level:

1. extract every rational root of `value(Y)`;
2. recover each normalized short-Weierstrass codomain from the specialized
   partial derivatives;
3. reconstruct and validate the rational isogeny with BMSS;
4. recover one Frobenius eigenvalue in the kernel quotient ring and, when
   enabled, derive only its validated characteristic-polynomial conjugate; and
5. return the exact trace residue modulo `ell`.

The path neither discovers Weber lifts nor transports roots between 24th-root
orbits.  That is deliberate: the X1(27) curve generator already retains the
exact source Weber coordinate.  A direct evaluator therefore computes exactly
one specialization per level and can hand it to this function without any
full bivariate table.

`reconstruct_weber_specialization_algorithm1` validates the suitable order,
enforces the necessary Weber order congruences, selects witnessed CRT primes,
and feeds streamed per-prime coefficients through exact bounded CRT into this
object.  The congruences alone do not prove that the auxiliary Weber values
enumerated modulo each CRT prime belong to the corresponding class-invariant
set; the callback must enforce that membership while implementing the
remaining volcano seam.  The target source is a separate input and need not
belong to the auxiliary order.  See
[`explicit_crt_producer.md`](explicit_crt_producer.md).

`discover_sutherland_suitable_order` now provides a bounded deterministic
producer for the validated order, with the ring-class number formula checked
against independent reduced-form enumeration.  The classical high-level
wrapper internally derives Sutherland's proved Algorithm 1 bound using exact
integer arithmetic and accepts no external bound.  Exact table evaluation is
retained only as differential evidence.  A proved Weber-specific height
derivation is still required for the faster Weber path.

`derive_three_power_class_polynomial_mod_prime` derives `H_O mod p` internally
from fixed exact `Phi_3` ring-class resultants.  The opaque result is accepted
by `enumerate_cm_interpolation_surfaces`, which validates complete square-free
splitting, admits the unique trace-signed twist at every CM root, and
classifies every table-free Vélu edge.
`specialize_classical_from_cm_surfaces` then applies only the two required
Lagrange linear functionals and returns classical `j` value/X-derivative
residues.  `reconstruct_classical_specialization_from_cm` connects this
callback-free producer to the proved-height CRT wrapper without constructing
the bivariate target-level polynomial.
`elkies_trace_residue_bmss_specialized_reference` consumes that object through
the classical normalized-codomain, BMSS, and Frobenius checks.  The level-7
fixture matches the positive exact residue returned by the full-table path.
The 416-bit `p125` fixture additionally reconstructs both level-7 channels
from 37 witnessed primes and obtains the same exact residue 5 from its two
Elkies roots.

`specialize_weber_from_cm_surfaces` validates a split Weber surface class
polynomial, uses small Weber relations other than the target level to connect
the complete surface and floor torsors, and requires the two floor components
to be global negatives.  It applies the BLS `X^ell Y^ell=-1` relative-sign
test to both orientations and rejects zero or two matches.  At level 5, the
resulting value and X-derivative residues exactly match the target-table oracle.
The class polynomial and small relations are caller-authenticated for now.

`SparseModularPolynomial::specialize_x_with_derivative` is the table-backed
reference producer.  It evaluates `Phi(f,Y)` and `Phi_X(f,Y)` together in one
pass and exists for differential validation, not as evidence that direct
evaluation has been completed.

## Validation performed

The core test suite checks all three specialized evaluations (`Phi`, `Phi_X`,
and `Phi_Y`) against direct bivariate evaluation at multiple points and checks
that malformed specialization objects fail closed.  It then compares the
complete specialized level against the existing table path, including kernel
polynomials, normalized codomains, neighbor invariants, Frobenius eigenvalues,
and exact residues.

The end-to-end comparison covers both:

- the level-37 fixture over `F_1009`, with exact residue `0`; and
- the 416-bit `p125` fixture at level 37, with exact residue `29`, matching the
  retained Magma oracle.

An additional local level-409 A/B on the authenticated `p125` fixture recovered
the same exact residue `19` in all runs.  Eagerly deriving `Phi_X` from the
table reduced median normalized-codomain time from 25,427 microseconds to
1,076 microseconds, but shifted comparable work into table specialization;
whole-level medians were 6,965,229 and 6,972,102 microseconds.  The table-only
rearrangement is therefore not enabled or claimed as a speedup.  A direct CRT
producer can emit `Phi_X` while doing its coefficient reconstruction, which is
the setting where this boundary removes repeated table work.  The run order,
raw timing arrays, build switches, and parsed semantic checks are retained in
[`artifacts/local/p125-direct-specialization-20260802/result.json`](../artifacts/local/p125-direct-specialization-20260802/result.json).

## Asymptotic meaning

The implemented boundary and auxiliary-prime producer are necessary for the
intended asymptotic implementation, but not sufficient.  They remove the API
dependency on a stored bivariate target-level table, implement a complete
classical CM route, signed Weber conversion, and bounded reconstruction.  The
classical route now has both internal class-polynomial production and the
theorem-derived height proof; Weber input authentication and its
normalization-specific height proof remain.

For Algorithm 1, Sutherland proves under GRH an expected running time

```text
O(ell^2 * H * log(H)^2 * loglog(H)),
H = O(ell log ell + log q),
```

and space `O(ell log q + ell^2 log H)`.  When `log q = Theta(ell)`, this is
quasi-cubic in `ell`; used across SEA levels it yields the paper's heuristic
`O(n^4 log^3(n) loglog(n))` point count for `n=log q`.  This polynomial-in-`n`
per-curve cost is what permits the one-shot search's separate smoothness
heuristic to remain `p^(1/8+o(1))` rather than inheriting a table-generation
cost exponential in `n`.

The branch does not yet establish that premise for unbounded inputs.  The
classical algorithms are polynomial-size and the three-power family avoids a
search, but auxiliary primes are still restricted to 64 bits and selected by
the paper's unproved fixed-`v` practical heuristic.  The current
schoolbook/Bareiss implementation also needs production-scale benchmarks and
faster polynomial arithmetic before its constants are understood.

The faster Weber route separately still needs an authenticated Weber class
polynomial, enough authenticated target-independent small relations to connect
both class-group torsors, and a proved normalization-specific coefficient
bound.  Only then can its implemented signed per-prime residues cross the
no-root trust boundary.

Until those steps exist, the honest claim is: a complete callback-free
classical direct evaluator is validated at level 7, the signed Weber auxiliary
producer is validated at level 5, and production still uses the bounded
authenticated archive.

## Why this is reviewable now

The boundary isolates the mathematically delicate handoff into a compact
object and one public function.  A reviewer can verify the derivative
orientation, Weber chain rule, exceptional cases, and downstream proof-object
checks without reviewing a future CRT implementation at the same time.  That
makes review useful now: agreement on this contract prevents the largest
remaining subsystem from being built against the wrong normalization or from
quietly depending on full-table data.

Primary references:

- R. Bröker, K. Lauter, and A. Sutherland, [*Modular polynomials via
  isogeny volcanoes*](https://arxiv.org/abs/1001.0402).
- A. Sutherland, [*On the evaluation of modular
  polynomials*](https://arxiv.org/abs/1202.3985), especially Algorithm 1,
  Theorem 1, and the normalized-isogeny discussion.
