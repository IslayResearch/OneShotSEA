#!/bin/sh
set -eu

# Required large authenticated inputs are intentionally not duplicated here.
REPO=${REPO:-"$(pwd)"}
BIN=${BIN:-"$REPO/build/oneshotsea"}
TABLE_DIR=${TABLE_DIR:-"$REPO/data/modpoly/weber_f"}
SMOOTH_CACHE=${SMOOTH_CACHE:-"$REPO/work/p125/smooth.cache"}
DIRECT_CACHE=${DIRECT_CACHE:-/private/tmp/p125-direct-low-5-59.ctx}
OUT=${OUT:-/private/tmp/oneshotsea-hybrid-reproduction}
CXX=${CXX:-c++}
GMP_PREFIX=${GMP_PREFIX:-/opt/homebrew/opt/gmp}

P=100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237
SMOOTH_SHA=afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551
DIRECT_SHA=b31c858c5398d7b284ad7b003ce4647211de8dec0412421f90ed226f2926ecfd
LEVELS=5,7,11,13,17,19,23,29,31,37,41,43,47,53,59

mkdir -p "$OUT"

"$BIN" search \
  --p "$P" --seed 202607300000 \
  --range-start 2000000 --range-end 2000001 \
  --worker-id 0 --worker-count 1 \
  --curve-family x1-27 --x1-require-point4 1 \
  --max-level 401 --trace-cap 64 --table-dir "$TABLE_DIR" \
  --smooth-cache "$SMOOTH_CACHE" --smooth-cache-sha256 "$SMOOTH_SHA" \
  --checkpoint "$OUT/weber.checkpoint.json" \
  --progress "$OUT/weber.progress.ndjson" \
  --certificate-out "$OUT/weber.certificate" \
  --curve-threads 1 --smooth-coordinators 1 --sea-threads 1 \
  --sea-level-telemetry 0 --max-curves 1 \
  --build-id local:8a9a083-reproduction

"$BIN" search \
  --p "$P" --seed 202607300000 \
  --range-start 2000000 --range-end 2000001 \
  --worker-id 0 --worker-count 1 \
  --curve-family x1-27 --x1-require-point4 1 \
  --max-level 401 --trace-cap 64 --table-dir "$TABLE_DIR" \
  --smooth-cache "$SMOOTH_CACHE" --smooth-cache-sha256 "$SMOOTH_SHA" \
  --checkpoint "$OUT/hybrid.checkpoint.json" \
  --progress "$OUT/hybrid.progress.ndjson" \
  --certificate-out "$OUT/hybrid.certificate" \
  --curve-threads 1 --smooth-coordinators 1 --sea-threads 1 \
  --sea-level-telemetry 0 --max-curves 1 \
  --sea-strategy direct-first \
  --classical-direct-levels "$LEVELS" \
  --classical-direct-max-prime-candidates 10000000 \
  --classical-direct-max-x-candidates 1000000 \
  --classical-direct-context-cache "$DIRECT_CACHE" \
  --classical-direct-context-sha256 "$DIRECT_SHA" \
  --classical-direct-cache-resident-bytes 1000000000 \
  --build-id local:8a9a083-reproduction

"$CXX" -std=c++20 -O3 -DNDEBUG \
  -I"$REPO/include" -I"$GMP_PREFIX/include" \
  "$REPO/artifacts/local/p125-direct-first-hybrid-20260803/verifier.cpp" \
  "$REPO/build/liboneshotsea.a" -L"$GMP_PREFIX/lib" -lgmpxx -lgmp \
  -o "$OUT/verify_p125_hybrid_trace"

"$OUT/verify_p125_hybrid_trace"

GP=${GP:-gp}
"$GP" -q -f -s 2000000000 \
  "$REPO/artifacts/local/p125-direct-first-hybrid-20260803/point_count.gp"
