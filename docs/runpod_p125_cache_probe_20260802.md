# RunPod p125 cache build and production-path probe, 2026-08-02

This record covers the authenticated `p125` exact-smooth cache build and the
first bounded production-path probe on RunPod CPU pod `ohfo3hbov7ot8v`.  The
cache was reproduced exactly.  The probe exhausted its assigned 30-curve range
without a certificate.  The first nominal production launch from adjacent
index `1000030` exposed and retained a zero-curve launcher-contract bug before
performing search work; the launchers now reject that configuration.

## Worker identity

- RunPod workload: `cpu3g`, 16 vCPU, 64 GB RAM.
- Host CPU: AMD EPYC 7702P; the container cpuset contains 16 logical CPUs.
- Recorded compute plus disk rate: `$0.643/hour` (`$0.640 + $0.003`).
- Deployment commit: `c08f0ed82923b7aee3a5a3c9326deb4d5e439b4c`.
- Production binary SHA-256:
  `383207a8c0b1f0c05a26183e2073afbbf8fe6a7c771eac4a194077e2175aecaf`.
- Product-forest source SHA-256:
  `3a3f11049af21e6f5fdc7fc6c6f89740c0bc59130ee5d586a1e66811b8e509aa`.
- Compiler: GCC 11.5.0.

The retained environment records include the exact cpuset, kernel, CPU flags,
compiler, commit, binary digest, and timestamps.  No credential or RunPod API
key is present in an artifact.

## Exact-smooth cache

The repository trust manifest fixes the 416-bit cache at 1,297,866,953 primes,
5,400,759,974 product bytes plus the 64-byte cache header, and SHA-256
`afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551`.
Every attempt used an empty deterministic worker assignment `[1,1)`, so cache
construction could not process a curve or emit a certificate.

| Artifact | Commit / implementation | Segment span | Outcome | Resource evidence |
|---|---|---:|---|---|
| `p125-runpod-cpu16-20260802a` | `93da28b`, sequential accumulator | 500,000,000 | timeout, exit 124 after 15:00; no cache output | outer timeout lost GNU-time evidence |
| `p125-runpod-cpu16-span4g-20260802b` | `93da28b`, sequential accumulator | 4,000,000,000 | explicitly stopped, exit 143 after 26:25.16; no cache output | 2,993.55 user s, 173.77 system s, 199% CPU, 26,507,584 KiB peak RSS, zero file output |
| `p125-runpod-cpu16-product-forest-20260802c` | `c08f0ed`, binary-carry product forest | 4,000,000,000 | timeout, exit 124 after 30:00; no cache output | outer timeout lost GNU-time evidence; sampled peak reached about 29.7 GB |
| `p125-runpod-cpu16-product-forest-20260802d` | `c08f0ed`, binary-carry product forest | 4,000,000,000 | success, exit 0 | 35:56.59 wall, 4,145.58 user s, 244.61 system s, 203% CPU, 29,671,612 KiB peak RSS, zero swaps |

The successful run started at `2026-08-02T17:44:08Z` and ended at
`2026-08-02T18:20:05Z`.  It wrote exactly 5,400,760,038 bytes.  The wrapper
hashed the temporary output, compared it with the independently trusted digest,
and only then renamed it to `/workspace/OneShotSEA/caches/p125.cache`.
An independent remote full-file hash reproduced the same digest.  The small
build artifact was then fetched locally, where every entry in `SHA256SUMS`, the
zero exit, manifest bindings, empty range, size, and digest were checked again.

The product forest has a retained 4.381x local p61 cache-construction
microbenchmark, but these remote attempts are not a completed same-input
sequential/product-forest A/B.  In particular, the optimized run did not lower
the absolute final-merge peak below the stopped sequential attempt.  No remote
speedup or memory reduction is claimed from this series.

Successful evidence is in
[`artifacts/runpod/p125-runpod-cpu16-product-forest-20260802d`](../artifacts/runpod/p125-runpod-cpu16-product-forest-20260802d/).
The three failed artifacts are retained alongside it with explicit nonzero
exits and no cache file.

## Thirty-curve production-path probe

