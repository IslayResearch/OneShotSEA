#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 ABSOLUTE_OUTPUT_DIRECTORY" >&2
    exit 2
}

[[ $# == 1 && "$1" == /* ]] || usage
output=$1
[[ ! -e "$output" ]] || {
    echo "refusing to overwrite benchmark directory: $output" >&2
    exit 1
}

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
project_root=$(cd "$script_dir/../.." && pwd)
build_recipe="$script_dir/build-p125-poly-ab.sh"
benchmark_runner="$project_root/scripts/benchmark-p125-poly-ab.sh"
table_dir="$project_root/data/modpoly/weber_f"

mkdir -p "$output"
"$build_recipe" >"$output/BUILD.log" 2>&1

"$benchmark_runner" \
    --label kronecker \
    --baseline "$project_root/build-ab-no-kronecker/benchmark_p125_poly_trusted" \
    --candidate "$project_root/build-ab-candidate/benchmark_p125_poly_trusted" \
    --build-commands "$build_recipe" \
    --build-log "$output/BUILD.log" \
    --table-dir "$table_dir" \
    --output "$output/kronecker" \
    --repetitions 5

"$benchmark_runner" \
    --label reciprocal \
    --baseline "$project_root/build-ab-no-reciprocal/benchmark_p125_poly_trusted" \
    --candidate "$project_root/build-ab-candidate/benchmark_p125_poly_trusted" \
    --build-commands "$build_recipe" \
    --build-log "$output/BUILD.log" \
    --table-dir "$table_dir" \
    --output "$output/reciprocal" \
    --repetitions 5

"$benchmark_runner" \
    --label quotient-context \
    --baseline "$project_root/build-ab-no-quotient-context/benchmark_p125_poly_trusted" \
    --candidate "$project_root/build-ab-candidate/benchmark_p125_poly_trusted" \
    --build-commands "$build_recipe" \
    --build-log "$output/BUILD.log" \
    --table-dir "$table_dir" \
    --output "$output/quotient-context" \
    --repetitions 5

python3 "$project_root/tools/summarize_p125_poly_ab.py" \
    "$output/kronecker" --output "$output/kronecker-summary.json"
python3 "$project_root/tools/summarize_p125_poly_ab.py" \
    "$output/reciprocal" --output "$output/reciprocal-summary.json"
python3 "$project_root/tools/summarize_p125_poly_ab.py" \
    "$output/quotient-context" --output "$output/quotient-context-summary.json"
