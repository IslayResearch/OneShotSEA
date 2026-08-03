# p125 batched direct-interpolation audit

This bundle retains the isolated A/B/A measurement and the production replay
for commit `131b19b1e0fac144d17a1af90428f611045c19b1`.  That commit changes the
compact classical direct-SEA specialization hot path to accumulate modular
matrix products in provably non-overflowing `unsigned __int128` batches.  It
also computes the derivative powers once per specialization.  It does not
change the direct-context format, level schedule, Atkin/Elkies classification,
trace combination, or early-abort policy.

## Result

The profiler evaluated levels 61, 67, 71, 73, 79, 83, 89, and 97 on the same
four deterministic X1(27) curves over `nextprime(10^125)`.  Each run used one
thread and the same authenticated 140,599,516-byte prepared context.  The
timed scope is only the 32 direct level evaluations; cache indexing, curve
generation, one-time context materialization, and Schoof controls are
excluded.

| run | implementation | summed evaluation time |
| --- | --- | ---: |
| baseline A | `cdecbdd` | 38,214,216 us |
| candidate | `131b19b` | 31,727,943 us |
| baseline B | `cdecbdd` | 40,633,241 us |

Relative to the mean of the two bracketing baselines, the candidate used
19.52069424% less direct-evaluation time.  All 32 records agreed exactly on
the curve, trace prior, level, exact/Atkin classification, trace residue or
Atkin order and residue count, and information content.  All three summary
records reported the same 169,918,464-byte peak RSS.

The candidate was also run through the actual four-curve p125 search using the
selected-20 prefix, the 89/97 pre-smooth suffix, the authenticated one-gigabyte
resident context, and Weber continuation through level 401.  Its status,
initial and final trace counts, direct and Weber work counts, and exact traces
matched the previously retained production run.  All four traces also match
the independently retained PARI/GP point counts.  The production timing was
thermally noisy and is deliberately not used for a speedup claim.

## Why this is worth review

This is a small, source-local optimization on the path the customized SEA
design actually spends time in, backed by real 416-bit arithmetic rather than
a toy modulus.  The evidence separates mathematical equivalence, isolated
kernel timing, and end-to-end correctness.  A reviewer can therefore focus on
the two nontrivial questions: whether the accumulator-capacity proof is sound
for every 64-bit modulus, and whether the new matrix traversal preserves the
interpolation/derivative orientation.  Passing unit, integration, ASan, and
UBSan tests plus exact production traces makes this a useful review target,
not a speculative refactor.

## Claim boundary

This is a measured constant-factor improvement to the existing specialized
classical-`j` direct evaluator.  It does not change the heuristic SEA exponent,
prove the claimed `p^(1/8+o(1))` one-shot search bound, establish the CM/SEA
crossover, measure certificate yield, or remove the finite prepared-context
producer.  Those remain separate requirements for the intended asymptotic
implementation.

## Reproduce the audit

From the repository root:

```sh
python3 artifacts/local/p125-direct-batched-interpolation-20260803/audit.py
```

The audit authenticates every retained file, resolves both implementation
trees, re-sums all timing records, checks exact semantic equality across the
A/B/A, replays the production comparison, and validates the four traces
against the independent point counts.
