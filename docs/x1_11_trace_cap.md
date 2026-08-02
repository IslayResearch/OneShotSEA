# p125 X1(11) trace-cap ablation

This note records a bounded trace-cap ablation for the p125 X1(11)
point-four search with exact family trace priors.  Four runs used the same
committed binary, deterministic curves at indices `[12,22)`, host, cache,
tables, verifier, and resource configuration.  Only `trace_cap` and its bound
schedule digest changed.  The compact record is
[`artifacts/local/p125-x1-11-trace-cap-20260801/result.json`](../artifacts/local/p125-x1-11-trace-cap-20260801/result.json).

The raw files remain in four host-local `/tmp` directories pinned by full
paths, byte sizes, line counts, SHA-256 hashes, and checkpoint CRC64 values in
the artifact.  Every run emitted commit `8c1605f0fd702260528915bde3ef52c8043acd4c`
and binary SHA-256
`feb9c06aa0303c699b4c6c9f777b53e0936b2e64b6a6908faa2ee0fc723288aa`.

## Result

| cap | real | user | sys | wall s/curve | wall curves/s | summed curve time | summed SEA | summed smoothness | SEA levels | retained trace candidates | full counts | peak RSS |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 64 | 272.19 s | 1727.19 s | 5.01 s | 27.219 | 0.0367390 | 1678.006 s | 1186.396 s | 469.073 s | 581 | 97 | 5/10 | 6.202 GiB |
| 32 | 271.95 s | 1726.24 s | 5.27 s | 27.195 | 0.0367715 | 1681.849 s | 1188.911 s | 469.760 s | 581 | 97 | 5/10 | 5.722 GiB |
| **16** | **260.34 s** | **1485.25 s** | **5.01 s** | **26.034** | **0.0384113** | **1435.541 s** | **1190.361 s** | **221.979 s** | **584** | **24** | **8/10** | **5.482 GiB** |
| 1 | 260.30 s | 1500.67 s | 4.83 s | 26.030 | 0.0384172 | 1451.021 s | 1255.524 s | 172.245 s | 595 | 10 | 10/10 | 5.520 GiB |

Cap 16 is the measured policy.  Relative to cap 64, it completed the wave
11.85 seconds sooner, giving **1.04552x wall throughput**.  It reduced retained
trace candidates from 97 to 24 and summed concurrent curve time from
1678.006 to 1435.541 seconds, a 14.45% work reduction.  It spent only three
additional SEA levels while cutting summed smoothness time by 247.094 seconds.

Cap 32 is a no-op on this sample.  It has exactly the same per-index stopping
points, trace counts, full-count flags, exact traces, SEA level counts, and
terminal outcomes as cap 64.  All 581 shared SEA classification/residue
records also match.  Its 0.24-second wall difference is noise-scale.

Cap 1 forces all ten curves to a unique trace.  Its wall time is only 0.04
seconds below cap 16, a 0.015% difference that is not meaningful in a single
260-second observation.  At the same time, it requires 11 more SEA levels and
65.163 seconds more summed SEA work.  Although it saves 49.734 seconds of
smoothness work, its summed curve time is 15.480 seconds, or 1.08%, worse than
cap 16.  That makes cap 16 the better measured balance.

## Soundness and pairing audit

All four runs recorded the same generator-rejection vector and the same signed
mod-88 trace priors.  Whenever two runs emitted an exact trace for an index,
the signed values were identical; all 28 emitted trace records also satisfy
their prior modulo 88.  The Atkin-level vector is identical across all runs.
Every curve in every run reached exact smoothness screening and ended in
`sound_smoothness_reject`.  There were 40 sound rejections and zero
certificates.

The retained-candidate total is the sum of the progress field
`initial_trace_count`: it counts trace candidates presented to exact
smoothness screening.  Each trace induces the curve/twist order pair, so it
should not be confused with a count of individual order values.

## Fixed-wave versus production throughput

Each benchmark range contains ten curves and uses `curve_threads=10`, so it is
one fixed initial wave.  Once a fast curve finishes, there is no eleventh curve
to refill its slot.  Whole-wave wall time therefore reflects the overlapping
tail and does not directly measure a long rolling queue.

Production continuously assigns a new curve to each freed slot.  In that
regime, per-curve work and slot occupancy matter in addition to a fixed wave's
wall maximum.  Cap 16's 1.08% lower summed curve time than cap 1 is therefore a
useful selection signal, while the observed 0.04-second fixed-wave wall edge
for cap 1 is not.  This remains a ten-curve sample without a confidence
interval; a longer rolling replay would be needed to quantify steady-state
throughput precisely.

Finally, `/usr/bin/time -l` ended every log with
`sysctl kern.clockrate: Operation not permitted` after printing real, user,
and sys times.  The searches themselves completed: each log has a complete
summary and each checkpoint ends at index 22.  Peak RSS above comes from the
search progress records, not unavailable extended timing counters.
