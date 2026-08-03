#!/usr/bin/env bash
set -uo pipefail
cd /workspace/OneShotSEA-550815e
started_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
started_epoch=$(date +%s)
printf '{"event":"start","utc":"%s","epoch":%s}
' "$started_utc" "$started_epoch" >>/workspace/OneShotSEA/runs/p125-topology-bx-550815e-20260803a/worker-1/attempts.jsonl
printf 'attempt_start utc=%s epoch=%s
' "$started_utc" "$started_epoch" >>/workspace/OneShotSEA/runs/p125-topology-bx-550815e-20260803a/worker-1/resource-usage.txt
set +e
/usr/bin/time -a -v -o /workspace/OneShotSEA/runs/p125-topology-bx-550815e-20260803a/worker-1/resource-usage.txt -- timeout --signal=TERM --kill-after=60 2400 /workspace/OneShotSEA-550815e/build/oneshotsea search --p 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 --seed 202607300000 --range-start 1000827 --range-end 1000891 --worker-id 1 --worker-count 2 --max-level 401 --table-dir /workspace/OneShotSEA-550815e/data/modpoly/weber_f --smooth-cache /workspace/OneShotSEA/caches/p125.cache --smooth-cache-sha256 afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551 --checkpoint /workspace/OneShotSEA/runs/p125-topology-bx-550815e-20260803a/worker-1/checkpoint.json --progress /workspace/OneShotSEA/runs/p125-topology-bx-550815e-20260803a/worker-1/progress.jsonl --certificate-out /workspace/OneShotSEA/runs/p125-topology-bx-550815e-20260803a/worker-1/certificate.txt --build-id git:550815e0013361f5eee4cdb6b044f5cec1a9ae2c+binary-sha256:550c38acebb0407de4fc1021d905796798f9f18534c341a8a5b5238d34259737 --curve-family x1-27 --x1-require-point4 1 --curve-threads 8 --sea-level-telemetry 0 --schoof-fallback 1 --skip-incomplete-curves 0 --smooth-coordinators 0 --max-curves 32 --checkpoint-every 1 --trace-cap 16 --sea-threads 1 --smooth-threads 1 --smooth-max-batch 128 --smooth-root-auxiliary-bytes 134217728 --smooth-build-segment-span 4000000000 --assembly-attempts 400 --max-certificate-candidates 100000 --max-candidate-search-nodes 1000000 >>/workspace/OneShotSEA/runs/p125-topology-bx-550815e-20260803a/worker-1/worker.log 2>&1
status=$?
set -e
ended_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
ended_epoch=$(date +%s)
printf '{"event":"end","utc":"%s","epoch":%s,"status":%s}
' "$ended_utc" "$ended_epoch" "$status" >>/workspace/OneShotSEA/runs/p125-topology-bx-550815e-20260803a/worker-1/attempts.jsonl
exit "$status"
