# Direct-first SEA review guide

This guide defines the useful expert-review boundary for the current p125
branch. It is not a request to certify a claimed p125 proof: no p125
certificate has been found. The review question is whether the implemented
direct-SEA evidence and its composition are sound enough to justify a larger
p125/p130 yield experiment.

## Why review now

The implementation has crossed the threshold where review can test real
mathematics rather than a proposed architecture:

- the custom specialized modular-polynomial producer reaches the production
  search pipeline;
- independently prepared levels complete exact 416-bit traces;
- Elkies residues and set-valued Atkin constraints survive direct, deferred,
  Weber, cap-one, and early-abort transitions;
- independent Magma and PARI/GP point counts are retained only as validation
  oracles, outside the production path;
- prepared contexts have a deterministic authenticated boundary; and
- arithmetic optimizations replay the same proof state and exact traces.

A positive review would justify spending compute on certificate yield and
p130 scaling. It would not establish the smoothness heuristic, the finite
SEA/CM crossover, or an unbounded asymptotic implementation.

## Questions for Drew

1. Do auxiliary-prime selection, ring-class resultants, CM-surface checks,
   interpolation bounds, and centered CRT reconstruction authenticate each
   specialized classical modular polynomial?
2. Do Vélu enumeration, codomain normalization, and BMSS checks justify every
   exact Elkies residue?
3. Do complete-root evidence, Frobenius composition, factor degrees,
   projective orders, and residue enumeration justify every Atkin trace set?
4. Does the specialized `X`-power planner admit only canonical sub-degree
   monomials and remain exactly equivalent to generic exponentiation?
5. Are fixed-inner composition plans lifetime-safe, correctly bounded, and
   exactly equivalent to generic quotient-ring composition?
6. Does the hybrid quotient reducer recover the exact highest quotient block,
   clear only coefficients that vanish in the field, and finish with a
   remainder equivalent to full long division?
7. Are exact and set-valued constraints composed soundly through cap-N
   screening, the deferred suffix, Weber continuation, cap-one recovery, and
   smoothness rejection?
8. Is loading an authenticated prepared context mathematically and
   operationally equivalent to constructing it fresh with the same inputs?

## Source map

- [`src/direct_modpoly.cpp`](../src/direct_modpoly.cpp),
  [`src/class_polynomial.cpp`](../src/class_polynomial.cpp),
  [`src/prime_isogeny.cpp`](../src/prime_isogeny.cpp), and
  [`src/cm_surface.cpp`](../src/cm_surface.cpp): specialized modular-polynomial
  production and authentication.
- [`src/poly.cpp`](../src/poly.cpp), [`src/factor.cpp`](../src/factor.cpp),
  [`src/elkies.cpp`](../src/elkies.cpp), and
  [`src/atkin.cpp`](../src/atkin.cpp): quotient-ring arithmetic and certified
  Elkies/Atkin recovery.
- [`src/sea.cpp`](../src/sea.cpp), [`src/trace.cpp`](../src/trace.cpp), and
  [`src/early_abort.cpp`](../src/early_abort.cpp): trace-state composition and
  sound early rejection.
- [`src/direct_context_cache.cpp`](../src/direct_context_cache.cpp) and
  [`src/search_pipeline.cpp`](../src/search_pipeline.cpp): authenticated
  context loading, continuation policy, telemetry, and checkpoint identity.
- [`scripts/aws/`](../scripts/aws/): immutable preparation and worker launch.

## Evidence map

- [Complete direct p125 trace](../artifacts/local/p125-direct-trace-777e293-20260803/README.md)
- [Four-curve direct-first cohort](../artifacts/local/p125-direct-first-cohort-20260803/README.md)
- [Certified Atkin singleton](../artifacts/local/p125-certified-atkin-singleton-20260803/README.md)
- [Deferred cap-one suffix](../artifacts/local/p125-cap-one-direct-tail-20260803/README.md)
- [Pre-smooth suffix promotion](../artifacts/local/p125-pre-smooth-direct-tail-20260803/README.md)
- [Bounded Atkin combination](../artifacts/local/p125-direct-atkin-mitm-20260803/README.md)
- [Frobenius evidence reuse](../artifacts/local/p125-direct-atkin-frobenius-reuse-20260803/README.md)
- [Prepared Frobenius composition](../artifacts/local/p125-direct-frobenius-composition-20260803/README.md)
- [Specialized Frobenius `X` window](../artifacts/local/p125-direct-frobenius-x-window-20260803/README.md)
- [Hybrid quotient reduction](../artifacts/local/p125-direct-hybrid-reduction-20260803/README.md)

The specialized-modular-polynomial design follows Bröker, Lauter, and
Sutherland's [modular-polynomial construction](https://arxiv.org/abs/1001.0402)
and Sutherland's [direct-evaluation analysis](https://arxiv.org/abs/1202.3985).
The precise asymptotic claim boundary is documented in
[`asymptotic_scope.md`](asymptotic_scope.md).
