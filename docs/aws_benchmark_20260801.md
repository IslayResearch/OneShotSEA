# AWS Graviton4 SEA benchmark, 2026-08-01

This bounded trial answers whether the quota-compatible AWS CPU fallback adds
useful `p125` SEA throughput relative to the local Apple M4.  It exercises the
actual CAS-free Weber SEA hot path on deterministic global index 1, not a toy
field or synthetic polynomial.

## Identity and resource envelope

- Git commit: `00969b73f2983d3f07aa2c56603a0014f308714a`
- Binary SHA-256: `c038412c82cf0220066cec82f9329a316d2abc5e07ce14b7d13222a58148c3ca`
- Instance: `i-0f7ef5978ecf7c1e9`, `m8g.xlarge`, four AWS Graviton4 cores,
  15 GiB visible RAM, Amazon Linux 2023, GCC 11.5.0
- Region/rate: `us-east-2`, on demand at `$0.17952/hour`
- Launch: `2026-08-01T19:01:39Z`; user-requested termination:
  `2026-08-01T19:26:35Z`
- Safety: zero security-group ingress, IMDSv2 required, encrypted
  delete-on-termination root volume, and a 30-minute hard-stop timer
- Cost tags: `Project=OneShotSEA`,
  `LaunchId=p125-m8g-trial-20260801a`

The exact command is retained separately for each thread count.  In compact
form it was:

```sh
build/oneshotsea sea-weber-count \
  --p 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 \
  --a 82574189722949072957728522659737830385005094873485824210996693879529527019787746889866491875855403894941918864621858709713497 \
  --b 88382793148632715305152348439825220256670063248990549473997795919686351346525164593244327917236935929961279243081239139809077 \
  --max-level 193 --trace-cap 64 --table-dir data/modpoly/weber_f \
  --sea-threads THREADS
```

All three runs processed the same 42 levels and produced the same exact
modulus, trace-candidate count, compatible source-lift count, and
`level_limit` summary.

## Scaling result

| Threads | Wall time | User CPU | CPU utilization | Accounted SEA time | Speedup | Parallel efficiency |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 614.87 s | 614.89 s | 100% | 614.731 s | 1.00x | 100% |
| 2 | 324.76 s | 613.77 s | 188% | 324.618 s | 1.89x | 94.7% |
| 4 | 179.20 s | 610.44 s | 340% | 179.058 s | 3.43x | 85.8% |

The modular-root subtotal scaled from 579.685 seconds at one thread to
144.100 seconds at four.  The nonparallel eigenvalue subtotal remained about
30.4 seconds, making it the next scaling limit on this machine.  Peak RSS was
only 6.5 MB because this benchmark deliberately excludes the 5.4 GB
exact-smooth cache.

The same 42 levels in the retained Apple M4/ten-thread optimized index-1 replay
account for 87.813 seconds.  On this matched arithmetic slice, the four-core
AWS fallback is therefore 2.04x slower than the local machine.  Running both
concurrently could raise aggregate throughput to about 1.49 local-machine
equivalents, but AWS does not shorten the latency of one curve and adds remote
operations and cost.  At the measured four-thread rate, this partial SEA pass
costs about `$0.00894` of instance time per curve.

The instance ran for 1,496 billed-wall seconds, corresponding to `$0.07460`
of instance time, plus the small EBS/data-transfer charge.  The fetch-time
upper-bound estimate was `$0.07408`; Cost Explorer still reported `$0.00`
with `estimated=true` immediately after termination because tagged billing
data lags usage.

## Decision

Do not use `m8g.xlarge` as a speed replacement for the local M4.  It is useful
only as inexpensive additive shard capacity or to validate cloud operations.
The requested 64-vCPU quota increase remained `CASE_OPENED` during this trial.
Benchmark a 16-core `c8g.4xlarge` after approval before starting a production
search; do not infer its value by assuming perfect four-to-sixteen-core
scaling.

The complete fetched bundle is under
`artifacts/aws/p125-m8g-sea193-20260801/i-0f7ef5978ecf7c1e9`.  Its top-level
`SHA256SUMS` validates the archive, build/runtime environments, exact commands,
per-level JSONL, GNU-time records, manifest, and fetch/instance metadata.  The
benchmark manifest SHA-256 is
`203b87d8443b74344d776887cd3b3ab861dd0c3738361e1c3d4e9f977a9e1f93`.
