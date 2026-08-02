# Retained-state exact Schoof fallback

The optional `--schoof-fallback 1` path completes rare SEA states that exhaust
every authenticated Weber table without fitting the requested trace cap. It
adds a fixed, schedule-bound tail of exact Schoof residues while retaining the
already computed trace prior, exact CRT, certified Atkin constraints, and
Weber telemetry. It does not rerun the full table schedule.

The fixed policy is
`retained-state-exact-schoof-3,5,13,17,19-v1`. Missing exact moduli are tried
in that order and the extension stops as soon as the complete exact and
effective Hasse sets fit the requested cap. A modulus already present in the
exact state is skipped. When a Schoof residue upgrades an existing Atkin
modulus, the implementation first verifies agreement with the certified Atkin
residue set, then rebuilds the effective state from exact constraints plus
only the remaining nonredundant Atkin constraints. Contradiction fails closed.
Each level is staged copy-on-write: contradictory Atkin evidence or a throwing
progress callback leaves the retained exact/effective state and fallback
telemetry unchanged, including a prior bounded trace enumeration during a
second-stage continuation.

The option defaults off and changes the schedule digest. It is independent of
`--skip-incomplete-curves`: production can enable exact fallback while leaving
the heuristic incomplete skip disabled. The curve record retains every
fallback level's prime, residue, exact/effective modulus, exact/effective Hasse
candidate count, and elapsed time. If an early screen has a survivor, the
unique-trace pass continues the same retained state and computes only new
fallback levels; it does not repeat SEA.

Checked-in tests cover exact/Atkin upgrades and contradictions, existing exact
modulus skips, a two-stage cap-16-to-cap-1 continuation, post-identity option
mutation, and transactional callback failure. Four small-field brute-force
differentials recover the complete trace. A known supersingular curve over
`p=10000019` forces every fixed level through 19 and recovers trace zero,
covering the expensive end of the policy without adding a target-sized test to
the fast suite.

## Index-206 recovery result

The first target-sized replay used deterministic X1(11) point-four index 206,
cap 16, fallback enabled, and incomplete skip disabled. Ordinary Weber SEA
exhausted 76 levels with 27 exact levels and one Atkin level. Its final state
contained 45,948 exact candidates and 9,189 effective candidates, so neither a
larger range nor a larger cap could provide a complete cap-16 screen.

The retained-state extension recorded:

| exact level | residue | exact candidates | effective candidates | elapsed |
|---:|---:|---:|---:|---:|
| 3 | 0 | 15,316 | 3,063 | 0.005173 s |
| 5 | 0 | 3,063 | 3,063 | 0.028213 s |
| 13 | 2 | 235 | 235 | 1.039312 s |
| 17 | 0 | 14 | 14 | 2.925228 s |

The four exact residues cost 3.997926 summed seconds. Level 5 upgraded the
earlier singleton Atkin-5 constraint, and exact/effective state then agreed.
Level 17 brought both complete Hasse sets under cap 16, so level 19 was not
needed. Exact smoothness screening checked both signs for all 14 traces and
returned `sound_smoothness_reject`. The record is nonheuristic, did not finish
a singleton point count, made no assembly call, and found no certificate.

The full invocation took 172.17 seconds wall. Reported curve work was 96.703303
seconds SEA, 10.197681 seconds smoothness, and 106.962195 seconds total; setup
and cache-loading time is outside that curve subtotal. This single recovery
has no same-binary no-fallback completion baseline, so it is not reported as a
speedup.

An independent audit reconstructed the exact CRT from the mod-176 trace prior
and all 27 exact Weber residues. It reproduced 45,948 Hasse candidates,
reproduced 9,189 after the singleton `t=0 mod 5` Atkin condition, and replayed
every fallback transition through the final 14/14 counts.

The complete record is
[`artifacts/local/p125-index206-schoof-fallback-20260801/result.json`](../artifacts/local/p125-index206-schoof-fallback-20260801/result.json).
It pins the raw log/progress/checkpoint hashes, CRC64, command argv, patch,
binary, schedule, cache, table, verifier, and Python identities.

## Prototype boundary and required retest

This successful replay used a prototype binary built from the X1(27) base plus
mailbox patch SHA-256
`763a2745e180d52542889823e9adca725cf371d8d03c0a27d83d8d911ae7beb8`.
The emitted binary SHA-256 was
`6fbeef06f2980389244081666eb55d3d98d63965c3722bdccc1aeff26b517ee4`.
No launcher script was retained; the exact search argv is reconstructed in the
artifact from the authoritative start record and execution transcript. The
older retained one-thread 7d475fe harness is a different run and must not be
used as this result's identity.

The prototype checkpoint advances only its isolated `[206,207)` replay. It
does not rewrite the historical production checkpoint, where index 206 was
explicitly recorded as a heuristic incomplete skip. Before claiming
committed-build recovery, freeze and hash the final committed fallback binary,
pass the full test and oracle gates, and rerun index 206 with
`--skip-incomplete-curves 0`. Production should continue sound-only only after
that final-build replay agrees.
