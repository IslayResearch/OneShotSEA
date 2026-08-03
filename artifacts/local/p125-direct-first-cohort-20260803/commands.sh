#!/bin/sh
set -eu

# Large authenticated inputs are deliberately external to this evidence bundle.
REPO=${REPO:-"$(pwd)"}
BIN=${BIN:-"$REPO/build/oneshotsea"}
PROFILER=${PROFILER:-"$REPO/build/profile_classical_direct_cohort"}
TABLE_DIR=${TABLE_DIR:-"$REPO/data/modpoly/weber_f"}
SMOOTH_CACHE=${SMOOTH_CACHE:-"$REPO/work/p125/smooth.cache"}
LOW_CACHE=${LOW_CACHE:-/private/tmp/p125-direct-low-5-59.ctx}
MID_CACHE=${MID_CACHE:-/private/tmp/p125-direct-mid-61-97.ctx}
SELECTED_CACHE=${SELECTED_CACHE:-/private/tmp/p125-direct-selected-20.ctx}
OUT=${OUT:-/private/tmp/oneshotsea-p125-direct-first-cohort-replay}
CXX=${CXX:-c++}
GP=${GP:-gp}
GMP_PREFIX=${GMP_PREFIX:-/opt/homebrew/opt/gmp}

P=100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237
SEED=202607300000
SMOOTH_SHA=afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551
LOW_SHA=b31c858c5398d7b284ad7b003ce4647211de8dec0412421f90ed226f2926ecfd
MID_SHA=b4543589450c429516c95c5de575be853b6d5e7b5a4cdaabfdcf119a4b15d450
SELECTED_SHA=d9848275c04d77c5a40f96eb06f113100ebc7a1b3ac0bc6c15d207677be41a53
LOW_LEVELS=7,5,11,13,19,17,23,29,31,37,41,43,47,53,59
SELECTED_LEVELS=7,5,11,13,19,17,23,29,31,37,41,43,47,53,67,71,79,61,73,59

mkdir -p "$OUT"

# Rebuilding these curve-independent caches is optional and takes minutes.
if [ "${REBUILD_CACHES:-0}" = 1 ]; then
  "$BIN" prepare-classical-direct-context \
    --p "$P" --classical-direct-levels "$LOW_LEVELS" \
    --classical-direct-max-prime-candidates 10000000 \
    --classical-direct-max-x-candidates 1000000 --sea-threads 1 \
    --classical-direct-context-max-file-bytes 1000000000 \
    --output "$LOW_CACHE"
  "$BIN" prepare-classical-direct-context \
    --p "$P" --classical-direct-levels 61,67,71,73,79,83,89,97 \
    --classical-direct-max-prime-candidates 10000000 \
    --classical-direct-max-x-candidates 1000000 --sea-threads 1 \
    --classical-direct-context-max-file-bytes 1000000000 \
    --output "$MID_CACHE"
  "$BIN" prepare-classical-direct-context \
    --p "$P" --classical-direct-levels "$SELECTED_LEVELS" \
    --classical-direct-max-prime-candidates 10000000 \
    --classical-direct-max-x-candidates 1000000 --sea-threads 1 \
    --classical-direct-context-max-file-bytes 1000000000 \
    --output "$SELECTED_CACHE"
fi

run_search() {
  prefix=$1
  cap=$2
  start=$3
  end=$4
  build_id=$5
  strategy=$6
  levels=$7
  cache=$8
  digest=$9
  set -- "$BIN" search \
    --p "$P" --seed "$SEED" --range-start "$start" --range-end "$end" \
    --worker-id 0 --worker-count 1 \
    --curve-family x1-27 --x1-require-point4 1 \
    --max-level 401 --trace-cap "$cap" --table-dir "$TABLE_DIR" \
    --smooth-cache "$SMOOTH_CACHE" --smooth-cache-sha256 "$SMOOTH_SHA" \
    --checkpoint "$OUT/$prefix.checkpoint.json" \
    --progress "$OUT/$prefix.progress.ndjson" \
    --certificate-out "$OUT/$prefix.certificate" \
    --curve-threads 1 --smooth-coordinators 1 --sea-threads 1 \
    --sea-level-telemetry 0 --max-curves 4 --build-id "$build_id"
  if [ "$strategy" = direct-first ]; then
    set -- "$@" --sea-strategy direct-first \
      --classical-direct-levels "$levels" \
      --classical-direct-max-prime-candidates 10000000 \
      --classical-direct-max-x-candidates 1000000 \
      --classical-direct-context-cache "$cache" \
      --classical-direct-context-sha256 "$digest" \
      --classical-direct-context-max-file-bytes 1000000000 \
      --classical-direct-cache-resident-bytes 1000000000
  fi
  "$@"
}

