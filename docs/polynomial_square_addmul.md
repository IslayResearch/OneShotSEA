# In-place cross-term accumulation for polynomial squares

The specialized schoolbook square previously computed every distinct cross
product into a temporary GMP integer, doubled the temporary, and added it to
the destination coefficient.  The optimized form uses `mpz_addmul` to
accumulate every undoubled cross product directly, doubles each destination
coefficient once, and then adds the diagonal squares:

```text
(sum_i a_i x^i)^2
  = 2 sum_(i<j) a_i a_j x^(i+j) + sum_i a_i^2 x^(2i).
```

The recursive three-square Karatsuba identity, coefficient threshold, quotient
reduction, and public API are unchanged.  This is an integer-convolution
reassociation before the existing exact reduction, so it adds no assumptions
about the field modulus or polynomial modulus.

The existing square differential remains the independent oracle.  It compares
`squaremod(a,m)` with both `mulmod(a,a,m)` and `mod(mul(a,a),m)` across sizes
1--195, recursive boundaries, p125 production degrees, monic and nonmonic
moduli, repeated factors, high-degree pre-reduction, adversarial coefficients,
zero, alias, and constant cases.  The complete suite, including Magma-backed
native oracle checks, passed.

## Same-input p125 measurement

An isolated clone at base `976924b3aa2148f62ceae11948824d8aed5a41bb`
ran one fixed p125 curve through Weber level 193.  Baseline and optimized
binaries differed only by the cross-term hunk and were run in reverse order
`B/S/S/B` with trace cap 64, ten requested SEA threads, root-orbit reuse, and
conjugate-eigenvalue reuse.

| Stage | Baseline mean | Optimized mean | Speedup |
|---|---:|---:|---:|
| External wall | 15.275 s | 14.165 s | 1.07836x |
| Modular roots | 9.102554 s | 8.377579 s | 1.08654x |
| Eigenvalue | 3.511944 s | 3.222771 s | 1.08973x |
| BMSS | 2.196491 s | 2.131863 s | 1.03032x |
| Normalized codomain | 0.272474 s | 0.264686 s | 1.02942x |

All four runs produced 42 level records, including 21 exact levels.  Removing
only the timing object from level records yielded the same canonical SHA-256
`5ff8d9c29b2374e37d802af9afadbbbf6a71d455aca172e396ac06acfb0b063b`
for all runs; summary constraints and candidate counts matched as well.

This is a bounded SEA benchmark on one p125 curve.  It does not include
X1(27) generation, smoothness screening, certificate assembly, or enough
production curves to claim a 7.8% end-to-end search-throughput improvement.
It establishes an exact, same-input SEA gain above the 3% promotion threshold.
Commands, source and binary identities, raw log paths and hashes, validation,
and further limitations are pinned in
[the benchmark artifact](../artifacts/local/p125-polynomial-square-addmul-20260801/result.json).
