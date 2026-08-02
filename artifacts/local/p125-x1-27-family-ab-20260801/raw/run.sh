#!/bin/zsh
set -o pipefail
set -u

family="$1"
label="$2"
case "$family" in
  x1-11|x1-27) ;;
  *) print -u2 "unsupported family"; exit 2 ;;
esac

cd '/Users/agent/Documents/ecpp reach'
output_directory="/private/tmp/oneshotsea-x127-ab-07bb/$label"
mkdir -p "$output_directory"

expected_binary_sha256=d4d839b889fc4f4cf50d70e5a17743c6f34b11ffbf29d1f5804026f382394fac
actual_binary_sha256="$(shasum -a 256 work/oneshotsea-07bbda3 | awk '{print $1}')"
if [[ "$actual_binary_sha256" != "$expected_binary_sha256" ]]; then
  print -u2 "benchmark binary digest mismatch"
  exit 1
fi

/usr/bin/time -l work/oneshotsea-07bbda3 search \
  --p 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 \
  --seed 202607300000 \
  --range-start 0 --range-end 10 \
  --worker-id 0 --worker-count 1 \
  --curve-family "$family" --x1-require-point4 1 \
  --max-level 401 --trace-cap 16 \
  --skip-incomplete-curves 1 \
  --curve-threads 10 --sea-threads 1 \
  --sea-level-telemetry 0 \
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
  --max-curves 10 \
  --build-id git:07bbda3333ed88297d8a5a3a15650584e8956070+binary-sha256:d4d839b889fc4f4cf50d70e5a17743c6f34b11ffbf29d1f5804026f382394fac \
  2>&1 | tee -a "$output_directory/search.log"
