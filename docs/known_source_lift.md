# Generator-retained Weber source lift

The production curve generators already retain the exact Weber-f moduli point
used to construct each `WeberCurvePair`.  SEA can use that witness directly
instead of rediscovering every rational lift of the same j-invariant.  This is
different from choosing an arbitrary root after receiving only a curve: the
generic public runner still enumerates every lift when no witness is supplied.

The optional runner input fails closed.  Before constructing a singleton
source state it requires the supplied value to be canonical in the field,
nonzero, unramified, outside the exceptional `j=0,1728` fibers, and to map
exactly to the curve's j-invariant.  A foreign or malformed witness throws;
there is no silent fallback.  For the checked-in prime levels, all coprime to
48, the retained nonexceptional generator point is a sufficient Weber source
coordinate.  BMSS reconstruction, isogeny validation, and Frobenius recovery
are unchanged.

Both production families pass their retained `pair.weber_f` to both SEA
passes.  Generic CLI/reference behavior is unchanged.  The semantic change is
bound into every production identity as

```text
weber_source_lift_policy=generator-retained-unramified-singleton-v1
```

and the checkpoint regression rejects an identity created before that policy.

## Correctness audit

The focused tests cover valid singleton equivalence at every emitted level,
noncanonical, zero, foreign-j, j=0, and j=1728 rejection, and repeated
production-pipeline determinism with the compatible-lift count fixed at one.
The pinned multi-orbit fixture is `F_277`, retained `f=20`, `j=73`: its 36
rational lifts occupy three distinct `f^24` orbits.  Exhaustive discovery and
every singleton agree on the empty level 5 and exact level 17, including trace
residue 6 on the generated curve and 11 on its twist.  An independent sweep
found no counterexample through level 43, including a 72-lift three-orbit
fixture over `F_1153`.

The controlled p125 runs also retained the exact per-level projection:
level sequence, exact/empty classification, exact residue, accumulated exact
modulus, exact trace-candidate count, and final trace vector all match between
generic and known-source modes for both fixtures.

## Controlled p125 benchmark

Production was cleanly paused at authenticated cursor 60.  The benchmark used
X1(11) point-four indices 12 and 17, the production group-divisor prior,
`trace-cap=16`, levels through 401, one SEA thread, verified root-orbit reuse,
and conjugate eigenvalue reuse.  It did not load the smooth cache or advance a
search checkpoint.  Both modes used the same patched library and executable;
the sole timing difference was `known_source_lift=nullopt` versus the retained
`pair.weber_f`, avoiding compiler variance.

| Index | Lifts / f24 orbits | Generic SEA | Known-source SEA | Root stage | Result |
|---:|---:|---:|---:|---:|---:|
| 12 | 12 / 1 | 65.383 s | 64.764 s | 41.318 -> 41.302 s | 1.010x, neutral control |
| 17 | 36 / 3 | 129.163 s | 59.823 s | 100.457 -> 33.537 s | 2.159x SEA, 2.995x roots |

Index 12 shows why this is not a blanket 12x improvement: existing verified
orbit transport already reduces all twelve lifts to one specialization.
Index 17 removes two of three specialization orbits, cutting measured SEA by
53.68%; BMSS and eigenvalue subtotals remain stable.  Each mode was timed once,
so the 1% control difference is noise and the result should not be used as a
population mix estimate.  The multi-orbit stage attribution is nevertheless
direct: the root subtotal changes by the expected factor while downstream
proof work and every mathematical output stay fixed.

The restricted macOS timing wrapper emitted a harmless
`sysctl kern.clockrate: Operation not permitted` warning after each successful
run, so the runner's steady-clock `elapsed_us` is the primary timing.  The
compact raw summary, exact residue projections, source, hashes, and clean
pre-change binary identity are in
[the retained artifact](../artifacts/local/p125-known-source-lift-20260801/result.json).
