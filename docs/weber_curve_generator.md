# Deterministic Weber-first curve generation

The search generator samples a rational Weber-f value before it chooses an
elliptic curve.  It maps the sample through the project's fixed relation

```text
j = (f^24 - 16)^3 / f^24
```

and rejects only `f=0` and the ramified images `j=0,1728` (equivalently, the
nonzero samples at which the Weber-to-j derivative vanishes).  Consequently
every returned curve starts SEA with a known rational, unramified Weber-f
source lift; arbitrary curves that cannot enter the specialized Weber path are
never scheduled.

For a nonexceptional image, set `k=j/(1728-j)`.  The returned representative is

```text
E: y^2 = x^3 + 3k*x + 2k.
```

It is nonsingular and has invariant `j`.  The generator also returns its twist
by the least positive quadratic nonsquare in the field.  For `j != 0,1728`,
these are the two F_p-isomorphism classes having this invariant, so a single
SEA trace supplies both candidate orders: `p+1-t` and `p+1+t`.  The returned
twist is useful for direct checks and certificate handoff, but a search does
not need to point-count it separately.

Generation is a pure function of `(p, seed, global_index)`.  Each rejected
sample is retried in a separately domain-tagged deterministic stream, and the
result reports the rejection count for replay diagnostics.

## Distribution caveat

This intentionally does not sample j-invariants uniformly.  Reduction of the
deterministic bit stream modulo `p` has a small modulo bias, and the Weber map
restricts output to j-invariants with a rational Weber-f lift, weighted by the
number of such lifts.  Different f values can also map to the same j.  These
properties are the admission optimization, not a claim of cryptographic or
uniform random sampling; search shards remain deterministic and disjoint by
global index even though curve values can collide.
