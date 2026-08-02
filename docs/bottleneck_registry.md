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
| P0 | SEA cost per admitted curve | Threshold-32 exact Karatsuba cut current-path warm curve work on indices 8/9 from 329.264/254.163 s to 110.340/90.010 s, with complete canonical projections identical. A reusable reciprocal context was exact but rejected after slowing the degree-194 kernel by 14.31% and a production level by 6.94%. Generator-retained Weber source state removes redundant root-specialization orbits before this arithmetic. | Profile the committed retained-source production mix; pursue another arithmetic change only behind a bounded exact A/B |
| P0 | Production certificate search | Authenticated identities have soundly rejected every unique index 0--89. Commit `1e84475` covered 12--59; retained-source commit `d957b15` covered 60--89 with 30 sound rejections, 15 full point counts, and zero certificates. Its checkpoint is `next_index=90`; the modulus-176 schedule change deliberately makes it incompatible. | Start the next committed X1(11) point-four, cap-16 identity at global index 90; preserve prior artifacts and do not replay or double-count 0--89 |
| P1 | Search parallelism | Commit `f8347c6` adds a rolling ordered curve window against one immutable 5.4 GB smooth cache. Same-binary K=1/2/3/5/10 scaling reached 100.175/57.800/40.501/34.185/27.071 warm seconds per curve. K=10 reported 6.19 GB RSS and 68.9 MiB of system-wide swapouts; all overlapping canonical records match. | Use `curve_threads=10, sea_threads=1, smooth_threads=1` with memory telemetry; do not exceed the ten physical cores without a new bounded A/B |
| P1 | Certificate opportunity model and family selection | The reproducible Dickman--Mertens model gives X1(11) cyclic-44/group-order-88 multipliers of 1.1775x/1.2394x over full-E[2] Weber. The same-build family A/B favored X1 by 1.03945x wall throughput. Trace prior plus selected cap 16 then measured 260.34 s on the same ten X1 curves; relative to the earlier 306.19 s Weber window this is 1.17612x observed throughput and 1.38488x conservative modeled opportunity rate, with different-build/distribution and n=10 caveats. Every run found zero certificates. | Use X1(11) point-four with cap 16 and retain the observed-versus-modeled distinction in production reporting |
| External | Cloud capacity | RunPod console access and AWS CLI identity are confirmed, but RunPod API/SSH variables remain unproven. The bounded tagged AWS trial is terminated; `m8g.xlarge` was 2.04x slower than local and the 64-vCPU quota request remains pending. | Keep AWS compute off; benchmark `c8g.4xlarge` only after quota approval, with the existing tag/budget/hard-stop controls |
| External | Local Magma launcher | The documented Magma V2.29-1 launcher is healthy but outside `PATH`; a 2026-08-01 smoke test returned the correct order/trace for `p=97,a=2,b=3` | Set `MAGMA=/Users/agent/Documents/Codex/t24-search/private/magma-local/install/magma` explicitly for mandatory oracle gates |

## Resolved or bounded items

| Item | Resolution evidence |
|---|---|
| Full-cache smoothness memory | Commit `6538bc7`; real 5 GB cache extraction completed with exact known outputs, 5.55 GB maximum RSS, zero swaps |
| Certificate-incompatible curve generation | The exact j-to-Montgomery admission predicate rejects the old p125 index-0 curve before SEA. A `p=10093` census admitted 50.89%; a 32-index p125 probe needed 41 rejected attempts in 0.48 s, making every scheduled SEA curve assembly-eligible. |
| Implementation-limit cursor safety | `sea_level_limit` and `no_rational_weber_lift` now checkpoint/report without changing cursor or counters; the regression retries the same index with adequate levels and obtains a canonical certificate. |
| Early trace-cap policy | The general default remains 64: old Weber cap-195/4096 experiments were negative. On the new X1 trace-prior path, same-build caps 64/32/16/1 took 272.19/271.95/260.34/260.30 s for indices 12--21. Cap 16 cut trace screens 97 to 24 and concurrent curve work 1678.006 to 1435.541 s; cap 1's 0.04 s wall edge is noise and its 1451.021 s curve-work sum is 1.08% worse. Production explicitly selects 16. |
| SEA task fan-out | Modular-root jobs are capped by `--sea-threads` and the source-lift count; the serial and bounded-parallel paths are differentially tested and report actual per-level worker counts. |
| Cache trust/completeness boundary | Every pre-existing exact cache requires a trusted lowercase SHA-256; p125 cache digest is pinned in `data/smooth_cache/TRUSTED_MANIFEST.json` |
| Candidate divisor coverage | Exhaustive bounded DFS tries every admissible divisor while failing closed on candidate/node caps |
| Final acceptance | Native validation is followed by the unmodified pinned canonical verifier under an authenticated Python 3 runtime |
| First production Atkin constraints | Pinned classical `Phi_5`/`Phi_7` factor degrees are used only with monic, square-free, uniform-degree no-root evidence; Atkin state is separate from the exact unique-trace gate and the progress auditor independently reconstructs it |
| Weber table trust boundary | Production startup pins the checked-in manifest digest and verifies the complete table filename set, byte counts, and every SHA-256 before SEA; tests reject missing, extra, and altered tables |
| Weber 24th-root orbit reuse | Covariance is verified term-by-term from each loaded table; a controlled off/on ablation and a 64-level retained production replay have identical canonical mathematical projections |
| Conjugate Frobenius reuse | A same-binary off/on ablation and full production replay partition every validated kernel into independent/derived telemetry; 42- and 64-level canonical projections are identical, including scalar/non-scalar safety tests |
| Rolling shared-cache curve execution | One immutable exact-smooth engine is used concurrently; durable state remains ordered and lower-index implementation limits/certificates discard higher speculative reports. The resource setting is resumable and absent from semantic identity. |
| Exact family trace priors | Weber-f claims `t=p+1 (mod 4)` only after direct full-rational-`E[2]` validation. X1(11) seeds `t=+(p+1)` or `-(p+1)` modulo the selected group divisor 44/88, promoted to 176 for the proven point-four `p=5 mod 8` branch, and skips redundant `ell=11`. The policy is bound into the schedule digest and emitted per curve. |
| Generator-retained Weber source | Both curve generators retain the exact nonexceptional Weber-f point. Strict validation precedes singleton SEA; unknown-source callers remain exhaustive and pre-policy checkpoints are incompatible. The one-orbit p125 control was neutral, while the three-orbit index 17 reduced SEA 129.163 to 59.823 s and modular roots 100.457 to 33.537 s with every exact projection unchanged. |
| Reusable reciprocal reduction | Exact across 60 small cases, p125 degrees 64/129/194, and a full core gate, but 14.31% slower on degree-194 Frobenius, 6.94% slower at deterministic p125 level 277, and 21.20% slower in its eigenvalue stage. The prototype was fully reverted. |
| Deferred product normalization | The quotient reducer already normalizes every raw convolution coefficient at its pivot or final output. Removing the redundant pre-pass was exact across adversarial non-monic/negative/alias cases and improved paired p125 index-17 SEA by 1.03479x, modular roots by 1.03840x, and eigen recovery by 1.04723x. |
| X1(11) family throughput gate | Same-build indices 12--21 measured 1.03945x X1 point-four wall throughput over Weber (294.57 versus 306.19 s) with ten sound rejections and zero certificates per family. This clears the predefined 1.1775 modeled-opportunity penalty threshold without claiming observed yield. |
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
