# Odd-degree norm sign recovery: rejected wall gate

For an exact Elkies kernel `h` at a level `ell = 3 mod 4`, the quotient
algebra `A = F_p[x]/h` has odd degree `d = (ell-1)/2`. The ordinary
x-coordinate eigenvalue scan determines an absolute scalar `m` such that
Frobenius sends the generic kernel point to `[+m]P` or `[-m]P`. If `R_m` is
the y-coordinate multiplier of `[m]P`, then

```text
f^((p-1)/2) / R_m = +1 or -1 in A.
```

Because `d` is odd, the algebra norm distinguishes the signs:
`Norm_A(-1) = -1`. The prototype computes these norms with an exact
Euclidean-resultant recurrence and a base-field exponentiation, avoiding the
second 416-bit quotient-ring Frobenius power at the applicable levels. Both
odd and even scalar-multiple Jacobian parity cases are handled explicitly.
Unexpected zero divisors fall back to the retained full-y path.

The independent oracle was not removed. Forced full-y linear and
meet-in-the-middle recovery remain available, and the focused differential
test compares them with forced norm recovery on division-polynomial fixtures
at levels 3 and 7 and a fully validated BMSS isogeny at level 11. Both signs
are covered.

## Strict p125 gate

The fixed p125 level-193 curve used for the square-addmul benchmark was run in
reverse order `B/S/S/B`. Every run emitted the same 42 non-timing level
records, 21 exact levels, summary constraints, and candidate counts. Ten
exact levels used the odd-degree path.

| Stage | Baseline mean | Prototype mean | Speedup |
|---|---:|---:|---:|
| External wall | 14.325 s | 13.655 s | 1.04907x |
| All eigen recovery | 3.289115 s | 2.630139 s | 1.25055x |
| Odd-degree eigen recovery | 1.970345 s | 1.290797 s | 1.52646x |
| Modular roots | 8.406063 s | 8.409734 s | 0.99956x |
| BMSS | 2.150314 s | 2.167306 s | 0.99216x |

The targeted kernel improved substantially, but the predeclared promotion
gate required wall speedup strictly greater than `1.05x`. The observed
`1.049066x` does not pass. It is not rounded up, the full Magma promotion
suite was not run, and the prototype is not recommended for production.

Exact commands, source and binary identities, raw hashes, projection digest,
and limitations are pinned in
[the negative artifact](../artifacts/local/p125-odd-degree-norm-sign-20260801/result.json).

## Gauss-lemma follow-up: route closed

The adjacent follow-up removed the dense resultants for the generic point's
Jacobian coordinates. Multiplication by the recovered absolute scalar `m`
permutes the kernel's x-root pairs `{+/-kP}`; Gauss's lemma makes the product
of the required orientation signs exactly `(m/ell)`. The candidate therefore
retained only the cheap `Norm_A(f) = Res(h,f)` computation.

The focused 18-kernel differential test passed against both independent
full-y recovery algorithms. A fresh strict p125 `B/S/S/B` gate also preserved
all 43 non-timing records in every run (42 levels, 21 exact, projection SHA-256
`5ff8d9c29b2374e37d802af9afadbbbf6a71d455aca172e396ac06acfb0b063b`).
The targeted odd-degree eigenvalue subtotal improved `1.56006x`, but external
wall improved only from `14.140` to `13.645` seconds, or `1.036277x`.

That does not strictly exceed the predefined `1.05x` promotion gate. The full
native/Magma suite was consequently not run, the candidate source is not for
integration, and the odd-degree norm-sign optimization route is closed. Exact
identities, stage totals, and raw hashes are in
[the Gauss-lemma addendum](../artifacts/local/p125-odd-degree-gauss-norm-sign-20260801/result.json).
