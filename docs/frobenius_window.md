# Exact-cost Frobenius window

The polynomial quotient-ring `powmod` path now uses an unsigned odd-power
sliding window.  For each exponent it constructs plans for widths one through
five and selects the plan with the fewest quotient-ring squarings and
multiplications, including the complete odd-power-table setup.  Ties retain
the narrower plan.  This exact cost selection prevents a table from making a
small or sparse arbitrary exponent slower.

No field or quotient-ring inverse is used.  If
`e = (w_0 0...0 w_1 ... w_k 0...0)_2`, where each `w_i` is an odd binary
window, the implementation initializes the accumulator to `base^w_0`, then
performs the intervening squarings and multiplies by the precomputed odd
powers.  This is ordinary exponentiation in an associative ring, so it remains
sound when the polynomial modulus is reducible or non-square-free.  The old
right-to-left binary loop is retained as an independent test reference.

For the fixed p125 exponents, counting every quotient multiplication used to
construct the table gives:

| Exponent | Binary operations | Window operations | Reduction |
|---|---:|---:|---:|
| `p` | 566 | 478 | 15.55% |
| `(p-1)/2` | 564 | 477 | 15.43% |
| `p^2` | 1182 | 964 | 18.44% |
| `(p^2-1)/2` | 1180 | 963 | 18.39% |

The differential gate covers every exponent from zero through 1024, all four
p125 exponents above, a nonmonic quotient, a nonmonic repeated-factor
quotient, exponent zero, a constant modulus, a zero modulus, and a negative
exponent.  These tests preserve the former edge semantics exactly.

## Controlled p125 A/B

Production was paused at its authenticated cursor 146.  Two reverse-order
pairs used the same compiler, p125 X1(11) index 17, retained source singleton,
selected-side trace prior, cap 16, levels through 401, and one SEA thread.  The
baseline and windowed libraries differ only in `Poly::powmod` and its tests.

| Runner timing | Baseline | Windowed | Speedup |
|---|---:|---:|---:|
| SEA elapsed, mean of two | 57.088 s | 49.178 s | 1.16085x |
| Modular roots | 32.007 s | 24.013 s | 1.33293x |
| External wall | 57.905 s | 49.860 s | 1.16135x |

Every run emitted the identical 55-level exact projection and final trace.
BMSS and eigenvalue recovery are outside this polynomial-exponentiation
change; their stable timings provide an internal control.  Full timings,
binary hashes, operation counts, and the equality boundary are retained in
[the benchmark artifact](../artifacts/local/p125-frobenius-window-20260801/result.json).
