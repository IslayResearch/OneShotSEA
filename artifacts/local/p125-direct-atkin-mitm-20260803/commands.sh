#!/bin/sh
# Reproduce the controlled source/build/run comparison. The authenticated cache
# must already exist at the exact retained path below.
set -eu

BUNDLE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH= cd -- "$BUNDLE/../../.." && pwd)
WORK_ROOT=${ONESHOTSEA_ATKIN_REPRO_ROOT:-/private/tmp/oneshotsea-atkin-reproduction}
CACHE=/private/tmp/p125-direct-low-5-59.ctx
BASELINE_COMMIT=bbcd04d2c27d87f582f4d579caaacd9d4278ee8e
BASELINE_TREE=5e203de1b77ddd89f9393a8c01e225d2c2f09e2c
CANDIDATE_COMMIT=13cd3a906167700b795b36abaae51c6433017fc8
CANDIDATE_TREE=8cdaa62a5465416e7b9bfcdde32f9e8f016e9a41
CACHE_SHA256=b31c858c5398d7b284ad7b003ce4647211de8dec0412421f90ed226f2926ecfd
HARNESS_SHA256=2ac700e044795325463ee462cd44bdd39c3919e594225cf4632257b2fe89cd27

if [ -e "$WORK_ROOT" ]; then
    echo "refusing to overwrite existing reproduction root: $WORK_ROOT" >&2
    exit 1
fi
test -f "$CACHE"
test "$(shasum -a 256 "$CACHE" | awk '{print $1}')" = "$CACHE_SHA256"
test "$(stat -f '%z' "$CACHE")" = 30203068
test "$(git -C "$REPO" show -s --format=%T "$BASELINE_COMMIT")" = "$BASELINE_TREE"
test "$(git -C "$REPO" show -s --format=%T "$CANDIDATE_COMMIT")" = "$CANDIDATE_TREE"

mkdir -p "$WORK_ROOT/baseline" "$WORK_ROOT/candidate"
git -C "$REPO" archive "$BASELINE_COMMIT" | tar -x -C "$WORK_ROOT/baseline"
git -C "$REPO" archive "$CANDIDATE_COMMIT" | tar -x -C "$WORK_ROOT/candidate"

# Use the candidate's profiler and its Makefile build rule on both source
# trees. This is the same byte-identical harness injection used for capture.
git -C "$REPO" show "$CANDIDATE_COMMIT:tools/profile_classical_direct_cohort.cpp" \
    >"$WORK_ROOT/baseline/tools/profile_classical_direct_cohort.cpp"
git -C "$REPO" show "$CANDIDATE_COMMIT:Makefile" \
    >"$WORK_ROOT/baseline/Makefile"
test "$(shasum -a 256 "$WORK_ROOT/baseline/tools/profile_classical_direct_cohort.cpp" | awk '{print $1}')" = "$HARNESS_SHA256"
test "$(shasum -a 256 "$WORK_ROOT/candidate/tools/profile_classical_direct_cohort.cpp" | awk '{print $1}')" = "$HARNESS_SHA256"
cmp "$WORK_ROOT/baseline/tools/profile_classical_direct_cohort.cpp" \
    "$WORK_ROOT/candidate/tools/profile_classical_direct_cohort.cpp"
cmp "$WORK_ROOT/baseline/Makefile" "$WORK_ROOT/candidate/Makefile"

# Both builds use the flags and dynamically linked libraries recorded in
# provenance.json. Absolute debug paths can make rebuilt binary hashes differ
# when WORK_ROOT differs; the capture-time binary hashes remain provenance for
# the binaries that produced the retained logs.
/usr/bin/make -C "$WORK_ROOT/baseline" -j4 CXX=/usr/bin/clang++ \
    build/profile_classical_direct_cohort
/usr/bin/make -C "$WORK_ROOT/candidate" -j4 CXX=/usr/bin/clang++ \
    build/profile_classical_direct_cohort

run_profile() {
    source_root=$1
    output=$2
    (
        cd "$source_root"
        ./build/profile_classical_direct_cohort \
            --p 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237 \
            --seed 202607300000 --range-start 2000000 --count 16 \
            --threads 1 --require-point4 1 \
            --cache /private/tmp/p125-direct-low-5-59.ctx \
            --cache-sha256 b31c858c5398d7b284ad7b003ce4647211de8dec0412421f90ed226f2926ecfd \
            --cache-resident-bytes 1000000000 --schoof-through 13 \
            --maximum-prime-candidates 10000000 \
            --maximum-x-candidates 1000000 \
            5 7 11 13 17 19 23 29 31 37 41 43 47 53 59
    ) >"$output"
}

# Serial execution prevents the two processes from contending with each other.
run_profile "$WORK_ROOT/baseline" "$WORK_ROOT/baseline.ndjson"
run_profile "$WORK_ROOT/candidate" "$WORK_ROOT/combined-candidate.ndjson"

python3 "$BUNDLE/audit.py" \
    --baseline "$WORK_ROOT/baseline.ndjson" \
    --candidate "$WORK_ROOT/combined-candidate.ndjson"
shasum -a 256 \
    "$WORK_ROOT/baseline/build/profile_classical_direct_cohort" \
    "$WORK_ROOT/candidate/build/profile_classical_direct_cohort" \
    "$WORK_ROOT/baseline/build/liboneshotsea.a" \
    "$WORK_ROOT/candidate/build/liboneshotsea.a" \
    "$WORK_ROOT/baseline.ndjson" "$WORK_ROOT/combined-candidate.ndjson"
