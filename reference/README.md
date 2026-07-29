# Reference Schoof oracle

`schoof.py` is a deliberately small, CAS-free implementation of the original
Schoof trace computation for short Weierstrass curves

```text
E/F_p: y^2 = x^3 + a*x + b.
```

It returns the trace `t = p + 1 - #E(F_p)` modulo a small odd prime `ell`.
Its role is differential testing of the native SEA implementation: it shares
neither a CAS nor production polynomial/curve code with that implementation.
It does **not** count points.

## Method

For odd `ell != p`, the generic nonzero `ell`-torsion point is represented in

```text
R = F_p[x,y] / (psi_ell(x), y^2 - (x^3+a*x+b)).
```

The code constructs `psi_ell` with the standard division-polynomial
recurrences.  If `pi` denotes Frobenius, Schoof's equation is

```text
pi^2(P) + [p]P = [t]pi(P).
```

Every residue `t mod ell` is tested in `R`.  Point operations use Jacobian
coordinates, which avoids assuming that a polynomial denominator is a unit in
the quotient ring.  The unique matching residue is returned.

This is reference code, optimized for clarity rather than speed.  `ell` values
3, 5, 7, and 11 are the intended testing range.

## Run tests

From the repository root:

```sh
python3 -m unittest -v reference/test_schoof.py
```

The differential suite obtains expected traces by brute-force enumeration in
the **test only**, over more than 150 `(curve, ell)` cases.  No optional Python
packages, Magma, PARI/GP, SageMath, or network access are required.