# Matched cap-64 cohort. Do not infer a total-time A/B from this ordering: the
# smooth cache is warmer in the direct-first arm.
run_search weber 64 2000001 2000005 \
  local:8a9a083-cohort-weber weber-first none none none
run_search hybrid 64 2000001 2000005 \
  local:8a9a083-cohort-hybrid direct-first \
  "$LOW_LEVELS" "$LOW_CACHE" "$LOW_SHA"

# Production-policy cap-16 coverage runs.
run_search weber-cap16 16 2000001 2000005 \
  local:8a9a083-cohort-weber-cap16 weber-first none none none
run_search hybrid-cap16 16 2000001 2000005 \
  local:8a9a083-cohort-hybrid-cap16 direct-first \
  "$LOW_LEVELS" "$LOW_CACHE" "$LOW_SHA"
run_search selected20-cap16 16 2000001 2000005 \
  local:8a9a083-cohort-selected20-cap16 direct-first \
  "$SELECTED_LEVELS" "$SELECTED_CACHE" "$SELECTED_SHA"

# The second and third rows of the same-curve thermal bracket. Selected A is
# index 2,000,003 in selected20-cap16 above.
run_search paired-a-low 16 2000003 2000004 \
  local:8a9a083-paired-a-low direct-first \
  "$LOW_LEVELS" "$LOW_CACHE" "$LOW_SHA"
run_search paired-b-selected 16 2000003 2000004 \
  local:8a9a083-paired-b-selected direct-first \
  "$SELECTED_LEVELS" "$SELECTED_CACHE" "$SELECTED_SHA"

# Reproduce the 16-curve mid-level schedule profile.
"$PROFILER" --p "$P" --seed "$SEED" --range-start 2000000 --count 16 \
  --threads 1 --require-point4 1 --cache "$MID_CACHE" \
  --cache-sha256 "$MID_SHA" --cache-resident-bytes 1000000000 \
  --maximum-prime-candidates 10000000 --maximum-x-candidates 1000000 \
  61 67 71 73 79 83 89 97 >"$OUT/mid8-profile.ndjson"

# Reconstruct the exact models, enumerate the complete selected-20 sets, and
# independently count the same models with PARI/GP.
"$CXX" -std=c++20 -O3 -DNDEBUG -I"$REPO/include" \
  -I"$GMP_PREFIX/include" \
  "$REPO/artifacts/local/p125-direct-first-cohort-20260803/print_curves.cpp" \
  "$REPO/build/liboneshotsea.a" -L"$GMP_PREFIX/lib" -lgmpxx -lgmp \
  -o "$OUT/print_curves"
"$OUT/print_curves" >"$OUT/generated-curves.txt"
cmp "$OUT/generated-curves.txt" \
  "$REPO/artifacts/local/p125-direct-first-cohort-20260803/raw/generated-curves.txt"

"$CXX" -std=c++20 -O3 -DNDEBUG -I"$REPO/include" \
  -I"$GMP_PREFIX/include" \
  "$REPO/artifacts/local/p125-direct-first-cohort-20260803/verify_candidates.cpp" \
  "$REPO/build/liboneshotsea.a" -L"$GMP_PREFIX/lib" -lgmpxx -lgmp \
  -o "$OUT/verify_candidates"
"$OUT/verify_candidates" "$TABLE_DIR" "$SELECTED_CACHE" \
  >"$OUT/selected20-candidates.txt"
cmp "$OUT/selected20-candidates.txt" \
  "$REPO/artifacts/local/p125-direct-first-cohort-20260803/raw/selected20-candidates.txt"

"$GP" -q -f -s 2000000000 \
  "$REPO/artifacts/local/p125-direct-first-cohort-20260803/point_count.gp" \
  >"$OUT/pari-point-counts.txt" 2>&1
if grep -q '\*\*\*' "$OUT/pari-point-counts.txt"; then
  echo "PARI replay emitted an error" >&2
  exit 1
fi

python3 "$REPO/artifacts/local/p125-direct-first-cohort-20260803/audit.py"
