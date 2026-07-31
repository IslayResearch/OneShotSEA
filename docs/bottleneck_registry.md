# Dependency and bottleneck registry

This registry tracks the shortest path to the required verified `p125`
certificate.  Measurements refer to the Apple M4 local host unless stated
otherwise.  A benchmark result is not a completion claim; the authoritative
completion gate remains acceptance by the unmodified pinned `voneshot.py`.

## Outcome gates

| Gate | State | Required evidence |
|---|---|---|
| Newly searched `p125` certificate | **open** | Durable canonical certificate, identity-bound checkpoint/log, and local pinned-verifier `True` transcript |
| Reproducible production search | **open** | Clean-checkout command and retained range/seed/build/cache/table identities on the winning run |
| `p130` continuation | waiting on `p125` | Same implementation and artifact discipline after all mandatory `p125` gates close |

## Active bottlenecks and dependencies

| Priority | Item | Evidence / decision | Next gate |
|---|---|---|---|
| P0 | SEA cost per admitted curve | `p125` index 0: 749.608 s SEA of 753.377 s total | Filter certificate-incompatible Weber images before SEA; then resume deterministic search |
| P0 | Cloud launcher must run the production CLI | Audit found stale option names, nonexistent CMake build instructions, and double sharding | Real-command contract test, exactly-once range union, authenticated shared cache, manifest-bound argv/deployment |
| P1 | Implementation limits must not skip indices | A low `max-level` can advance past a curve that succeeds with adequate tables | Non-advancing checkpoint/progress regression for `sea_level_limit` and other implementation-only outcomes |
| P1 | Search parallelism | No CUDA SEA kernel exists; local SEA uses unbounded `std::async` jobs over source lifts | Add measured CPU concurrency control before cloud CPU/GPU comparison; use a GPU only after a throughput win |
| P1 | Certificate hit-rate model | No production hit yet; curve/twist are already screened together | Measure exact-smooth survivor rate on admitted Hasse-near order pairs without overlapping a live timing run |
| External | RunPod access | No `RUNPOD_*` credentials or saved pod state were present on 2026-07-31 | Authorized operator configures credentials; dry-run and local contract gates must pass first |
| External | Local Magma process state | Two Rosetta Magma processes are stuck uninterruptibly; prior independent p125 trace/order result is retained | Do not put Magma in production path; recover oracle service before new mandatory oracle runs |

## Resolved or bounded items

| Item | Resolution evidence |
|---|---|
| Full-cache smoothness memory | Commit `6538bc7`; real 5 GB cache extraction completed with exact known outputs, 5.55 GB maximum RSS, zero swaps |
| Early trace-cap policy | Cap 195 on index 0 cost 99.722 s to save 68.115 s of SEA; cap 4096 on index 1 screened 1,188 traces and took 1,097.637 s total. Keep the default at 64 with explicit overrides. |
| Cache trust/completeness boundary | Every pre-existing exact cache requires a trusted lowercase SHA-256; p125 cache digest is pinned in `data/smooth_cache/TRUSTED_MANIFEST.json` |
| Candidate divisor coverage | Exhaustive bounded DFS tries every admissible divisor while failing closed on candidate/node caps |
| Final acceptance | Native validation is followed by the unmodified pinned canonical verifier under an authenticated Python 3 runtime |

## Measurement discipline

- Do not run a 5 GB cache benchmark concurrently with an SEA timing run.
- Resource-limit outcomes are implementation evidence, not mathematical
  rejection evidence.
- Search ranges are global half-open intervals partitioned exactly once by the
  production CLI; all workers in one partition share the same seed and worker
  count.
- Commit and push each tested milestone before deploying it or changing the
  live search identity.
