# Direct modular-polynomial specialization boundary

Status: the consumer and explicit-CRT orchestration are implemented and
differentially validated.  The remaining producer is the per-auxiliary-prime
Hilbert-class-polynomial/Weber-volcano callback; production still uses tables.

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

The implemented boundary and explicit-CRT scaffold are necessary for the
intended asymptotic implementation, but not sufficient.  They remove the API
dependency on a stored bivariate table and implement bounded reconstruction;
they do not yet remove the finite table catalog from the per-prime producer.

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

The branch does not yet meet that premise for unbounded inputs.  Steps 1 and 5
below now have checked scaffolding; order discovery, the proof of the
normalization-specific bound, and steps 2--4 remain substantial:

1. discover suitable quadratic orders and compute a proved Weber coefficient
   bound (validation and CRT-prime selection are implemented);
2. compute and authenticate the required Hilbert class polynomial state;
3. enumerate the surface and floor of each `ell`-isogeny volcano modulo every
   CRT prime;
4. specialize the Weber modular function modulo each CRT prime while retaining
   the X derivative; and
5. supply per-prime `value` and `x_derivative` residues to the implemented
   exact CRT and checked specialization interface.

Until those steps exist, the honest claim is: the SEA consumer and CRT
orchestration are ready for a volcano evaluator and are validated beyond 400
bits, while the production specialization source remains a bounded
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
