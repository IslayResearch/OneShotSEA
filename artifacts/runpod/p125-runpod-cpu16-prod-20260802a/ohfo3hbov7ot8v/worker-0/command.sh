#!/usr/bin/env bash
set -uo pipefail
cd /workspace/OneShotSEA
started_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
started_epoch=$(date +%s)
printf '{"event":"start","utc":"%s","epoch":%s}
' "$started_utc" "$started_epoch" >>/workspace/OneShotSEA/runs/p125-runpod-cpu16-prod-20260802a/worker-0/attempts.jsonl
printf 'attempt_start utc=%s epoch=%s
' "$started_utc" "$started_epoch" >>/workspace/OneShotSEA/runs/p125-runpod-cpu16-prod-20260802a/worker-0/resource-usage.txt
set +e
/usr/bin/time -a -v -o /workspace/OneShotSEA/runs/p125-runpod-cpu16-prod-20260802a/worker-0/resource-usage.txt -- timeout --signal=TERM --kill-after=60 14400 /workspace/OneShotSEA/build/oneshotsea search --p 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 --seed 202607300000 --range-start 1000030 --range-end 1000000000 --worker-id 0 --worker-count 1 --max-level 401 --table-dir /workspace/OneShotSEA/data/modpoly/weber_f --smooth-cache /workspace/OneShotSEA/caches/p125.cache --smooth-cache-sha256 afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551 --checkpoint /workspace/OneShotSEA/runs/p125-runpod-cpu16-prod-20260802a/worker-0/checkpoint.json --progress /workspace/OneShotSEA/runs/p125-runpod-cpu16-prod-20260802a/worker-0/progress.jsonl --certificate-out /workspace/OneShotSEA/runs/p125-runpod-cpu16-prod-20260802a/worker-0/certificate.txt --build-id git:c08f0ed82923b7aee3a5a3c9326deb4d5e439b4c+binary-sha256:383207a8c0b1f0c05a26183e2073afbbf8fe6a7c771eac4a194077e2175aecaf --curve-family x1-27 --x1-require-point4 1 --curve-threads 16 --sea-level-telemetry 0 --schoof-fallback 1 --skip-incomplete-curves 0 --smooth-coordinators 0 --max-curves 0 --checkpoint-every 1 --trace-cap 16 --sea-threads 1 --smooth-threads 1 --smooth-max-batch 128 --smooth-root-auxiliary-bytes 134217728 --smooth-build-segment-span 4000000000 --assembly-attempts 400 --max-certificate-candidates 100000 --max-candidate-search-nodes 1000000 >>/workspace/OneShotSEA/runs/p125-runpod-cpu16-prod-20260802a/worker-0/worker.log 2>&1
status=$?
set -e
ended_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
ended_epoch=$(date +%s)
printf '{"event":"end","utc":"%s","epoch":%s,"status":%s}
' "$ended_utc" "$ended_epoch" "$status" >>/workspace/OneShotSEA/runs/p125-runpod-cpu16-prod-20260802a/worker-0/attempts.jsonl
exit "$status"
