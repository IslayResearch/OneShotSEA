# p125 deferred direct-SEA cap-one suffix

This artifact validates implementation commit
`07377b5e4b7c263348ef9d7c09e79286f53d0463` at the 416-bit target
`nextprime(10^125) = 10^125 + 237`. The policy prepares one authenticated
22-level direct context, uses the first 20 levels for ordinary cap-16
screening, and defers levels 89 and 97 until a complete multi-trace set has
survived exact smoothness screening and needs the cap-one gate.

## Production-path no-charge check

The retained search ran deterministic X1(27) indices 2,000,001 through
2,000,004 with trace cap 16. All four curves ended in the sound smoothness
rejection path. Each curve emitted exactly one direct pass containing the
selected 20-level prefix:

```text
7,5,11,13,19,17,23,29,31,37,41,43,47,53,67,71,79,61,73,59
```

No curve emitted a direct level 89 or 97 and no curve started a second direct
pass. Thus the four rejected curves caused 80 prefix evaluations and zero
suffix evaluations even though the authenticated cache contains all 22
contexts. The raw progress and terminal checkpoint are retained under
`raw/`.

This statement is specifically about the cached direct schedule. The ordinary
Weber pass can independently encounter table levels 89 or 97; that is not a
load or evaluation of the deferred classical-j specialization.

## Deterministic cap-one replay

The checked-in `validate_p125_cap_one_tail` tool reconstructed the complete
cap-16 states for the three multi-trace fixtures, then compared two cap-one
continuations from identical retained evidence. The baseline continued Weber.
The candidate evaluated the real cached 89/97 suffix first and used Weber only
if it remained incomplete.

| index | cap-16 traces | baseline last Weber level | suffix last Weber level | direct suffix levels | exact trace preserved |
|---:|---:|---:|---:|---:|:---:|
| 2,000,002 | 13 | 263 | 257 | 2 | yes |
| 2,000,003 | 4 | 277 | 269 | 2 | yes |
| 2,000,004 | 5 | 379 | 373 | 1 | yes |

In every replay the suffix reduced the effective candidate count to one, so
no later Weber continuation was needed. Index 2,000,004 evaluated only direct
level 89 because its retained Weber state already owned modulus 97. All three
traces equal the independently retained PARI/GP 2.17.4 point counts for the
same deterministic curves.

The replay loaded 65 direct contexts: 20 prefix contexts for each of three
curves, followed by suffix counts 2, 2, and 1. Peak cached residency was one
context. This independently exercises lazy prefix loading and duplicate-level
skipping.

## Performance interpretation

The deterministic result is avoided work and correct stopping order, not a
controlled speedup. In this replay the baseline Weber continuations summed to
29.825 seconds and the direct suffix evaluations summed to 14.673 seconds,
but they were sequential, non-randomized measurements on one machine. The
production cohort contained no smoothness survivor, so it intentionally
validates that rejected curves do not pay for the suffix rather than measuring
certificate throughput.

This optimization changes a policy constant and the expected cost of rejected
curves. It does not change the proposed `p^(1/8+o(1))` outer search exponent,
establish the SEA/CM crossover, measure production yield, or find a p125
certificate.

## Reproduce and audit

The direct cache was prepared with the ordered 22-level schedule, construction
caps 10,000,000 and 1,000,000, one preparation thread, and a 1,000,000,000-byte
admission ceiling. Its unretained 150,799,468-byte file had SHA-256:

```text
a459dc7732e0a8924f3dcce15bd640c5a72f594d403bf117d0fa45c6b3805625
```

After building the implementation commit and obtaining that authenticated
cache, the focused replay command is:

```sh
./build/validate_p125_cap_one_tail \
  data/modpoly/weber_f \
  /path/to/selected20-tail89-97.ctx \
  a459dc7732e0a8924f3dcce15bd640c5a72f594d403bf117d0fa45c6b3805625
```

Run `python3 audit.py` in this directory to authenticate every retained file,
resolve the implementation tree, replay the production and diagnostic
invariants, and compare the exact traces and orders with the retained PARI
record.