The hardened RunPod launcher created benchmark run
`p125-runpod-cpu16-probe-20260802b` with the exact half-open range
`[1000000,1000030)`, seed `202607300000`, and a 1,800-second wall cap.  Its
manifest binds the complete command, range, deployment commit, binary and cache
digests, and command SHA-256.  The important resource/search controls were:

```text
curve_family=x1-27  x1_require_point4=1  trace_cap=16
curve_threads=16    sea_threads=1         smooth_threads=1
smooth_coordinators=0                    smooth_max_batch=128
schoof_fallback=1   skip_incomplete_curves=0
```

The run started at `2026-08-02T18:23:12Z` and ended at
`2026-08-02T18:36:33Z` with wrapper and search exit zero.

| Measurement | Result |
|---|---:|
| Curves attempted | 30 |
| Sound smoothness rejections | 30 |
| Heuristic rejections | 0 |
| Full point counts | 14 |
| Candidates reaching smoothness | 30 |
| Candidate attempts / assembly calls | 0 / 0 |
| Certificates | 0 |
| SEA levels / exact / Atkin | 1,733 / 907 / 29 |
| Schoof fallback levels | 0 |
| Wall / user / system | 800.92 / 7,150.64 / 19.19 s |
| Average CPU | 895% |
| Peak RSS | 10,552,336 KiB (10,805,592,064 bytes) |
| Swaps | 0 |

The local audit verified all 30 records, contiguous indices `1000000` through
`1000029`, monotone final cursor `1000030`, exact checkpoint counters, immutable
command SHA, attempt status, summary, and all fetched checksums.  There is no
certificate file.  The full evidence and derived checked summary are in
[`artifacts/runpod/p125-runpod-cpu16-probe-20260802b/ohfo3hbov7ot8v`](../artifacts/runpod/p125-runpod-cpu16-probe-20260802b/ohfo3hbov7ot8v/).

Including cache authentication/loading and the short-run tail, the probe
measured 26.7 elapsed seconds per curve, 134.83 curves/hour, and an estimated
`$0.1431` of worker time.  At that observed rate, the separately checked
optimistic X1(27) cyclic-divisor smooth-order probability
`0.000010446455027346424` implies about 95,726 curves, 710 hours, and `$456.51`.
This is deliberately not called a certificate-cost estimate:
the model is optimistic and exact-order assembly or curve/twist dependence can
lower realized yield.

## Capped production continuation

Run `p125-runpod-cpu16-prod-20260802a` started at
`2026-08-02T18:40:07Z` with range `[1000030,1000000000)`, a 14,400-second wall
cap, and `--max-curves 0`.  The launcher had documented zero as “unlimited,”
but the production search intentionally defines zero as “process no curves.”
After cache authentication the run exited zero at `18:42:12Z`, reporting
`processed=0`, `range_exhausted=false`, no progress/checkpoint/certificate, and
unchanged cursor `1000030`.  The fetched no-op artifact is retained rather than
presented as search evidence at
[`artifacts/runpod/p125-runpod-cpu16-prod-20260802a/ohfo3hbov7ot8v`](../artifacts/runpod/p125-runpod-cpu16-prod-20260802a/ohfo3hbov7ot8v/).

Both cloud launchers and the AWS remote worker now require a positive
per-attempt curve cap independently of the wall timeout.  Regression tests pass
`max-curves=00` together with a positive wall cap and require fail-closed
rejection.  The replacement production run must use the full positive assigned
range count `998999970`; the separate 14,400-second timeout remains the
effective spend boundary.  At the probe rate that cap projects about 539 curves
and `$2.57`, or 0.56% of the optimistic expected-curve scale.

Replacement run `p125-runpod-cpu16-prod-20260802b` deployed commit `ffedd29`
over `[1000030,1000000000)` with that positive cap.  At `2026-08-02T18:56:44Z`
the live worker had durably committed indices `1000030` and `1000031`, advanced
its authenticated checkpoint to `1000032`, and was using about nine CPU cores.
This proves the corrected launch escaped the zero-work path.  The run remains
subject to the same candidate-or-four-hour fetch boundary; a mutable live
snapshot is not presented as a completed run artifact.

AWS has no active resources and incurred no cost for this work.  The retained
four-core AWS benchmark was much slower than both the local host and this
RunPod worker; AWS therefore remains an isolation fallback, not the production
throughput choice.
