#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

instance_id=''
run_id=''
prime=''
curve_a=''
curve_b=''
max_level=''
trace_cap=64
threads_csv='1,2,4'
table_dir='data/modpoly/weber_f'

usage() {
  cat <<'EOF'
Usage: benchmark-sea.sh --run-id RUN --prime P --a A --b B --max-level L
       [--trace-cap N] [--threads CSV] [--table-dir RELPATH]
       [--instance-id ID] [--execute]

Runs the CAS-free Weber SEA point-counting path once for each comma-separated
thread count. Outputs, GNU-time resource records, environment metadata, exact
binary/commit identity, and SHA-256 digests are retained below the run ID for
fetch.sh. Dry-run is the default.
EOF
}

while (( $# )); do
  case "$1" in
    --instance-id) instance_id="${2:-}"; shift 2 ;;
    --run-id) run_id="${2:-}"; shift 2 ;;
    --prime) prime="${2:-}"; shift 2 ;;
    --a) curve_a="${2:-}"; shift 2 ;;
    --b) curve_b="${2:-}"; shift 2 ;;
    --max-level) max_level="${2:-}"; shift 2 ;;
    --trace-cap) trace_cap="${2:-}"; shift 2 ;;
    --threads) threads_csv="${2:-}"; shift 2 ;;
    --table-dir) table_dir="${2:-}"; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

instance_id="$(load_instance_id "$instance_id")"
[[ -n "$run_id" ]] || die '--run-id is required'
validate_run_id "$run_id"
validate_positive_uint prime "$prime"
validate_uint a "$curve_a"
validate_uint b "$curve_b"
validate_positive_uint max-level "$max_level"
(( 10#$max_level >= 5 )) || die 'max-level must be at least 5'
validate_positive_uint trace-cap "$trace_cap"
[[ "$threads_csv" =~ ^[1-9][0-9]*(,[1-9][0-9]*)*$ ]] ||
  die '--threads must be a comma-separated list of positive integers'
IFS=',' read -r -a thread_values <<<"$threads_csv"
seen_threads=','
for thread in "${thread_values[@]}"; do
  (( 10#$thread <= 256 )) || die 'thread counts may not exceed 256'
  [[ "$seen_threads" != *",${thread},"* ]] || die 'thread counts must be unique'
  seen_threads+="${thread},"
done
[[ "$table_dir" =~ ^[A-Za-z0-9._/+~-]+$ && "$table_dir" != /* ]] ||
  die 'table directory must be a simple relative path'
[[ "$table_dir" != ../* && "$table_dir" != */../* && "$table_dir" != */.. ]] ||
  die 'table directory may not traverse parents'
validate_remote_root

if ! require_execute; then
  printf 'DRY-RUN: benchmark deployed CAS-free SEA on %s; run=%s max_level=%s trace_cap=%s threads=%s\n' \
    "$instance_id" "$run_id" "$max_level" "$trace_cap" "$threads_csv"
  printf 'DRY-RUN: retain exact curve, command argv, build identity, environment, per-thread JSONL/time, and SHA-256 records\n'
  exit 0
fi

ssm_require_online "$instance_id"
# shellcheck disable=SC2016
remote_benchmark='set -euo pipefail
root=$1; instance_id=$2; run_id=$3; prime=$4; curve_a=$5; curve_b=$6
max_level=$7; trace_cap=$8; threads_csv=$9; table_dir=${10}
deploy=$(readlink -f "$root/current")
[[ -d "$deploy" ]]
build_manifest="$deploy/build-manifest.json"
readarray -t identity < <(python3 - "$build_manifest" "$instance_id" <<"PY"
import json, sys
with open(sys.argv[1], encoding="utf-8") as stream:
    value = json.load(stream)
if value.get("schema") != "oneshotsea.aws-build.v1" or value.get("instance_id") != sys.argv[2]:
    raise SystemExit("error: invalid build manifest")
print(value["deployment_commit"])
print(value["binary_sha256"])
PY
)
commit=${identity[0]}; binary_sha=${identity[1]}
exe="$deploy/build/oneshotsea"; tables="$deploy/$table_dir"
[[ -x "$exe" && -d "$tables" ]]
[[ "$(sha256sum "$exe" | awk "{print \$1}")" == "$binary_sha" ]]
run_dir="$root/runs/$run_id"
mkdir -p "$run_dir"
test -z "$(find "$run_dir" -mindepth 1 -maxdepth 1 -print -quit)"
cp "$deploy/environment.txt" "$run_dir/build-environment.txt"
{
  printf "benchmark_started_utc=%s\n" "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  uname -a
  lscpu
  free -h
} >"$run_dir/runtime-environment.txt"
IFS=, read -r -a thread_values <<<"$threads_csv"
for thread in "${thread_values[@]}"; do
  command_file="$run_dir/threads-${thread}.command"
  output_file="$run_dir/threads-${thread}.jsonl"
  time_file="$run_dir/threads-${thread}.time"
  cmd=("$exe" sea-weber-count --p "$prime" --a "$curve_a" --b "$curve_b"
       --max-level "$max_level" --table-dir "$tables" --trace-cap "$trace_cap"
       --sea-threads "$thread")
  printf "exec" >"$command_file"; printf " %q" "${cmd[@]}" >>"$command_file"; printf "\n" >>"$command_file"
  /usr/bin/time -v "${cmd[@]}" >"$output_file" 2>"$time_file"
done
python3 - "$run_dir" "$run_id" "$instance_id" "$prime" "$curve_a" "$curve_b" \
  "$max_level" "$trace_cap" "$threads_csv" "$table_dir" "$commit" "$binary_sha" <<"PY" \
  >"$run_dir/manifest.json"
from datetime import datetime, timezone
import hashlib, json, pathlib, sys
(
    run_dir, run_id, instance_id, prime, curve_a, curve_b, max_level,
    trace_cap, threads_csv, table_dir, commit, binary_sha,
) = sys.argv[1:]
root = pathlib.Path(run_dir)
artifacts = {}
for path in sorted(root.iterdir()):
    if path.is_file() and path.name != "manifest.json":
        artifacts[path.name] = {
            "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        }
json.dump({
    "schema": "oneshotsea.aws-sea-benchmark.v1", "run_id": run_id,
    "instance_id": instance_id, "prime": prime,
    "curve": {"a": curve_a, "b": curve_b},
    "max_level": int(max_level), "trace_cap": int(trace_cap),
    "thread_counts": [int(value) for value in threads_csv.split(",")],
    "table_dir": table_dir, "deployment_commit": commit,
    "binary_sha256": binary_sha,
    "completed_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "artifacts": artifacts,
}, sys.stdout, sort_keys=True, separators=(",", ":"))
sys.stdout.write("\n")
PY
printf "run_dir=%s\nmanifest_sha256=%s\n" "$run_dir" \
  "$(sha256sum "$run_dir/manifest.json" | awk "{print \$1}")"
'
printf -v remote_command 'bash -c %q -- %q %q %q %q %q %q %q %q %q %q' \
  "$remote_benchmark" "$AWS_REMOTE_ROOT" "$instance_id" "$run_id" "$prime" \
  "$curve_a" "$curve_b" "$max_level" "$trace_cap" "$threads_csv" "$table_dir"
ssm_run_command "$instance_id" 'OneShotSEA CAS free SEA benchmark' "$remote_command" 3600
