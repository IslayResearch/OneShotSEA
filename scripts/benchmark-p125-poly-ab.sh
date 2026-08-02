#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 --label LABEL --baseline BINARY --candidate BINARY --build-commands FILE --build-log FILE --table-dir DIRECTORY --output DIRECTORY [--repetitions N]" >&2
    exit 2
}

label=
baseline=
candidate=
build_commands=
build_log=
table_dir=
output=
repetitions=5
while (($#)); do
    case "$1" in
        --label) label=${2-}; shift 2 ;;
        --baseline) baseline=${2-}; shift 2 ;;
        --candidate) candidate=${2-}; shift 2 ;;
        --build-commands) build_commands=${2-}; shift 2 ;;
        --build-log) build_log=${2-}; shift 2 ;;
        --table-dir) table_dir=${2-}; shift 2 ;;
        --output) output=${2-}; shift 2 ;;
        --repetitions) repetitions=${2-}; shift 2 ;;
        *) usage ;;
    esac
done

[[ -n "$label" && -x "$baseline" && -x "$candidate" ]] || usage
[[ -d "$table_dir" && -n "$output" ]] || usage
[[ -f "$build_commands" && ! -L "$build_commands" ]] || usage
[[ -f "$build_log" && ! -L "$build_log" ]] || usage
[[ "$repetitions" =~ ^[1-9][0-9]*$ ]] || usage
[[ ! -e "$output" ]] || {
    echo "refusing to overwrite benchmark output: $output" >&2
    exit 1
}
[[ -x /usr/bin/time ]] || {
    echo "GNU /usr/bin/time is required" >&2
    exit 1
}
command -v sha256sum >/dev/null || {
    echo "sha256sum is required" >&2
    exit 1
}

mkdir -p "$output"
cp -- "$baseline" "$output/baseline.bin"
cp -- "$candidate" "$output/candidate.bin"
cp -- "$build_commands" "$output/BUILD_COMMANDS.sh"
cp -- "$build_log" "$output/BUILD.log"
source_commit=$(git rev-parse --verify HEAD)
[[ "$source_commit" =~ ^[0-9a-f]{40}$ ]] || {
    echo "cannot identify source commit" >&2
    exit 1
}
git diff --quiet
git diff --cached --quiet

record_command() {
    printf '%q ' "$@" >>"$output/COMMANDS.sh"
    printf '\n' >>"$output/COMMANDS.sh"
}

warm_tables() {
    find "$table_dir" -maxdepth 1 -type f -name 'phi_*.txt' -print0 \
        | sort -z \
        | xargs -0 sha256sum >/dev/null
}

run_one() {
    local phase=$1
    local binary=$2
    local mode=$3
    local stem="$phase-$mode"
    shift 3
    warm_tables
    record_command /usr/bin/time -v -o "$output/$stem.resource.txt" \
        "$binary" "$@"
    /usr/bin/time -v -o "$output/$stem.resource.txt" \
        "$binary" "$@" >"$output/$stem.stdout" \
        2>"$output/$stem.timing.stderr"
}

{
    echo "schema=oneshotsea.p125-poly-isolated-ab.v1"
    echo "label=$label"
    echo "started_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "repetitions=$repetitions"
    echo "source_commit=$source_commit"
    echo "source_tracked_diff_clean=true"
    echo "baseline=$baseline"
    echo "candidate=$candidate"
    echo "table_dir=$table_dir"
    echo "baseline_sha256=$(sha256sum "$baseline" | awk '{print $1}')"
    echo "candidate_sha256=$(sha256sum "$candidate" | awk '{print $1}')"
    echo "retained_baseline_sha256=$(sha256sum "$output/baseline.bin" | awk '{print $1}')"
    echo "retained_candidate_sha256=$(sha256sum "$output/candidate.bin" | awk '{print $1}')"
    echo "build_commands_sha256=$(sha256sum "$output/BUILD_COMMANDS.sh" | awk '{print $1}')"
    echo "build_log_sha256=$(sha256sum "$output/BUILD.log" | awk '{print $1}')"
    echo "table_manifest_sha256=$(sha256sum "$table_dir/MANIFEST.json" | awk '{print $1}')"
    echo "uname=$(uname -a)"
    echo "nproc=$(nproc)"
    echo "compiler=$(${CXX:-c++} --version | head -n 1)"
    echo "time=$(/usr/bin/time --version | head -n 1)"
    echo "lscpu_begin"
    lscpu
    echo "lscpu_end"
} >"$output/ENVIRONMENT.txt"

: >"$output/COMMANDS.sh"
for phase in b1 a1 a2 b2; do
    case "$phase" in
        b1|b2) binary=$baseline ;;
        a1|a2) binary=$candidate ;;
    esac
    for degree in 194 281 401; do
        run_one "$phase" "$binary" "frobenius-$degree" \
            frobenius "$degree" "$repetitions"
    done
    run_one "$phase" "$binary" sea sea "$table_dir"
done

for mode in frobenius-194 frobenius-281 frobenius-401 sea; do
    reference=
    for phase in b1 a1 a2 b2; do
        projection="$output/$phase-$mode.stdout"
        digest=$(sha256sum "$projection" | awk '{print $1}')
        if [[ -z "$reference" ]]; then
            reference=$digest
        elif [[ "$digest" != "$reference" ]]; then
            echo "semantic projection mismatch for $mode" >&2
            exit 1
        fi
    done
    echo "$mode $reference" >>"$output/PROJECTION_SHA256.txt"
done

echo "completed_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
    >>"$output/ENVIRONMENT.txt"
chmod 0444 "$output/ENVIRONMENT.txt"
(
    cd "$output"
    find . -maxdepth 1 -type f ! -name SHA256SUMS -print0 \
        | sort -z \
        | xargs -0 sha256sum
) >"$output/SHA256SUMS"
