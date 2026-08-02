# Deterministic X1(27) curve family

The optional `--curve-family x1-27` generator implements Andrew Sutherland's
optimized X1(27) model directly over the target field. The primary equation
and birational map are pinned by URL and SHA-256:

```text
https://math.mit.edu/~drew/X1/X1opt27new.txt
b63a2527b1778acce2fa7d003655d929c1687eec9902b03982e729e11a571250
```

The source text is not vendored. The implementation evaluates the published
mathematical formulas using project-local field arithmetic, rejects every zero
map denominator, constructs the corresponding Tate normal form, and directly
checks that the distinguished point has exact order 27. Singular and
exceptional-j curves are rejected before Weber/Montgomery conversion.

Accepted curves have the order-27 point and full rational E[2], giving group
divisor 108. Requiring a rational order-four point promotes this to 216. For
the production target branch `p = 5 (mod 8)`, the retained Weber/Montgomery
identity supplies a 2-primary subgroup of order 16 and promotes the
selected-side group divisor to 432. The conservative cyclic divisor remains
108. These are trace-prior facts; certificate assembly still verifies the
exact order of every proposed point independently.

The deterministic stream binds the seed, global index, attempt number, sorted
roots, generator version, formula digest, point-four policy, and the distinct
X1(27) trace-prior policy into the search schedule. X1(11) and X1(27)
checkpoints are therefore incompatible by construction.

`make test-x1-27-probe` covers the formula and map, denominator failures,
determinism, exact-order checks, selected curve/twist congruences, and sharp
small-field divisor boundaries at 108, 216, and 432. A separate adversarial
audit reconstructed the formulas from the pinned source and found no
soundness defect. On p125 the isolated generator averaged 1.476 seconds over
three indices versus 0.917 seconds for X1(11); end-to-end family promotion is
therefore gated on a same-binary SEA/smoothness A/B rather than the divisor
model alone.
