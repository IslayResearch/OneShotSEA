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
| P0 | SEA cost per admitted curve | Commit `b41311a` verifies Weber 24th-root covariance and reuses one modular-root evaluation per lift orbit. A 64-level production index-4 replay matched every canonical level record while modular roots fell from 1,252.390 to 207.917 s (6.023x) and the full SEA comparison improved 3.759x. | Measure complete optimized production curves from index 6; the remaining measured serial tail is eigenvalue recovery, not modular-root evaluation |
| P0 | Production certificate search | Commit `4eda927` soundly rejected unique indices 3--5 and advanced its authenticated cursor to 6. Across unique indices 0--5, 140 curve/twist orders were exactly screened and no certificate exists yet. | Publish the tested orbit-reuse milestone, then continue from a clean committed-build identity whose global range begins at 6 |
| P1 | Search parallelism | Orbit reuse collapses the level-193 root jobs from 504 to 42 and reduces mean modular-root time 3.445x; eigenvalue recovery remains about 24.7 s and limits the matched wall speedup to 2.366x. AWS `m8g.xlarge` was 2.04x slower than the pre-orbit local comparison and is off. | Keep one ten-thread local worker and profile complete optimized curves before adding another process or cloud shard |
| P1 | Certificate hit-rate model | No production hit yet; curve/twist are already screened together | Measure exact-smooth survivor rate on admitted Hasse-near order pairs without overlapping a live timing run |
| External | Cloud capacity | RunPod console access and AWS CLI identity are confirmed, but RunPod API/SSH variables remain unproven. The bounded tagged AWS trial is terminated; `m8g.xlarge` was 2.04x slower than local and the 64-vCPU quota request remains pending. | Keep AWS compute off; benchmark `c8g.4xlarge` only after quota approval, with the existing tag/budget/hard-stop controls |
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
| First production Atkin constraints | Pinned classical `Phi_5`/`Phi_7` factor degrees are used only with monic, square-free, uniform-degree no-root evidence; Atkin state is separate from the exact unique-trace gate and the progress auditor independently reconstructs it |
| Weber table trust boundary | Production startup pins the checked-in manifest digest and verifies the complete table filename set, byte counts, and every SHA-256 before SEA; tests reject missing, extra, and altered tables |
| Weber 24th-root orbit reuse | Covariance is verified term-by-term from each loaded table; a controlled off/on ablation and a 64-level retained production replay have identical canonical mathematical projections |

## Measurement discipline

- Do not run a 5 GB cache benchmark concurrently with an SEA timing run.
- Resource-limit outcomes are implementation evidence, not mathematical
  rejection evidence.
- Search ranges are global half-open intervals partitioned exactly once by the
  production CLI; all workers in one partition share the same seed and worker
  count.
- Commit and push each tested milestone before deploying it or changing the
  live search identity.
