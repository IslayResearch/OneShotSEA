# p125 RunPod topology gate

This artifact records the predeclared `B_X -> A_X -> A_Y -> B_Y` bracket for
the immutable `550815e` production binary.  `A` is one process with 16 curve
threads; `B` is two concurrently launched processes with 8 curve threads each.
Every leg used the same p125 target, seed, X1(27) point-four family, SEA and
smoothness options, authenticated tables, 5.4 GB smooth cache, and 64-curve
range.  X and Y are adjacent, disjoint ranges.

The hardened audit accepted the dual-process topology:

- pair X: `1158.62 / 1020.39 = 1.1354678111x`;
- pair Y: `1204.37 / 1145.04 = 1.0518147837x`;
- geometric-mean speedup: `1.0928411733x`;
- dual peak-RSS sums: 20.126 GiB and 20.125 GiB, below the strict 48 GiB gate;
- no swaps, nonzero exit statuses, heuristic skips, or certificates;
- exact semantic equality within each pair; and
- strict four-leg chronology with 7-second and 6-second dual launch skews.

The four effective leg intervals total 4,528.42 seconds.  At the recorded
`$0.643/hour` pod rate, their compute-time estimate is `$0.808826`; this is not
an invoice and excludes gaps between legs.  `environment.txt` records the pod,
host, toolchain, binary, cache, and table identities.  `table-MANIFEST.json` is
the exact deployment manifest.  `build-provenance.json` is checked against the
retained binary's SHA-256 and embedded DWARF producer.

Recompute the decision from a clean checkout with:

```sh
python3 tools/audit_p125_topology.py \
  artifacts/runpod/p125-topology-bx-550815e-20260803a/ohfo3hbov7ot8v \
  artifacts/runpod/p125-topology-ax-550815e-20260803a/ohfo3hbov7ot8v \
  artifacts/runpod/p125-topology-ay-550815e-20260803a/ohfo3hbov7ot8v \
  artifacts/runpod/p125-topology-by-550815e-20260803a/ohfo3hbov7ot8v \
  --build-provenance artifacts/runpod/p125-topology-gate-550815e-20260803a/build-provenance.json \
  --binary artifacts/runpod/p125-runpod-cpu16-replay-550815e-20260802a/ohfo3hbov7ot8v/binaries/candidate.bin \
  --source-repo . \
  --result artifacts/runpod/p125-topology-gate-550815e-20260803a/result.json
```

For the next four-hour production epoch, the conservative planning rate is the
lower retained dual rate, 201.215678 curves/hour.  A 90%-capacity fixed epoch
therefore assigns 724 curves total, 362 per worker, over global range
`[1000827,1001551)`.  The hard RunPod search bound is `$2.572` plus a
15-minute fetch/stop allowance of `$0.16075`, for an operational ceiling of
`$2.73275`.
