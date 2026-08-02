# Reusable reciprocal-reduction A/B

A reusable reciprocal reduction context was implemented and differentially
validated as a possible acceleration for repeated quotient-ring Frobenius
arithmetic.  It was exact, but slower on both the isolated large-degree kernel
and a deterministic production Weber level, so the prototype was rejected and
fully reverted.

The exactness gate covered 60 deterministic small cases, p125 quotient degrees
64, 129, and 194, unbalanced products, context exponentiation, and the full core
test.  Residues, kernel counts, polynomial degrees, and checksums matched.

| Workload | Baseline | Reciprocal context | Result |
|---|---:|---:|---:|
| Degree-194 Frobenius pair, five interleaved runs (median) | 1.95062 s | 2.22974 s | 14.31% slower |
| Deterministic p125 index-9 Weber level 277, two interleaved runs (mean) | 3.79822 s | 4.06195 s | 6.94% slower |
| Level-277 eigenvalue stage | 1.21931 s | 1.47784 s | 21.20% slower |

The production-level comparison emitted two kernels, trace residue 221 modulo
277, quotient degree 138, and one independent plus one derived eigenvalue in
both modes.  The likely explanation is that setup and reciprocal-correction
overhead outweigh the saved divisions at the current operand sizes.  No
reciprocal-reduction code remains in the accepted source tree.

The compact evidence and prototype-patch digest are retained in
[the negative-result artifact](../artifacts/local/p125-reciprocal-reduction-20260801/result.json).
