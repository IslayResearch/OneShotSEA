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
| P0 | SEA cost per admitted curve | Commit `164814b` retains full rational-isogeny validation but derives the conjugate Frobenius eigenvalue after one recovery. On the 64-level index-4 replay, eigenvalue time fell from 148.418 to 70.196 s and wall from 377.42 to 291.43 s, with every canonical record identical. Combined with root-orbit reuse, the comparison against the original 1,418.823 s SEA stage is 4.868x. | Measure complete conjugate-optimized production curves from index 8 and profile the new root/eigen balance before another hot-path change |
| P0 | Production certificate search | Commit `deaeaf4` soundly rejected unique indices 6 and 7 and advanced its authenticated cursor to 8. Across unique indices 0--7, 204 curve/twist orders were exactly screened and no certificate exists yet. | Publish the tested conjugate milestone, then continue from a clean committed-build identity whose global range begins at 8 |
| P1 | Search parallelism | Orbit reuse collapses level-193 roots from 504 evaluations to 42; conjugate reuse then reduces 42 full eigen recoveries to 21. Complete orbit-only curve work ranged from 180.496 to 525.564 s on indices 6--7. AWS `m8g.xlarge` was already 2.04x slower than the pre-orbit local comparison and is off. | Keep one ten-thread local worker and use new production telemetry before considering cross-level batching or a second memory-heavy process |
| P1 | Required performance ablations | Early abort, reducer batching, curve/twist work sharing, and Weber internal specialized-path ablations are documented. A same-binary common-level boundary finds Weber 12.449x/10.982x slower than classical-j at levels 5/7; no comparable classical production schedule exists. No same-binary alternate prime schedule has been run. | Freeze a disjoint training/measurement curve set and close the schedule comparison |
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
| Conjugate Frobenius reuse | A same-binary off/on ablation and full production replay partition every validated kernel into independent/derived telemetry; 42- and 64-level canonical projections are identical, including scalar/non-scalar safety tests |

## Measurement discipline

- Do not run a 5 GB cache benchmark concurrently with an SEA timing run.
- Resource-limit outcomes are implementation evidence, not mathematical
  rejection evidence.
- Search ranges are global half-open intervals partitioned exactly once by the
  production CLI; all workers in one partition share the same seed and worker
  count.
- Commit and push each tested milestone before deploying it or changing the
  live search identity.
