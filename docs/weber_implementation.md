# Weber-f specialized modular-polynomial path

This is the executable reference path for the specialized modular function
proposed in the task. It follows Section 7.3 of
[Broeker-Lauter-Sutherland](https://arxiv.org/abs/1001.0402) and Sections 3.8
and 3.9 of [Sutherland](https://arxiv.org/abs/1202.3985).
The current checked-in levels are an end-to-end reference, not yet a
large-level production CRT evaluator.

## Exact normalization

Let `q = exp(2*pi*i*z)`, `r = q^(1/48)`, and

```text
f(z) = zeta_48^-1 eta((z+1)/2) / eta(z)
     = r^-1 product_{m >= 1} (1 + r^(24(2m-1))).
```

The phase `zeta_48^-1` is essential. With this convention

```text
gamma_2 = (f^24 - 16) / f^8,
j       = (f^24 - 16)^3 / f^24,
Psi^f(F,J) = (F^24 - 16)^3 - F^24 J.
```

For a prime `ell` not dividing 48, `Phi_ell^f(X,Y)` is the symmetric, monic
degree-`ell+1` polynomial satisfying

```text
Phi_ell^f(f(z), f(ell*z)) = 0.
```

Only coefficients `X^a Y^b` with `ell*a+b = ell+1 (mod 24)` can be nonzero.
For admissible prime levels, `ell^2 = 1 (mod 24)`, so this condition is
symmetric in `a,b`. The coefficient of `X^ell Y^ell` is `-1`, fixing the sign
consistency discussed by BLS.

`tools/generate_weber_modpoly.py` expands the product in `Z((r))`, applies the
sparsity rule, and solves the remaining coefficients in descending pole order.
The Laurent system has unit diagonal, so all arithmetic is exact integer
arithmetic. It then checks the identity beyond every coefficient used by the
solve. The byte-reproducible fixtures are

```text
Phi_5^f = X^6 + Y^6 - X^5 Y^5 + 4 X Y,
Phi_7^f = X^8 + Y^8 - X^7 Y^7 + 7 X^4 Y^4 - 8 X Y.
```

The script also directly specializes a generated polynomial modulo a prime:

```bash
python3 tools/generate_weber_modpoly.py --level 5
python3 tools/generate_weber_modpoly.py --level 5 --specialize 2 --modulus 101
```

The naive q-product solver is deliberately capped at level 43. Production
levels should use the isogeny-volcano/explicit-CRT algorithms from the papers,
retaining the same normalization, sparsity, and `X^ell Y^ell=-1` sign test.
The generated level-5 and level-7 tables, hashes, and provenance are checked in
under `data/modpoly/weber_f/`.

## Finite-field admission and sign cases

For an ordinary curve with endomorphism discriminant `D`, the simple Weber
class-invariant case requires `D = 1 (mod 8)` and `3` not dividing `D`. If the
field prime is also `p = 11 (mod 12)`, `Psi^f(F,j(E))` has exactly two roots,
`x` and `-x`; either initial sign works because
`Phi_ell^f(X,Y)=Phi_ell^f(-X,-Y)`, but signs must remain consistent while
walking a component.

Both primary targets are `1 (mod 12)`, not `11 (mod 12)`. Therefore the
two-root lemma does not apply to the production search. The safe 1202.3985
fallback is to enumerate every root of `Psi^f(F,j(E))`, instantiate for each,
and validate the resulting isogeny using its dual. Silently choosing one root
would make this path incorrect.

`weber_f_lifts` and `elkies_kernels_weber_bmss_reference` implement this
exhaustive policy. Incompatible lifts are rejected by normalized BMSS
reconstruction; duplicate sign lifts are coalesced only after their kernels,
neighbors, eigenvalues, and residues agree.

## Kernel and codomain handoff

For each accepted Weber lift `x` of `j(E)`:

1. Compute `phi(Y)=Phi_ell^f(x,Y)` and its `X` derivative; find each rational
   root `xtilde` and evaluate `Phi_X^f(x,xtilde)` and `Phi_Y^f(x,xtilde)`.
2. Map `xtilde` back with `F(G)=(G^24-16)^3/G^24` to obtain `jtilde`.
3. For `E: y^2=x^3+A*x+B`, form `jprime=(18B/A)j` in the nonexceptional
   short-Weierstrass case. With `F'` the derivative of the Weber-to-j map, use

   ```text
   jtprime = -Phi_X^f(x,xtilde) F'(xtilde) jprime
             / (ell Phi_Y^f(x,xtilde) F'(x)).
   ```

4. Set `mtilde=jtprime/jtilde`, `ktilde=jtprime/(1728-jtilde)` and construct
   the normalized codomain

   ```text
   y^2 = x^3 + ell^4*mtilde*ktilde/48*x
               + ell^6*mtilde^2*ktilde/864.
   ```

5. Hand `E` and this codomain to fastElkies' to recover the kernel polynomial.
   Validate by constructing the dual with normalization factor `ell` and check
   that the composition acts as multiplication by `ell` on random points.

Exceptional zeros of `A`, `j`, `j-1728`, `F'`, or either modular derivative
must fall back to the classical-j/Schoof path; division through them is not
valid.

## Remaining production work

The exhaustive root/codomain experiment is complete at levels 5 and 7. Weber
and classical-j paths recover identical kernels and trace residues on the
admitted fixtures, and the Weber path produces an independently Schoof-checked
level-7 residue over the 416-bit `p125` field. A curve with no compatible Weber
class-invariant lift safely returns no result so the scheduler can fall back.

The remaining blockers are the large-level instantiated evaluator and the fast
arithmetic behind it: generate or explicitly evaluate the levels selected by
the SEA scheduler, replace quadratic reference power-series arithmetic with
Newton doubling and fast multiplication, and use dual-composition validation
when full division polynomials are no longer a practical independent check.
