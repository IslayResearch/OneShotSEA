# p125 direct-SEA factored Atkin comparison

This bundle retains the fixed 16-curve cohort that exposed the direct SEA
Atkin-state bottleneck and the replay after replacing full CRT Cartesian
products with exact factored meet-in-the-middle counting.

The mathematical projection of all 240 level records is identical.  The 64
independent Schoof controls through `ell=13` all agree and no level is
unconstrained.  Aggregate direct-level evaluation changes from 263.265228 s
and 1,253,654,528 bytes peak RSS to 33.784948 s and 40,943,616 bytes: 7.79x
faster and 30.62x lower peak memory on this cohort.

`baseline.ndjson` is the retained pre-MITM run. `factored-mitm.ndjson` is the
post-MITM replay. Both use the same p125 target, X1(27) seed/range, authenticated
direct-context cache payload, level list, and independent-control policy.
Timing noise outside direct evaluation is visible in curve generation and
Schoof, so the primary comparison is the summed `evaluation_us` field.

Run `python3 audit.py` from any directory to authenticate both raw logs,
recompute their structural and semantic comparison, and verify the headline
ratios. See [`docs/direct_atkin_mitm.md`](../../../docs/direct_atkin_mitm.md)
for the proof and review boundary.
