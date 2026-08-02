#!/bin/zsh
set -euo pipefail

if [[ $# -ne 3 ]]; then
  print -u2 'usage: run-one.sh LABEL BINARY OUTPUT_DIRECTORY'
  exit 2
fi

label=$1
binary=$2
output_directory=$3

if [[ -e "$output_directory" ]]; then
  print -u2 "refusing existing output directory: $output_directory"
  exit 2
fi
mkdir -p "$output_directory"

cd '/Users/agent/Documents/ecpp reach'

/usr/bin/time -p "$binary" search \
  --p 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 \
  --seed 202607300000 \
  --range-start 435 --range-end 445 \
  --worker-id 0 --worker-count 1 \
  --curve-family x1-27 --x1-require-point4 1 \
  --max-level 401 --trace-cap 16 \
  --schoof-fallback 1 --skip-incomplete-curves 0 \
  --curve-threads 10 --sea-threads 1 --sea-level-telemetry 0 \
  --table-dir data/modpoly/weber_f \
  --smooth-cache work/p125/smooth.cache \
  --smooth-cache-sha256 afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551 \
  --smooth-threads 1 --smooth-max-batch 128 \
  --smooth-root-auxiliary-bytes 134217728 \
  --smooth-build-segment-span 500000000 \
  --checkpoint "$output_directory/checkpoint.json" \
  --checkpoint-every 1 \
  --progress "$output_directory/progress.ndjson" \
  --certificate-out "$output_directory/certificate.txt" \
  --assembly-attempts 400 \
  --max-certificate-candidates 100000 \
  --max-candidate-search-nodes 1000000 \
  --build-id "smooth-batch-ab:$label" \
  >"$output_directory/stdout.log" \
  2>"$output_directory/time.txt"
