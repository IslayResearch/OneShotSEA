# Deferred product normalization

The quotient-ring `mulmod` and `squaremod` paths previously normalized every
raw convolution coefficient before reduction.  The reducer then normalized
the same high coefficient when it became a descending pivot and normalized
every surviving low coefficient in its final loop.  The first full pass was
therefore redundant and has been removed.

This is exact over signed Karatsuba intermediates.  Karatsuba produces the
integer convolution.  At high coefficient `c_k`, the reducer computes
`q = c_k * lead(M)^-1 (mod p)` and subtracts the lower coefficients of
`q*x^(k-d)M`; the omitted pivot difference `c_k-q*lead(M)` is a multiple of
`p`.  Descending induction preserves the class in `F_p[x]/(M)`, and the final
loop chooses canonical representatives.  The proof covers non-monic moduli,
pre-reduced high-degree operands, aliases, and negative intermediates.
Coefficient growth remains bounded by roughly `2d(p-1)^2`, so the change does
not introduce an unbounded-memory path.

The expanded differential tests include sizes around the Karatsuba threshold
through degree 194 over p125, a product below the modulus degree, non-monic
dense near-p coefficients, high-degree operand pre-reduction, a strongly
unbalanced shape, source aliases, and zero/constant moduli.  All compare with
independent schoolbook multiplication followed by generic division.

## Controlled p125 A/B

Production was paused at authenticated cursor 90.  Two interleaved runs used
the same compiler, harness, p125 X1(11) index 17, selected-side prior,
known-source singleton, trace cap 16, levels through 401, and one SEA thread.
The frozen baseline and patched libraries differ only in the deleted
normalization loops.

| Runner timing | Baseline | Patched | Speedup |
|---|---:|---:|---:|
| SEA elapsed, mean of two | 59.942 s | 57.927 s | 1.03479x |
| Modular roots | 33.635 s | 32.391 s | 1.03840x |
| Eigenvalue recovery | 18.652 s | 17.810 s | 1.04723x |

Both pairs emitted the same 55-level exact projection and final trace.
The 3.48% SEA improvement is small but repeatable in both interleaved pairs,
and it targets every remaining quotient exponentiation rather than only
multi-orbit curves.  Compact timings, binary hashes, and the equality boundary
are retained in
[the benchmark artifact](../artifacts/local/p125-deferred-product-normalization-20260801/result.json).
