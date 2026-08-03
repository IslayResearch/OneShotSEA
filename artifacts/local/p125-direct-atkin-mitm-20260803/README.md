# Controlled p125 direct-SEA combined-patch comparison

This bundle is a controlled A/B of parent commit
`bbcd04d2c27d87f582f4d579caaacd9d4278ee8e` and candidate commit
`13cd3a906167700b795b36abaae51c6433017fc8`. The candidate is the complete
combined patch: factored CRT meet-in-the-middle counting/enumeration, the
uniform factor-degree certificate, and its baby-step/giant-step modular
composition primitive. The measurement does not isolate those components.

Both source archives were built on the same Apple M4 host with the same
candidate profiler source, Makefile build rule, Apple clang 21.0.0 flags, GMP
6.3.0 libraries, authenticated cache, and byte-identical argument vector. The
two runs were executed serially. `provenance.json` records the full commit/tree,
host, compiler, flags, library, cache, harness, binary, static-library, command,
and raw-log identities. `commands.sh` reconstructs both immutable source trees,
builds them, runs the identical command, and structurally audits a replay.

Across the unique ordered 16-curve by 15-level grid, all 240 mathematical and
control projections are identical, all 64 independent Schoof controls agree,
and no result is unconstrained. Raw level and curve records recompute every
retained summary aggregate. The controlled measurement is:

| Metric | Baseline | Combined candidate | Ratio |
|---|---:|---:|---:|
| Direct evaluation | 427.991317 s | 39.033330 s | 10.9648x faster |
| Peak RSS | 1,159,053,312 B | 40,370,176 B | 28.7106x lower |
| Whole cohort | 567.560778 s | 195.298315 s | 2.9061x faster |

This controlled rerun supersedes the earlier 7.79x draft measurement, whose
retained baseline and candidate did not have an auditable identical
configuration. The valid result is attributed only to the complete combined
candidate commit, not to MITM alone.

Run `python3 audit.py` from any directory. The auditor authenticates every
bundle file, validates provenance and the exact raw schemas/order/bounds,
derives summaries and `result.json` from raw records, and checks the cross-run
configuration and semantic equality. See
[`docs/direct_atkin_mitm.md`](../../../docs/direct_atkin_mitm.md) for the proof
and claim boundary.
