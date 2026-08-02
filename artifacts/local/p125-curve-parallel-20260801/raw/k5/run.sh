#!/bin/zsh
set -e
cd '/Users/agent/Documents/ecpp reach'
vm_stat > /tmp/oneshotsea-k5-bench-20260801/vm-before-k5.txt
/usr/bin/time -p -o /tmp/oneshotsea-k5-bench-20260801/k5.time \
  '/Users/agent/Documents/ecpp reach/build/oneshotsea-karatsuba-eval-01d9cabac896' search \
  --p 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 \
  --seed 202607300000 \
  --range-start 8 --range-end 13 --worker-id 0 --worker-count 1 \
  --max-level 401 --trace-cap 64 --curve-threads 5 --sea-threads 2 \
  --table-dir data/modpoly/weber_f \
  --smooth-cache work/p125/smooth.cache \
  --smooth-cache-sha256 afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551 \
  --smooth-threads 1 --smooth-max-batch 128 \
  --smooth-root-auxiliary-bytes 134217728 \
  --checkpoint /tmp/oneshotsea-k5-bench-20260801/k5/checkpoint.json \
  --checkpoint-every 1 \
  --progress /tmp/oneshotsea-k5-bench-20260801/k5/progress.jsonl \
  --certificate-out /tmp/oneshotsea-k5-bench-20260801/k5/certificate.txt \
  --build-id bench:worktree-karatsuba+binary-sha256:01d9cabac89660094545c7cd42b83b1c6d06d74b5c979aec3d52ea14a01eee0e \
  --max-curves 5 \
  > /tmp/oneshotsea-k5-bench-20260801/k5.jsonl \
  2> /tmp/oneshotsea-k5-bench-20260801/k5.stderr
vm_stat > /tmp/oneshotsea-k5-bench-20260801/vm-after-k5.txt
