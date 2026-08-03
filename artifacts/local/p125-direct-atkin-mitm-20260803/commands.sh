#!/bin/sh
set -eu

prime=100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237
cache=/private/tmp/p125-direct-low-5-59.ctx
digest=b31c858c5398d7b284ad7b003ce4647211de8dec0412421f90ed226f2926ecfd

# The retained cache was prepared once with this schedule and these caps.
./build/oneshotsea prepare-classical-direct-context \
  --p "$prime" \
  --classical-direct-levels 5,7,11,13,17,19,23,29,31,37,41,43,47,53,59 \
  --classical-direct-max-prime-candidates 10000000 \
  --classical-direct-max-x-candidates 1000000 \
  --sea-threads 1 \
  --output "$cache"

# Confirm that the emitted digest equals $digest before profiling.
./build/profile_classical_direct_cohort \
  --p "$prime" --seed 202607300000 --range-start 2000000 --count 16 \
  --threads 1 --require-point4 1 \
  --cache "$cache" --cache-sha256 "$digest" \
  --cache-resident-bytes 1000000000 --schoof-through 13 \
  --maximum-prime-candidates 10000000 --maximum-x-candidates 1000000 \
  5 7 11 13 17 19 23 29 31 37 41 43 47 53 59

python3 artifacts/local/p125-direct-atkin-mitm-20260803/audit.py
