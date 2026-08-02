# RunPod CPU SEA benchmark, 2026-08-02

This bounded trial measures the current CAS-free Weber SEA hot path on a
stateful RunPod CPU worker before using that worker for the `p125` search.  It
uses the same deterministic target and curve as the retained local and AWS
level-193 trials.  The arithmetic summary is retained and compared before any
production work is admitted.

## Identity and resource envelope

- Run id: `runpod-cpu16-bench-20260802a`
- Pod id: `ohfo3hbov7ot8v`; workload `cpu3g`
- Git commit: `93da28b88524c22f77bdacc6dc819bfa83dfd60d`
- Binary SHA-256:
  `53d2ceae88c3af3f5d07a2e5907af23f4bd417c9da7c10a3f0f7b69efd457ac3`
- Host CPU: AMD EPYC 7702P; the container cpuset admits 16 logical CPUs
- Memory allocation: 64 GiB; compiler: GCC 11.5.0 on x86-64 Ubuntu 20.04
- Rate: `$0.640/hour` compute plus `$0.003/hour` disk
- Benchmark interval: `2026-08-02T16:18:13Z` through
  `2026-08-02T16:18:57Z`

The exact retained command is:

```sh
build/oneshotsea sea-weber-count \
  --p 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 \
  --a 82574189722949072957728522659737830385005094873485824210996693879529527019787746889866491875855403894941918864621858709713497 \
  --b 88382793148632715305152348439825220256670063248990549473997795919686351346525164593244327917236935929961279243081239139809077 \
  --max-level 193 --trace-cap 64 \
  --table-dir data/modpoly/weber_f --sea-threads 16
```

## Result

The trial completed in 31.08 wall seconds with 31.04 user seconds, 99% CPU,
and 6,164 KiB peak RSS.  It processed 42 levels and ended at the intended
`level_limit` boundary.  Its final exact modulus was
`554564412780858924938214705907886692705`; the Atkin-aware constraint modulus
was `3881950889466012474567502941355206848935`.  The exact-only candidate
count was `2280909187310568780087046`, the constrained candidate count was
`651688339231591080024870`, and there were 12 compatible source lifts.

The retained AWS trial reported the same 42 levels, exact modulus, exact-only
candidate count, and compatible-source-lift count.  The additional constraint
modulus and reduced candidate count are expected from the subsequently added
Atkin-state implementation.  The fetched bundle passes its own SHA-256
manifest locally.

The 31.08-second observation is 1.53 times shorter than the 47.64-second
optimized Apple M4 trial and 5.77 times shorter than the 179.20-second
four-thread Graviton4 trial.  Those are cross-commit planning comparisons, not
a same-binary hardware A/B: the arithmetic output is compatible, but the
RunPod commit includes later implementation changes.  In particular, the
RunPod process used only one effective CPU despite `--sea-threads 16`, so the
production benefit must be measured with curve-level concurrency rather than
inferred from the advertised vCPU count.

The complete fetched bundle is under
`artifacts/runpod/runpod-cpu16-bench-20260802a/runpod-cpu16-bench-20260802a`.
It retains the exact command, environment and cpuset, GNU-time record, raw
NDJSON, manifest, exit status, and a validated `SHA256SUMS` file.
