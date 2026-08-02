#!/bin/zsh
set -e
cd '/Users/agent/Documents/ecpp reach'

/usr/bin/time -p -o /tmp/oneshotsea-k3-bench-20260801/serial10.time \
  '/Users/agent/Documents/ecpp reach/build/oneshotsea-karatsuba-eval-01d9cabac896' search \
  --p 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 \
  --seed 202607300000 \
  --range-start 10 --range-end 11 --worker-id 0 --worker-count 1 \
  --max-level 401 --trace-cap 64 --curve-threads 1 --sea-threads 10 \
  --table-dir data/modpoly/weber_f \
  --smooth-cache work/p125/smooth.cache \
  --smooth-cache-sha256 afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551 \
  --smooth-threads 8 --smooth-max-batch 128 \
  --smooth-root-auxiliary-bytes 134217728 \
  --checkpoint /tmp/oneshotsea-k3-bench-20260801/serial10/checkpoint.json \
  --checkpoint-every 1 \
  --progress /tmp/oneshotsea-k3-bench-20260801/serial10/progress.jsonl \
  --certificate-out /tmp/oneshotsea-k3-bench-20260801/serial10/certificate.txt \
  --build-id bench:worktree-karatsuba+binary-sha256:01d9cabac89660094545c7cd42b83b1c6d06d74b5c979aec3d52ea14a01eee0e \
  --max-curves 1 \
  > /tmp/oneshotsea-k3-bench-20260801/serial10.jsonl \
  2> /tmp/oneshotsea-k3-bench-20260801/serial10.stderr

vm_stat > /tmp/oneshotsea-k3-bench-20260801/vm-before-k3.txt
/usr/bin/time -p -o /tmp/oneshotsea-k3-bench-20260801/k3.time \
  '/Users/agent/Documents/ecpp reach/build/oneshotsea-karatsuba-eval-01d9cabac896' search \
  --p 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 \
  --seed 202607300000 \
  --range-start 8 --range-end 11 --worker-id 0 --worker-count 1 \
  --max-level 401 --trace-cap 64 --curve-threads 3 --sea-threads 3 \
  --table-dir data/modpoly/weber_f \
  --smooth-cache work/p125/smooth.cache \
  --smooth-cache-sha256 afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551 \
  --smooth-threads 2 --smooth-max-batch 128 \
  --smooth-root-auxiliary-bytes 134217728 \
  --checkpoint /tmp/oneshotsea-k3-bench-20260801/k3/checkpoint.json \
  --checkpoint-every 1 \
  --progress /tmp/oneshotsea-k3-bench-20260801/k3/progress.jsonl \
  --certificate-out /tmp/oneshotsea-k3-bench-20260801/k3/certificate.txt \
  --build-id bench:worktree-karatsuba+binary-sha256:01d9cabac89660094545c7cd42b83b1c6d06d74b5c979aec3d52ea14a01eee0e \
  --max-curves 3 \
  > /tmp/oneshotsea-k3-bench-20260801/k3.jsonl \
  2> /tmp/oneshotsea-k3-bench-20260801/k3.stderr
vm_stat > /tmp/oneshotsea-k3-bench-20260801/vm-after-k3.txt
