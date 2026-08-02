# Polynomial-subring Element exponentiation

Frobenius eigenvalue recovery represents the generic curve point in
`F_p[x]/(h)[y]/(y^2-f)`.  Its two production exponentiation bases are `x` and
`f`, both of which have zero `y` coefficient.  The polynomial subring is
closed under multiplication, so these powers can delegate to the exact-cost
windowed `Poly::powmod` path and then be wrapped back as an Element with zero
`y` coefficient.  The general nonzero-`y` Element path retains the former
binary loop.

This does not require irreducibility, square-freeness, or an inverse in the
quotient.  It is simply the injective subring operation induced by the same
polynomial modulus; rewrapping through the ordinary Element constructor also
preserves the existing constant-modulus zero-ring behavior.  The binary
Element loop remains as an independent differential reference.

For one independent p125 eigenvalue recovery, the `x^p` and
`f^((p-1)/2)` powers fall from 1,130 quotient squarings/multiplications to 955,
a 15.49% reduction including odd-power-table construction.  The delegation
also avoids the zero products, additions, and zero-Element wrappers formerly
created around those operations.

The differential gate covers exponents zero through 256, all four fixed p125
Frobenius exponents, 32 deterministic target-sized exponents, a sparse large
exponent with long internal/trailing zero runs, a nonmonic reducible repeated-
factor quotient, zero- and nonzero-`y` inputs, a constant modulus, and negative
rejection.

## Controlled p125 A/B

Production was paused at authenticated cursor 177.  The isolated reverse-order
pair uses retained-source p125 X1(11) index 17 at exact Weber level 269:

| Isolated level 269 | Baseline | Delegated | Speedup |
|---|---:|---:|---:|
| Eigenvalue recovery, mean of two | 1.886 s | 1.653 s | 1.14084x |

The two-kernel eigenvalue/trace/degree projection was identical.  A second
reverse-order pair replayed the full 55-level SEA path:

| Full retained-source SEA | Baseline | Delegated | Speedup |
|---|---:|---:|---:|
| SEA elapsed | 49.322 s | 46.754 s | 1.05493x |
| Eigenvalue recovery | 17.681 s | 15.069 s | 1.17333x |
| External wall | 49.995 s | 47.435 s | 1.05397x |

Modular roots and BMSS were stable internal controls.  Every run emitted the
same 55-level exact projection and final trace.  Commands, hashes, individual
timings, operation counts, and equality boundaries are retained in
[the benchmark artifact](../artifacts/local/p125-element-subring-window-20260801/result.json).
