# Reusable reciprocal reduction after packed convolution

The production polynomial `powmod` path now prepares one quotient-ring
reduction context per exponentiation.  For a monic modulus of degree at least
96, the context computes the inverse of the reversed modulus modulo `x^d` once
and obtains each quotient with reverse-polynomial division.  The two balanced
coefficient products in that division use the exact limb-aligned Kronecker
convolution already present in the branch.  Smaller or nonmonic moduli retain
the former high-coefficient elimination reducer.

This is a post-Kronecker re-evaluation of an older rejected idea.  The first
prototype, measured before packed convolution was available, was exact but
14.31% slower on the degree-194 kernel and was fully reverted.  Its negative
result remains useful: reciprocal division is not intrinsically faster here;
it becomes profitable only when its quotient products use the packed
convolution and its inverse is amortized across an entire exponentiation.

## Exactness boundary

The public `mulmod` and `squaremod` APIs still use long reduction, so the
binary reference exponentiator is algorithmically independent of the new
context.  Differential tests cover:

- monic p125 quotient degrees 95, 96, 97, 129, 194, and 401, straddling the
  activation threshold and reaching the largest checked-in Weber level;
- the full 416-bit Frobenius exponent at degrees 96 and 194;
- nonmonic, sparse, and repeated-factor moduli; and
- high-degree inputs that must be reduced before multiplication.

The normal core and polynomial-square suites pass.  The same suites also pass
under AddressSanitizer and UndefinedBehaviorSanitizer.  Apple ASan does not
support leak detection, so leak detection was not claimed.  The deterministic
55-level SEA projection is unchanged at SHA-256
`8055a435d1abd535574867a55169168635ac683c2ed9e065df135d7440f4b8e6`.
That projection was independently Magma-validated in the preceding packed-
convolution audit; no fresh Magma executable was discoverable on this host for
this iteration.

## Same-source A/B

The baseline was compiled from the exact same source with
`ONESHOTSEA_RECIPROCAL_REDUCTION_DEGREE_THRESHOLD=0`.  Candidate and baseline
were run in B/S/S/B order on an Apple M4, with one SEA thread and the same
deterministic p125 curve and authenticated table set.

| Workload | Long-reducer mean | Reciprocal mean | Speedup |
|---|---:|---:|---:|
| Degree-194 Frobenius | 2.115 s | 1.192 s | 1.775x |
| Degree-401 Frobenius | 7.949 s | 2.473 s | 3.214x |
| Complete 55-level SEA stage | 56.074 s | 42.229 s | 1.328x |
| SEA modular-root subtotal | 24.463 s | 12.963 s | 1.887x |

All baseline and candidate projections matched exactly.  The BMSS subtotal was
effectively neutral (1.005x), as expected because it does little quotient-ring
Frobenius work.  Eigenvalue recovery improved 1.123x.

Degree 48 regressed in an isolated sample, while degree 64 and degree 80
improved 1.076x and 1.132x.  A direct full-SEA comparison of thresholds 96 and
48 differed by only 0.12%, within run noise, with identical projections.
Production therefore keeps the conservative degree-96 threshold rather than
paying a demonstrated small-degree regression for no measured end-to-end
gain.

The raw timing vectors, binary identities, validation record, threshold
ablation, and limitations are retained in
[the post-Kronecker artifact](../artifacts/local/p125-reciprocal-kronecker-20260802/result.json).
The [older negative artifact](../artifacts/local/p125-reciprocal-reduction-20260801/result.json)
is retained as superseded history rather than rewritten.
