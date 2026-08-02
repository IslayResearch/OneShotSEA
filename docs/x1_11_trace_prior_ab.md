# p125 X1(11) point-four trace-prior A/B

This note records a bounded, paired p125 comparison of X1(11) point-four
search before and after seeding SEA with the family's exact trace congruence.
Both runs covered the same deterministic curves at global indices `[12,22)`.
It is benchmark evidence only: both runs found zero certificates, and neither
checkpoint advances the authenticated production cursor.

The complete compact record is
[`artifacts/local/p125-x1-11-trace-prior-ab-20260801/result.json`](../artifacts/local/p125-x1-11-trace-prior-ab-20260801/result.json).
The host-local raw files remain under
`/tmp/oneshotsea-family-ab-20260801.Ba9BnP/x1-point4` and
`/tmp/oneshotsea-x1-prior-20260801.uDH5Ni`; the artifact pins every file's
path, byte size, line count, SHA-256 digest, and checkpoint CRC64.

## Paired setup and identities

Both invocations used prime p125, seed `202607300000`, X1(11) with the
point-four filter, indices 12 through 21, ten rolling curve threads, one SEA
thread and one smooth thread per curve, the same 5.4 GB smooth cache, the same
authenticated Weber tables and verifier, maximum level 401, trace cap 64, and
the same smoothness resource limits.

This was not a same-binary feature toggle:

| run | commit | binary SHA-256 | schedule SHA-256 |
|---|---|---|---|
| baseline | `4d0df939...20798` | `1ab2ffa0...09cf2` | `d81c3f2a...abec3` |
| trace prior | `8c1605f0...acd4c` | `feb9c06a...288aa` | `8a7b2af7...2ce8` |

The differing build and schedule identities are intentional and fully pinned.
They also limit causal interpretation: the measured delta is a same-curve
before/after observation, not an isolated runtime switch inside one binary.

## Curve and trace audit

The deterministic generator recorded the identical rejection-count vector in
both runs:

`[87, 148, 27, 47, 308, 36, 28, 46, 63, 251]`.

More strongly, every one of the 581 SEA levels executed by the prior build has
a corresponding baseline level with identical exact/Atkin classification,
exact trace residue, Atkin projective order and residue count, and compatible
source-lift count.  There were zero mismatches.  This validates that the paired
indices reached the same curve outputs through the SEA computations.

Here `p + 1 = 6 (mod 88)`, so the selected curve uses trace prior `t = 6
(mod 88)` and the selected twist uses `t = -(p + 1) = 82 (mod 88)`.  The prior
run recorded residue 6 on indices 15, 16, 18, 19, 20, and 21, and residue 82 on
indices 12, 13, 14, and 17.  All ten residues reduce modulo 11 to the
independently computed baseline ℓ=11 trace residue.  Across the union of both
runs, eight curves emitted an exact signed trace; every one also matches its
prior modulo 88.  The remaining two have the same independent ℓ=11 check.

The baseline evaluated ℓ=11 once per curve.  The prior build evaluated it zero
times in both progress and live-level telemetry, because 11 already divides
the exact modulus 88.  Skipping ℓ=11 is therefore both observed and backed by
the independent baseline residues.

## Measured result

| metric | baseline | trace prior | change |
|---|---:|---:|---:|
| real time | 294.57 s | 272.19 s | -22.38 s (-7.60%) |
| wall seconds/curve | 29.457 | 27.219 | -7.60% |
| wall curves/second | 0.0339478 | 0.0367390 | 1.08222x |
| user time | 1618.52 s | 1727.19 s | +108.67 s |
| sys time | 4.94 s | 5.01 s | +0.07 s |
| sum SEA time | 1217.423 s | 1186.396 s | -31.027 s (-2.55%) |
| SEA levels | 597 | 581 | -16 (-2.68%) |
| exact SEA levels | 315 | 301 | -14 (-4.44%) |
| Atkin SEA levels | 11 | 11 | unchanged |
| summed smoothness time | 327.610 s | 469.073 s | +141.463 s |
| summed concurrent curve time | 1567.419 s | 1678.006 s | +110.586 s |
| full point counts | 3/10 | 5/10 | +2 |
| peak RSS | 5.969 GiB | 6.202 GiB | +0.233 GiB |

Whole-invocation wall throughput improved by **1.08222x** on these ten paired
curves.  The prior also removed 16 SEA levels, including all ten ℓ=11 levels,
and reduced summed SEA time by 2.55%.

The summed concurrent curve time moved in the other direction because ten
curve tasks overlap and it is not wall time.  Smoothness work also depends on
the trace set surviving when sound rejection becomes possible.  In particular,
`full_point_count` is an execution-path property here: sound smoothness
rejection can terminate with multiple traces, so different full-count flags do
not mean the builds computed different mathematical curves.  All ten terminal
statuses were the same sound smoothness rejection, and both runs produced zero
certificates.

The 7.60% wall reduction is a measured result, but it should not be attributed
entirely to the trace prior.  The sample has only ten concurrent curves, and
the compared binaries differ.  Host load, task overlap, SEA tails, and
smoothness tails remain confounders.  A same-build runtime toggle and repeated
windows would be needed for a clean causal estimate.

## Why `/usr/bin/time -l` returned 1

After printing real, user, and sys times, the macOS timing wrapper reported:

`time: sysctl kern.clockrate: Operation not permitted`

That blocked post-run sysctl made the wrapper return status 1.  It did not make
the search partial or failed: each log contains a completed search summary,
each checkpoint has `next_index: 22`, all ten progress records are present,
and the range is exhausted.  Peak RSS in the table comes from the search's own
progress telemetry; unavailable extended `time -l` host counters are not
inferred.
