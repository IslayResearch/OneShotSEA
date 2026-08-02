#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
project_root=$(cd "$script_dir/../.." && pwd)
cd "$project_root"

[[ $(uname -s) == Linux ]] || {
    echo "this retained build recipe targets the Linux RunPod host" >&2
    exit 1
}
for command in g++ gcc make nproc sha256sum; do
    command -v "$command" >/dev/null || {
        echo "missing build command: $command" >&2
        exit 1
    }
done

jobs=$(nproc)
# The current RunPod image uses GCC 9, whose spelling for C++20 is c++2a.
common_cxxflags='-O2 -g -std=c++2a -Wall -Wextra -Wpedantic -Wconversion -Wshadow'
common_cflags='-O2 -g -std=c11'

build_variant() {
    local build_dir=$1
    local cxxflags=$2
    [[ ! -e "$build_dir" ]] || {
        echo "refusing to reuse build directory: $build_dir" >&2
        exit 1
    }
    make -j"$jobs" \
        CC=gcc CXX=g++ GMP_PREFIX=/usr BUILD_DIR="$build_dir" \
        CFLAGS="$common_cflags" CXXFLAGS="$cxxflags" \
        "$build_dir/benchmark_p125_poly_trusted" \
        "$build_dir/test_core" "$build_dir/test_poly_square"
    "$build_dir/test_core"
    "$build_dir/test_poly_square"
}

build_variant build-ab-candidate "$common_cxxflags"
build_variant build-ab-no-kronecker \
    "$common_cxxflags -DONESHOTSEA_KRONECKER_COEFFICIENT_THRESHOLD=0"
build_variant build-ab-no-reciprocal \
    "$common_cxxflags -DONESHOTSEA_RECIPROCAL_REDUCTION_DEGREE_THRESHOLD=0"
build_variant build-ab-no-quotient-context \
    "$common_cxxflags -DONESHOTSEA_QUOTIENT_CONTEXT_REUSE=0"

sha256sum \
    build-ab-candidate/benchmark_p125_poly_trusted \
    build-ab-no-kronecker/benchmark_p125_poly_trusted \
    build-ab-no-reciprocal/benchmark_p125_poly_trusted \
    build-ab-no-quotient-context/benchmark_p125_poly_trusted
