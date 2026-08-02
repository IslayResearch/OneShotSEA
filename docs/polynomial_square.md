# Specialized polynomial squaring

Production quotient-ring exponentiation spends most of its multiplications on
squares.  Calling the generic polynomial product for `a*a` performed both
ordered cross products independently.  The specialized schoolbook base case
now computes every diagonal once and every distinct cross product once before
doubling it.  Above the existing 32-coefficient boundary it uses the exact
three-square recurrence

```text
(L + x^m H)^2 = L^2 + x^m((L+H)^2-L^2-H^2) + x^(2m)H^2.
```

For odd coefficient counts the high half is one coefficient longer; the same
identity and output bound `2n-1` remain exact.  Recombination is over signed
GMP integers, followed by the unchanged quotient reducer, so nonmonic and
reducible moduli require no extra assumptions.

`make test-poly-square` differentially checks the specialized result against
both generic product paths around every recursive threshold and at production
degrees 64, 129, and 194.  It also covers high-degree pre-reduction, repeated
factors, adversarial coefficients, zero and constant cases.  The core suite
and a separate proof-oriented audit passed.

On a reverse-order p125 index-17 replay, all four runs emitted identical 55
level projections and the same final trace.  Specialized squaring reduced mean
modular-root time from 24.454403 to 22.456082 seconds (1.08899x), mean SEA time
from 47.619140 to 44.479128 seconds (1.07060x), and invocation wall time from
48.285 to 45.195 seconds (1.06837x).  Isolated p125 Frobenius pairs improved
1.09528x, 1.09007x, and 1.07977x at degrees 64, 129, and 194.

The complete raw measurements, binary identities, test boundary, and exact
trace are pinned in
[the compact artifact](../artifacts/local/p125-polynomial-square-20260801/result.json).
