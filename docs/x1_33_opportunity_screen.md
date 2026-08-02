# Bounded X1(33) opportunity screen

X1(33) is the only family retained for a possible bounded follow-up after the
negative X1(25) gate. This is a formula-and-model screen only: no X1(33)
source is implemented, no p125 curve was generated or counted, and production
remains on X1(27).

The machine-readable record is
[`artifacts/local/p125-x1-33-opportunity-screen-20260801/result.json`](../artifacts/local/p125-x1-33-opportunity-screen-20260801/result.json).

## Explicit formula and map

The primary source is Sutherland and van Hoeij's optimized X1(33) file:

```text
https://math.mit.edu/~drew/X1/X1opt33.txt
af679adfe6ea87d60b0a0ae6f8c4df30208def8ff3abcd7f463f656098a2fcd2
```

Its plane equation has degree 20 in `x` and degree 10 in `y`. A bounded
generator would sample `x`, find the sorted roots of the degree-10 polynomial
in `y`, and evaluate the published map

```text
q = (1+x)/x
t = 1/((1-y)(1+x))
E = [(1+q)t+(2-q), 0, q t^2+(1-q)t, 0, 0]
P = [-t, t^2].
```

At minimum it must reject `x=0`, `x=-1`, `y=1`, every singular or
exceptional-j output, every unavailable Weber/Montgomery lift, and every point
that fails `[33]P=O`, `[11]P!=O`, or `[3]P!=O`.

## Exact divisor and trace-prior obligations

On the required point-four branch, exact orders 33 and 4 combine to an exact
order-132 point because they are coprime. Thus 132 is a conservative cyclic
divisor. Full rational `E[2]`, point four, and the existing audited
Weber/Montgomery identity give the selected side a 2-primary subgroup of order
16 when `p=5 mod 8`. Its intersection with the odd order-33 subgroup is
trivial, proving the selected group-order divisor

`16 * 33 = 528`.

The canonical side must be established by the exact same-j scaling identities
before assigning a sign. For p125 the canonical trace is 446 or 82 modulo
528, according to whether the published curve is the canonical curve or
twist; the paired order is 364 modulo 528 and is forced only by divisor 4.
Because 11 divides the exact prior, SEA level 11 becomes redundant. A future
prototype must independently reproduce that omitted residue with small-field
brute-force counts or Schoof.

The schedule must bind the family and formula digest, deterministic sample and
root-order domains, point-four choice, exact-order checks, side rule, and
528-divisor policy. Initial work must expose a hard `max_x_samples`; an
unbounded search family is out of scope until the bounded gates pass.

## Quantitative boundary

The checked Dickman--Mertens planning model gives:

| basis | X1(27) | X1(33) | X1(33)/X1(27) opportunity |
|---|---:|---:|---:|
| conservative cyclic divisor | 108 | 132 | 1.015421x |
| group-order sensitivity | 432 | 528 | 1.016401x |

Using the clean X1(27) reference mean of 198.19 seconds per ten curves,
X1(33) breaks even on the conservative model only at

`198.19 * 1.015421 = 201.246 seconds`.

That is a narrow 3.056-second allowance. Reusing the prior 5% promotion margin
requires at most **191.663 seconds**, so X1(33) would need to be about 3.29%
faster than X1(27), not merely close.

The credible SEA mechanism is concrete: modulus 528 lowers the initial
Hasse-lattice density by 18.18% relative to 432 and skips `ell=11`. The risk is
also concrete: root degree grows from 6 to 10 and coefficient degree from 8 to
20. Admission density is unknown. Existing X1(11) prior evidence confirms
that an exact prior can eliminate level 11, but its 1.08222x historical wall
result changed binaries and introduced a prior from scratch, so it is not a
prediction for X1(33). A retained AWS p125 `ell=11` record spent only 0.209100
seconds in single-thread modular roots, preventing the skipped level alone
from being treated as a large guaranteed win.

## Bounded future gate

If this route is revisited:

1. Implement only a diagnostic probe in an isolated clone, with a hard sample
   bound and exhaustive small-field formula, denominator, exact-order,
   canonical-side, and sharp 132/264/528 divisor tests.
2. When cores are free, run a generator-only reverse-order p125 gate. Close the
   route if generation work exceeds 2.0x X1(27) or admission has a long tail.
3. Only after that passes, wire a schedule-bound experimental family and run
   same-binary reverse-order SEA windows. Require zero heuristic outcomes,
   deterministic normalized projections and an independent `ell=11` check.
4. Close unless two independent windows beat 1.0x conservative modeled
   opportunities per wall time. Promotion retains the 1.05x margin and the
   191.663-second gate.

Until those gates pass, X1(33) is documented research scope, not an active
implementation or production-search change.
