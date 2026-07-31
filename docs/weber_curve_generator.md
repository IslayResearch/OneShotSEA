# Deterministic Weber-first curve generation

The search generator samples a rational Weber-f value before it chooses an
elliptic curve.  It maps the sample through the project's fixed relation

```text
j = (f^24 - 16)^3 / f^24
```

and rejects `f=0` and the ramified images `j=0,1728` (equivalently, the
nonzero samples at which the Weber-to-j derivative vanishes).  The production
deterministic sampler also rejects images for which the complete j-to-
Montgomery solver finds no coefficient over the base field: no certificate
for either counted order can be assembled from such an image.  Consequently
every returned curve starts SEA with both a known rational, unramified Weber-f
source lift and a canonical-verifier-compatible Montgomery handoff.

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
sample, including a certificate-incompatible Montgomery image, is retried in a
separately domain-tagged deterministic stream, and the result reports the
rejection count for replay diagnostics.  Direct `weber_curve_pair_from_f`
construction remains unfiltered for reference tests.

## Distribution caveat

This intentionally does not sample j-invariants uniformly.  Reduction of the
deterministic bit stream modulo `p` has a small modulo bias, and the Weber map
restricts output to j-invariants with a rational Weber-f lift, weighted by the
number of such lifts.  Different f values can also map to the same j.  These
properties are the admission optimization, not a claim of cryptographic or
uniform random sampling; search shards remain deterministic and disjoint by
global index even though curve values can collide.

For an exhaustive small-field census with `p=10093`, which has the same
residue `13 mod 48` as `p125`, 5,136 of 10,092 nonzero Weber-f samples admitted
a base-field Montgomery coefficient and 4,956 did not.  The admitted half had
full rational 2-torsion, so both the curve and twist orders were divisible by
four.  Filtering therefore removes about half of otherwise guaranteed
certificate-assembly failures before any SEA work and supplies an additional
known factor of two to both smooth parts.  This is a search-distribution
optimization only; native exact-order checks and the unmodified canonical
verifier remain authoritative.

The prefilter also diagnoses the recorded production
`seed=202607300000,index=0` sample: replaying its original first Weber-f value
gives

```text
j = 68849206327601965313888583746088196828445328144495959503257426787566910433543213612952311000300151519318346208210352873994837
```

and the complete base-field Montgomery admission predicate is false.  Its
documented 753.4-second SEA point count therefore could not have produced a
canonical certificate even if its smooth part had passed the order-level
gate.  The filtered schedule deterministically retries this sample before SEA.
