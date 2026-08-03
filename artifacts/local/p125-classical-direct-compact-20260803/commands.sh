#!/bin/sh
set -eu

# Start from a repository containing both immutable commits. Use independent
# no-hardlink clones so candidate and baseline objects cannot alias.
git clone --no-hardlinks . /private/tmp/oneshotsea-candidate
git clone --no-hardlinks . /private/tmp/oneshotsea-baseline
git -C /private/tmp/oneshotsea-candidate checkout --detach 7c6a997ddd4482e311b27d6bd3f7e1aa93b909e9
git -C /private/tmp/oneshotsea-baseline checkout --detach 4f917fcfc2a2dc5d06980e08bef77d9c95c83dbd

/usr/bin/make -C /private/tmp/oneshotsea-candidate -j10 build/benchmark_p125_classical_direct
/usr/bin/make -C /private/tmp/oneshotsea-baseline -j10 build/liboneshotsea.a

# Compile the candidate's checked harness against the baseline library. The
# define changes only the two unavailable matrix-telemetry fields to JSON null.
/usr/bin/clang++ \
  -I/private/tmp/oneshotsea-baseline/include \
  -isystem /opt/homebrew/opt/gmp/include \
  -O2 -g -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
  -DONESHOTSEA_BENCHMARK_LEGACY_CONTEXT=1 \
  /private/tmp/oneshotsea-candidate/tools/benchmark_p125_classical_direct.cpp \
  /private/tmp/oneshotsea-baseline/build/liboneshotsea.a \
  -L/opt/homebrew/opt/gmp/lib -lgmpxx -lgmp \
  -o /private/tmp/oneshotsea-baseline/build/benchmark_p125_classical_direct

# Three same-binary S/P/P/S brackets. Append each JSON record in this order:
# 7/1, 7/4, 7/4, 7/1, 11/1, 11/4, 11/4, 11/1.
for repetition in 1 2 3; do
  /private/tmp/oneshotsea-candidate/build/benchmark_p125_classical_direct --threads 1 7
  /private/tmp/oneshotsea-candidate/build/benchmark_p125_classical_direct --threads 4 7
  /private/tmp/oneshotsea-candidate/build/benchmark_p125_classical_direct --threads 4 7
  /private/tmp/oneshotsea-candidate/build/benchmark_p125_classical_direct --threads 1 7
  /private/tmp/oneshotsea-candidate/build/benchmark_p125_classical_direct --threads 1 11
  /private/tmp/oneshotsea-candidate/build/benchmark_p125_classical_direct --threads 4 11
  /private/tmp/oneshotsea-candidate/build/benchmark_p125_classical_direct --threads 4 11
  /private/tmp/oneshotsea-candidate/build/benchmark_p125_classical_direct --threads 1 11
done

# Same-host compact/baseline bracket. Append the seven JSON records in exactly
# this order; each invocation is a fresh process for isolated peak RSS.
/private/tmp/oneshotsea-candidate/build/benchmark_p125_classical_direct --threads 4 13
/private/tmp/oneshotsea-baseline/build/benchmark_p125_classical_direct --threads 4 13
/private/tmp/oneshotsea-baseline/build/benchmark_p125_classical_direct --threads 4 13
/private/tmp/oneshotsea-candidate/build/benchmark_p125_classical_direct --threads 4 13
/private/tmp/oneshotsea-candidate/build/benchmark_p125_classical_direct --threads 4 29
/private/tmp/oneshotsea-baseline/build/benchmark_p125_classical_direct --threads 4 29
/private/tmp/oneshotsea-candidate/build/benchmark_p125_classical_direct --threads 4 29
