# Packed polynomial convolution

The SEA quotient-ring hot path now uses exact Kronecker substitution for
balanced polynomial products and squares with at least 48 coefficients.  The
existing schoolbook/Karatsuba implementation remains the fallback below the
measured crossover and for unbalanced products.

For normalized coefficients `0 <= a_i,b_i < p`, choose a radix `R=2^w` with

```text
w >= 2*bitlength(p) + ceil(log2(min(m,n))).
```

Every coefficient of the integer convolution of an `m`-term and `n`-term
polynomial is then strictly smaller than `R`.  Packing coefficients as radix
`R` digits, multiplying the two packed GMP integers, and unpacking the digits
therefore returns the exact integer convolution with no carry crossing a
coefficient boundary.  The unchanged quotient reducer subsequently maps the
result into `F_p[x]/(M)`.

The implementation rounds `w` up to a whole GMP-limb count.  It uses
`mpz_limbs_write`, `mpz_limbs_read`, and `mpz_limbs_finish` only for transient
in-memory values; no native-limb representation is persisted or exchanged
between hosts.  Checked size arithmetic rejects representations that cannot
fit the GMP size type.  Public `Poly` construction and the internal normalized
result tag are the proof boundary that guarantees nonnegative coefficients.

The same patch removes redundant coefficient normalization after operations
that already produce canonical field elements, and avoids multiplication by
one when reducing by monic polynomials.  The public representation and all
failure behavior are unchanged.

## Correctness gates

The independent schoolbook differential covers products and squares on both a
small field and the 416-bit `p125` field across the 48-coefficient dispatch
boundary and through degree 401.  It includes monic/nonmonic and repeated
moduli, negative inputs normalized by public construction, high-degree operand
pre-reduction, aliases, zero, and constant moduli.  The optimized core and
polynomial-square suites also pass under AddressSanitizer and
UndefinedBehaviorSanitizer.

A full deterministic X1(27) Weber-SEA replay processed 55 levels through the
trace-cap-16 gate.  Two baseline and two candidate runs all emitted the exact
same trusted projection, SHA-256
`8055a435d1abd535574867a55169168635ac683c2ed9e065df135d7440f4b8e6`.

## Measured p125 effect

The audit baseline was compiled from the same source with
`ONESHOTSEA_KRONECKER_COEFFICIENT_THRESHOLD=0`; the candidate used the
production default 48.  Reverse-order B/S/S/B runs on the Apple M4 host gave:

| Workload | Baseline mean | Candidate mean | Speedup |
|---|---:|---:|---:|
| Degree-194 quotient Frobenius | 2.099980 s | 1.795067 s | 1.16986x |
| Degree-401 quotient Frobenius | 7.686691 s | 6.543171 s | 1.17477x |
| Full SEA stage | 54.405483 s | 46.688214 s | 1.16529x |
| Full modular-root subtotal | 24.602316 s | 20.808589 s | 1.18232x |
| Full eigenvalue subtotal | 20.207062 s | 16.609596 s | 1.21659x |

Host load drifted during the full bracket, so the reverse order and paired
means matter; the isolated Frobenius brackets are the cleaner kernel evidence.
The full run nevertheless shows the intended end-to-end SEA effect and exact
projection equality.  This benchmark does not include the 5.4 GB smoothness
scan and is not a certificate-yield claim.

The complete compact identities, raw timing values, validation boundary, and
limitations are in
[`artifacts/local/p125-kronecker-20260802/result.json`](../artifacts/local/p125-kronecker-20260802/result.json).
