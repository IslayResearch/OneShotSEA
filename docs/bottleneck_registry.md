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
| P0 | SEA cost per admitted curve | Batched exact polynomial reduction replayed filtered index 1 with the same outcome and reduced SEA from 770.594 s to 293.641 s; modular roots fell from 603.731 s to 223.681 s and eigenvalues from 155.147 s to 59.865 s | Commit after the full oracle gate, then measure thread scaling and resume in a new identity-bound production directory |
| P0 | Cloud launcher must run the production CLI | Audit found stale option names, nonexistent CMake build instructions, and double sharding | Real-command contract test, exactly-once range union, authenticated shared cache, manifest-bound argv/deployment |
| P1 | Search parallelism | SEA modular-root work now uses a fixed worker group bounded by explicit `--sea-threads`; no CUDA SEA kernel exists | Measure the explicit CPU thread setting before cloud CPU/GPU comparison; use a GPU only after a throughput win |
| P1 | Certificate hit-rate model | No production hit yet; curve/twist are already screened together | Measure exact-smooth survivor rate on admitted Hasse-near order pairs without overlapping a live timing run |
| External | Cloud capacity | RunPod console access and AWS CLI identity are confirmed, but RunPod API/SSH variables remain unproven; AWS `us-east-2` Standard-instance quota is 5 vCPUs and a no-cost increase to 64 is pending | Finish dry-run AWS controls; benchmark CPU instances only after an explicit bounded launch configuration |
| External | Local Magma launcher | The documented Magma V2.29-1 launcher is healthy but outside `PATH`; a 2026-08-01 smoke test returned the correct order/trace for `p=97,a=2,b=3` | Set `MAGMA=/Users/agent/Documents/Codex/t24-search/private/magma-local/install/magma` explicitly for mandatory oracle gates |

## Resolved or bounded items

| Item | Resolution evidence |
|---|---|
| Full-cache smoothness memory | Commit `6538bc7`; real 5 GB cache extraction completed with exact known outputs, 5.55 GB maximum RSS, zero swaps |
| Certificate-incompatible curve generation | The exact j-to-Montgomery admission predicate rejects the old p125 index-0 curve before SEA. A `p=10093` census admitted 50.89%; a 32-index p125 probe needed 41 rejected attempts in 0.48 s, making every scheduled SEA curve assembly-eligible. |
| Implementation-limit cursor safety | `sea_level_limit` and `no_rational_weber_lift` now checkpoint/report without changing cursor or counters; the regression retries the same index with adequate levels and obtains a canonical certificate. |
| Early trace-cap policy | Cap 195 on index 0 cost 99.722 s to save 68.115 s of SEA; cap 4096 on index 1 screened 1,188 traces and took 1,097.637 s total. Keep the default at 64 with explicit overrides. |
| SEA task fan-out | Modular-root jobs are capped by `--sea-threads` and the source-lift count; the serial and bounded-parallel paths are differentially tested and report actual per-level worker counts. |
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
