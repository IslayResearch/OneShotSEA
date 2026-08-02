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
| P0 | SEA cost per admitted curve | Threshold-32 exact Karatsuba cut current-path warm curve work on indices 8/9 from 329.264/254.163 s to 110.340/90.010 s, with complete canonical projections identical. Isolated degree-194 quotient Frobenius improved 1.554x. The remaining quadratic reduction loops still dominate repeated quotient arithmetic. | Gate the coefficient-reference reducer cleanup, then measure a reusable reciprocal reduction context only if the simple exact change leaves a material ceiling |
| P0 | Production certificate search | Commit `c5de573` soundly rejected unique indices 9--11 and advanced its authenticated cursor to 12. Across unique indices 0--11, 272 trace-candidate order values were exactly screened and no certificate exists. Those values represent only 24 actual curve/twist group orders. | Continue from a clean committed-build identity whose global range begins at 12 after the current hot-path gate |
| P1 | Search parallelism | Commit `f8347c6` adds a rolling ordered curve window against one immutable 5.4 GB smooth cache. Same-binary K=1/2/3/5/10 scaling reached 100.175/57.800/40.501/34.185/27.071 warm seconds per curve. K=10 reported 6.19 GB RSS and 68.9 MiB of system-wide swapouts; all overlapping canonical records match. | Use `curve_threads=10, sea_threads=1, smooth_threads=1` with memory telemetry; do not exceed the ten physical cores without a new bounded A/B |
| P1 | Certificate hit-rate model | The production prefilter forces full rational E[2], so the reproducible Dickman--Mertens model uses divisor 4 on both actual orders and estimates an optimistic `8.300e-6` opportunity per curve: mean 120,489 curves, before correlation, group-exponent, or assembly loss. At 27.071 warm seconds/curve the optimistic mean is 37.8 days. X1-selected divisors 44/88 keep divisor 4 on the paired twist and give 1.177x/1.239x multipliers. | Run a same-build K=10 Weber/X1(11)-point4 comparison; select X1 only if its measured throughput penalty is below its conservative 1.177x yield gain |
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
| Rolling shared-cache curve execution | One immutable exact-smooth engine is used concurrently; durable state remains ordered and lower-index implementation limits/certificates discard higher speculative reports. The resource setting is resumable and absent from semantic identity. |
| Required performance ablations | Early abort, batching, curve/twist sharing, specialized Weber work, and prime scheduling are quantified. The held-out measured schedule was 0.7% slower than increasing order; the common-level Weber/classical boundary is honestly negative, while no classical 77-level production schedule exists. |

## Measurement discipline

- Do not run a 5 GB cache benchmark concurrently with an SEA timing run.
- Resource-limit outcomes are implementation evidence, not mathematical
  rejection evidence.
- Search ranges are global half-open intervals partitioned exactly once by the
  production CLI; all workers in one partition share the same seed and worker
  count.
- Commit and push each tested milestone before deploying it or changing the
  live search identity.
