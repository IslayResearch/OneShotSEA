#!/bin/zsh
set -e
cd '/Users/agent/Documents/ecpp reach'

vm_stat > /tmp/oneshotsea-curve-parallel-bench-20260801/vm-before-parallel.txt
/usr/bin/time -p -o /tmp/oneshotsea-curve-parallel-bench-20260801/parallel.time \
  '/Users/agent/Documents/ecpp reach/build/oneshotsea-karatsuba-eval-01d9cabac896' search \
  --p 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 \
  --seed 202607300000 \
  --range-start 8 --range-end 10 \
  --worker-id 0 --worker-count 1 \
  --max-level 401 --trace-cap 64 --curve-threads 2 --sea-threads 5 \
  --table-dir data/modpoly/weber_f \
  --smooth-cache work/p125/smooth.cache \
  --smooth-cache-sha256 afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551 \
  --smooth-threads 4 --smooth-max-batch 128 \
  --smooth-root-auxiliary-bytes 134217728 \
  --checkpoint /tmp/oneshotsea-curve-parallel-bench-20260801/parallel/checkpoint.json \
  --checkpoint-every 1 \
  --progress /tmp/oneshotsea-curve-parallel-bench-20260801/parallel/progress.jsonl \
  --certificate-out /tmp/oneshotsea-curve-parallel-bench-20260801/parallel/certificate.txt \
  --build-id bench:worktree-karatsuba+binary-sha256:01d9cabac89660094545c7cd42b83b1c6d06d74b5c979aec3d52ea14a01eee0e \
  --max-curves 2 \
  > /tmp/oneshotsea-curve-parallel-bench-20260801/parallel.jsonl \
  2> /tmp/oneshotsea-curve-parallel-bench-20260801/parallel.stderr

vm_stat > /tmp/oneshotsea-curve-parallel-bench-20260801/vm-after-parallel.txt
