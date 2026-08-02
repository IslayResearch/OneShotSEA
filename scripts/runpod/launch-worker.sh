#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

run_id=''
run_kind=''
binary='build/oneshotsea'
prime=''
worker_id=''
worker_count=''
range_start=''
range_end=''
seed=''
max_level=''
sea_threads=''
table_dir=''
smooth_cache=''
smooth_cache_sha256=''
curve_family='weber-f'
x1_require_point4=0
curve_threads=1
sea_level_telemetry=1
schoof_fallback=0
skip_incomplete_curves=0
smooth_coordinators=0
wall_time_limit_seconds=0
max_curves=0
resume=0
resource_args=()
search_args=()

usage() {
  cat <<'EOF'
Usage: launch-worker.sh --run-id RUN --run-kind benchmark|production
       --prime P --worker-id I --worker-count W
       --range-start GLOBAL_FIRST --range-end GLOBAL_EXCLUSIVE --seed SEED
       --max-level L --table-dir RELPATH --smooth-cache PATH
       --smooth-cache-sha256 SHA256 [options]

Options:
  --binary RELPATH                    Search executable below deployed `current`
  --max-curves N                      Stop after at most N curves (0 means none)
  --checkpoint-every N                Checkpoint interval
  --curve-family FAMILY               weber-f, x1-11, or x1-27
  --x1-require-point4 0|1             Require validated X1 point of order four
  --curve-threads N                   Concurrent curve workers
  --sea-level-telemetry 0|1           Emit per-level SEA telemetry
  --schoof-fallback 0|1               Complete exhausted SEA states exactly
  --skip-incomplete-curves 0|1        Skip incomplete SEA states
  --trace-cap N                       Early complete-trace-set cap
  --sea-threads N                     SEA modular-root workers
  --smooth-threads N                  Exact-smooth worker threads (0 is automatic)
  --smooth-coordinators N             Exact-smooth coordinator cohorts
  --smooth-max-batch N                Maximum orders per exact-smooth batch
  --smooth-root-auxiliary-bytes N     Root-reduction auxiliary-memory cap
  --smooth-build-segment-span N       Smooth-cache build segment span
  --assembly-attempts N               Attempts per certificate coefficient
  --max-certificate-candidates N      Certificate candidate cap
  --max-candidate-search-nodes N      Candidate enumeration node cap
  --wall-time-limit-seconds N         Host-side timeout; 0 means none
  --resume                            Resume the manifest-bound checkpoint
  --execute                           Create files and launch the remote tmux session

Dry-run is the default. The range is the shared global half-open interval, not
the worker's assigned subrange. Every worker in a run must receive the same
range and seed; `oneshotsea search` performs the one and only partitioning.
The table directory and binary are relative to the deployed commit. The smooth
cache is an absolute path below RUNPOD_REMOTE_ROOT and must already exist.
Benchmark runs must have a positive max-curves or wall-time bound. Production
runs require a positive wall-time bound so there is time to fetch artifacts.
EOF
}

append_uint_option() {
  local option="$1"
  local value="${2:-}"
  validate_uint "$option" "$value"
  resource_args+=("--${option}" "$value")
}

append_positive_uint_option() {
  local option="$1"
  local value="${2:-}"
  validate_positive_uint "$option" "$value"
  resource_args+=("--${option}" "$value")
}

append_boolean_option() {
  local option="$1"
  local value="${2:-}"
  validate_uint "$option" "$value"
  [[ "$value" == 0 || "$value" == 1 ]] ||
    die "$option must be zero or one"
  search_args+=("--${option}" "$value")
}

while (( $# )); do
  case "$1" in
    --run-id) run_id="${2:-}"; shift 2 ;;
    --run-kind) run_kind="${2:-}"; shift 2 ;;
    --binary) binary="${2:-}"; shift 2 ;;
    --prime) prime="${2:-}"; shift 2 ;;
    --worker-id) worker_id="${2:-}"; shift 2 ;;
    --worker-count) worker_count="${2:-}"; shift 2 ;;
    --range-start) range_start="${2:-}"; shift 2 ;;
    --range-end) range_end="${2:-}"; shift 2 ;;
    --seed) seed="${2:-}"; shift 2 ;;
    --max-level) max_level="${2:-}"; shift 2 ;;
    --table-dir) table_dir="${2:-}"; shift 2 ;;
    --smooth-cache) smooth_cache="${2:-}"; shift 2 ;;
    --smooth-cache-sha256) smooth_cache_sha256="${2:-}"; shift 2 ;;
    --curve-family)
      curve_family="${2:-}"
      case "$curve_family" in
        weber-f|x1-11|x1-27) ;;
        *) die 'curve-family must be weber-f, x1-11, or x1-27' ;;
      esac
      search_args+=(--curve-family "$curve_family"); shift 2 ;;
    --x1-require-point4)
      x1_require_point4="${2:-}"
      append_boolean_option x1-require-point4 "$x1_require_point4"; shift 2 ;;
    --curve-threads)
      curve_threads="${2:-}"
      validate_positive_uint curve-threads "$curve_threads"
      search_args+=(--curve-threads "$curve_threads"); shift 2 ;;
    --sea-level-telemetry)
      sea_level_telemetry="${2:-}"
      append_boolean_option sea-level-telemetry "$sea_level_telemetry"; shift 2 ;;
    --schoof-fallback)
      schoof_fallback="${2:-}"
      append_boolean_option schoof-fallback "$schoof_fallback"; shift 2 ;;
    --skip-incomplete-curves)
      skip_incomplete_curves="${2:-}"
      append_boolean_option skip-incomplete-curves "$skip_incomplete_curves"; shift 2 ;;
    --smooth-coordinators)
      smooth_coordinators="${2:-}"
      validate_uint smooth-coordinators "$smooth_coordinators"
      search_args+=(--smooth-coordinators "$smooth_coordinators"); shift 2 ;;
    --max-curves)
      max_curves="${2:-}"
      append_uint_option max-curves "$max_curves"; shift 2 ;;
    --checkpoint-every) append_positive_uint_option checkpoint-every "${2:-}"; shift 2 ;;
    --trace-cap) append_positive_uint_option trace-cap "${2:-}"; shift 2 ;;
    --sea-threads)
      sea_threads="${2:-}"
      append_positive_uint_option sea-threads "$sea_threads"; shift 2 ;;
    --smooth-threads) append_uint_option smooth-threads "${2:-}"; shift 2 ;;
    --smooth-max-batch) append_positive_uint_option smooth-max-batch "${2:-}"; shift 2 ;;
    --smooth-root-auxiliary-bytes) append_positive_uint_option smooth-root-auxiliary-bytes "${2:-}"; shift 2 ;;
    --smooth-build-segment-span) append_positive_uint_option smooth-build-segment-span "${2:-}"; shift 2 ;;
    --assembly-attempts) append_positive_uint_option assembly-attempts "${2:-}"; shift 2 ;;
    --max-certificate-candidates) append_positive_uint_option max-certificate-candidates "${2:-}"; shift 2 ;;
    --max-candidate-search-nodes) append_positive_uint_option max-candidate-search-nodes "${2:-}"; shift 2 ;;
    --wall-time-limit-seconds) wall_time_limit_seconds="${2:-}"; shift 2 ;;
    --resume) resume=1; shift ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

[[ -n "$run_id" ]] || die "--run-id is required"
validate_run_id "$run_id"
[[ "$run_kind" == benchmark || "$run_kind" == production ]] ||
  die "--run-kind must be benchmark or production"
[[ "$binary" =~ ^[A-Za-z0-9._/+~-]+$ && "$binary" != /* ]] ||
  die "binary must be a simple relative path"
[[ "$binary" != ../* && "$binary" != */../* && "$binary" != */.. ]] ||
  die "binary path may not traverse parents"
validate_positive_uint prime "$prime"
validate_uint worker-id "$worker_id"
validate_positive_uint worker-count "$worker_count"
validate_uint range-start "$range_start"
validate_positive_uint range-end "$range_end"
validate_uint seed "$seed"
validate_positive_uint max-level "$max_level"
validate_positive_uint sea-threads "$sea_threads"
if [[ "$curve_family" == weber-f && "$x1_require_point4" == 1 ]]; then
  die '--x1-require-point4 requires an X1 curve family'
fi
require_cmd python3
if ! python3 - "$curve_threads" "$smooth_coordinators" <<'PY'
import sys
curve_threads, smooth_coordinators = map(int, sys.argv[1:])
if smooth_coordinators > curve_threads:
    raise SystemExit(1)
PY
then
  die '--smooth-coordinators may not exceed --curve-threads'
fi
validate_uint wall-time-limit-seconds "$wall_time_limit_seconds"
if [[ "$run_kind" == benchmark ]] && is_zero_uint "$max_curves" &&
    is_zero_uint "$wall_time_limit_seconds"; then
  die 'benchmark runs require positive --max-curves or --wall-time-limit-seconds'
fi
if [[ "$run_kind" == production ]] &&
    is_zero_uint "$wall_time_limit_seconds"; then
  die 'production runs require a positive --wall-time-limit-seconds for fetch margin'
fi
[[ "$table_dir" =~ ^[A-Za-z0-9._/+~-]+$ && "$table_dir" != /* ]] ||
  die "table directory must be a simple relative path"
[[ "$table_dir" != ../* && "$table_dir" != */../* && "$table_dir" != */.. ]] ||
  die "table directory may not traverse parents"
[[ "$smooth_cache_sha256" =~ ^[0-9a-f]{64}$ ]] ||
  die "--smooth-cache-sha256 must be a trusted lowercase SHA-256 digest"
validate_remote_root
[[ "$smooth_cache" =~ ^/[A-Za-z0-9._/+~-]+$ &&
   "$smooth_cache" == "${RUNPOD_REMOTE_ROOT}/"* ]] ||
  die "smooth cache must be a simple absolute path below RUNPOD_REMOTE_ROOT"
[[ "$smooth_cache" != *'/../'* && "$smooth_cache" != */.. ]] ||
  die "smooth cache path may not traverse parents"
partition_values="$(python3 - "$worker_id" "$worker_count" "$range_start" "$range_end" "$seed" "$max_level" "$wall_time_limit_seconds" "$curve_threads" "$x1_require_point4" "$sea_level_telemetry" "$schoof_fallback" "$skip_incomplete_curves" "$smooth_coordinators" ${resource_args[@]+"${resource_args[@]}"} <<'PY'
import sys

maximum = (1 << 64) - 1
fixed = list(map(int, sys.argv[1:14]))
worker, workers, start, end, seed, max_level = fixed[:6]
resource_values = [int(value) for value in sys.argv[15::2]]
if any(value > maximum for value in
       [*fixed, *resource_values]):
    raise SystemExit("error: search integer exceeds the unsigned 64-bit CLI limit")
if not 0 <= worker < workers:
    raise SystemExit("error: require 0 <= worker-id < worker-count")
if not start < end:
    raise SystemExit("error: require range-start < range-end")
if max_level < 5:
    raise SystemExit("error: max-level must be at least 5")
width, remainder = divmod(end - start, workers)
count = width + (worker < remainder)
assigned_start = start + worker * width + min(worker, remainder)
print(end - start, assigned_start, assigned_start + count, count)
PY
)"
read -r range_count assigned_start assigned_end assigned_count <<<"$partition_values"
build_ssh_args

remote_run_dir="${RUNPOD_REMOTE_ROOT}/runs/${run_id}/worker-${worker_id}"
session_run="${run_id//./_}"
session="sea-${session_run}-${worker_id}"

if ! require_execute; then
  dry_run_build_id='git:deployed-commit'
  dry_run_cmd=(
    "${RUNPOD_REMOTE_ROOT}/current/${binary}" search
    --p "$prime" --seed "$seed"
    --range-start "$range_start" --range-end "$range_end"
    --worker-id "$worker_id" --worker-count "$worker_count"
    --max-level "$max_level"
    --table-dir "${RUNPOD_REMOTE_ROOT}/current/${table_dir}"
    --smooth-cache "$smooth_cache"
    --smooth-cache-sha256 "$smooth_cache_sha256"
    --checkpoint "${remote_run_dir}/checkpoint.json"
    --progress "${remote_run_dir}/progress.jsonl"
    --certificate-out "${remote_run_dir}/certificate.txt"
    --build-id "$dry_run_build_id"
    ${search_args[@]+"${search_args[@]}"}
    ${resource_args[@]+"${resource_args[@]}"}
  )
  dry_run_launch=("${dry_run_cmd[@]}")
  if ! is_zero_uint "$wall_time_limit_seconds"; then
    dry_run_launch=(timeout --signal=TERM --kill-after=60
      "$wall_time_limit_seconds" "${dry_run_cmd[@]}")
  fi
  printf 'DRY-RUN: tmux session=%s run_kind=%s remote_dir=%s assigned_range=[%s,%s)\n' \
    "$session" "$run_kind" "$remote_run_dir" "$assigned_start" "$assigned_end"
  print_cmd "${dry_run_launch[@]}"
  exit 0
fi

# shellcheck disable=SC2016
remote_script='set -euo pipefail
root=$1; run_id=$2; run_kind=$3; binary=$4; prime=$5; worker_id=$6
worker_count=$7; range_start=$8; range_end=$9; range_count=${10}
assigned_start=${11}; assigned_end=${12}; assigned_count=${13}; seed=${14}
max_level=${15}; table_dir=${16}; smooth_cache=${17}
smooth_cache_sha256=${18}; wall_time_limit_seconds=${19}; resume=${20}
shift 20
deploy=$(readlink -f "$root/current")
[[ -n "$deploy" && -d "$deploy" ]] || { echo "error: no current deployment" >&2; exit 2; }
exe="$deploy/$binary"
tables="$deploy/$table_dir"
[[ -x "$exe" ]] || { echo "error: search executable is not executable: $exe" >&2; exit 2; }
[[ -d "$tables" ]] || { echo "error: Weber table directory does not exist: $tables" >&2; exit 2; }
[[ -f "$smooth_cache" ]] || { echo "error: trusted smooth cache does not exist: $smooth_cache" >&2; exit 2; }
actual_smooth_cache_sha256=$(python3 - "$smooth_cache" <<"PY"
import hashlib
import sys

digest = hashlib.sha256()
with open(sys.argv[1], "rb") as stream:
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(block)
print(digest.hexdigest())
PY
)
[[ "$actual_smooth_cache_sha256" == "$smooth_cache_sha256" ]] || {
  echo "error: smooth cache does not match its trusted SHA-256" >&2; exit 2;
}
deployment_commit=""
if [[ -f "$deploy/environment.txt" ]]; then
  deployment_commit=$(sed -n "s/^git_commit=//p" "$deploy/environment.txt" | head -n 1)
fi
if [[ -z "$deployment_commit" ]] && command -v git >/dev/null 2>&1; then
  deployment_commit=$(git -C "$deploy" rev-parse HEAD 2>/dev/null || true)
fi
[[ "$deployment_commit" =~ ^[0-9a-f]{40}$ ]] || {
  echo "error: current deployment has no trustworthy Git commit identity" >&2; exit 2;
}
binary_sha256=$(sha256sum "$exe" | awk "{print \$1}")
[[ "$binary_sha256" =~ ^[0-9a-f]{64}$ ]] || {
  echo "error: cannot authenticate deployed binary" >&2; exit 2;
}
run_dir="$root/runs/$run_id/worker-$worker_id"
session_run=${run_id//./_}
session="sea-${session_run}-${worker_id}"
mkdir -p "$run_dir"
if tmux has-session -t "$session" 2>/dev/null; then
  echo "error: tmux session already exists: $session" >&2
  exit 2
fi
checkpoint="$run_dir/checkpoint.json"
progress="$run_dir/progress.jsonl"
result="$run_dir/certificate.txt"
log="$run_dir/worker.log"
resource_usage="$run_dir/resource-usage.txt"
attempts="$run_dir/attempts.jsonl"
command_file="$run_dir/command.sh"
manifest="$run_dir/manifest.json"
cmd=("$exe" search --p "$prime" --seed "$seed"
     --range-start "$range_start" --range-end "$range_end"
     --worker-id "$worker_id" --worker-count "$worker_count"
     --max-level "$max_level" --table-dir "$tables"
     --smooth-cache "$smooth_cache" --smooth-cache-sha256 "$smooth_cache_sha256"
     --checkpoint "$checkpoint" --progress "$progress" --certificate-out "$result"
     --build-id "git:$deployment_commit+binary-sha256:$binary_sha256" "$@")
command_candidate="$run_dir/command.sh.candidate.$$"
cleanup_command_candidate() { rm -f -- "$command_candidate"; }
trap cleanup_command_candidate EXIT
{
  printf "#!/usr/bin/env bash\nset -uo pipefail\ncd %q\n" "$deploy"
  printf "started_utc=\$(date -u +%%Y-%%m-%%dT%%H:%%M:%%SZ)\n"
  printf "started_epoch=\$(date +%%s)\n"
  printf "printf '\''{\"event\":\"start\",\"utc\":\"%%s\",\"epoch\":%%s}\\n'\'' \"\$started_utc\" \"\$started_epoch\" >>%q\n" "$attempts"
  printf "printf '\''attempt_start utc=%%s epoch=%%s\\n'\'' \"\$started_utc\" \"\$started_epoch\" >>%q\n" "$resource_usage"
  printf "set +e\n/usr/bin/time -a -v -o %q --" "$resource_usage"
  if [[ ! "$wall_time_limit_seconds" =~ ^0+$ ]]; then
    printf " %q %q %q %q" timeout --signal=TERM --kill-after=60 "$wall_time_limit_seconds"
  fi
  printf " %q" "${cmd[@]}"
  printf " >>%q 2>&1\nstatus=\$?\nset -e\n" "$log"
  printf "ended_utc=\$(date -u +%%Y-%%m-%%dT%%H:%%M:%%SZ)\n"
  printf "ended_epoch=\$(date +%%s)\n"
  printf "printf '\''{\"event\":\"end\",\"utc\":\"%%s\",\"epoch\":%%s,\"status\":%%s}\\n'\'' \"\$ended_utc\" \"\$ended_epoch\" \"\$status\" >>%q\n" "$attempts"
  printf "exit \"\$status\"\n"
} >"$command_candidate"
chmod 700 "$command_candidate"
command_sha256=$(sha256sum "$command_candidate" | awk "{print \$1}")
new_run=0
if [[ -f "$manifest" ]]; then
  [[ "$resume" == 1 ]] || { echo "error: worker manifest exists; use --resume after checking it" >&2; exit 2; }
  [[ -s "$checkpoint" ]] || { echo "error: cannot resume without a nonempty checkpoint" >&2; exit 2; }
  [[ ! -e "$result" ]] || { echo "error: result already exists; fetch and verify it instead of resuming" >&2; exit 2; }
  [[ -f "$command_file" && ! -L "$command_file" ]] || {
    echo "error: retained command file is missing or not regular" >&2; exit 2;
  }
  cmp -s "$command_candidate" "$command_file" || {
    echo "error: retained command file changed" >&2; exit 2;
  }
  python3 - "$manifest" "$run_id" "$run_kind" "$worker_id" "$worker_count" "$range_start" "$range_end" "$range_count" "$assigned_start" "$assigned_end" "$assigned_count" "$seed" "$prime" "$deployment_commit" "$binary_sha256" "$wall_time_limit_seconds" "$command_sha256" "${cmd[@]}" <<"PY"
import json
import sys

(path, run_id, run_kind, worker_id, worker_count, start, end, count,
 assigned_start, assigned_end, assigned_count, seed, prime,
 deployment_commit, binary_sha256, wall_time_limit_seconds,
 command_sha256) = sys.argv[1:18]
command = sys.argv[18:]
with open(path, encoding="utf-8") as stream:
    value = json.load(stream)
expected = {
    "schema": "oneshotsea.runpod-worker.v3",
    "run_id": run_id,
    "run_kind": run_kind,
    "worker_id": int(worker_id),
    "worker_count": int(worker_count),
    "global_range": {"start": start, "end": end, "count": count},
    "assigned_range": {
        "start": assigned_start, "end": assigned_end,
        "count": assigned_count,
    },
    "seed": seed,
    "prime": prime,
    "deployment_commit": deployment_commit,
    "binary_sha256": binary_sha256,
    "wall_time_limit_seconds": int(wall_time_limit_seconds),
    "command_sha256": command_sha256,
    "command_argv": command,
}
for key, wanted in expected.items():
    if value.get(key) != wanted:
        raise SystemExit(f"error: resume manifest mismatch for {key}")
PY
else
  [[ "$resume" == 0 ]] || { echo "error: --resume requested but no prior manifest exists" >&2; exit 2; }
  [[ ! -e "$checkpoint" && ! -e "$progress" && ! -e "$result" &&
     ! -e "$command_file" ]] || {
    echo "error: worker artifacts exist without a manifest" >&2; exit 2;
  }
  manifest_tmp="$manifest.tmp.$$"
  python3 - "$run_id" "$run_kind" "$worker_id" "$worker_count" "$range_start" "$range_end" "$range_count" "$assigned_start" "$assigned_end" "$assigned_count" "$seed" "$prime" "$deployment_commit" "$binary_sha256" "$wall_time_limit_seconds" "$command_sha256" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "${cmd[@]}" <<"PY" >"$manifest_tmp"
import json
import sys

(run_id, run_kind, worker_id, worker_count, start, end, count,
 assigned_start, assigned_end, assigned_count, seed, prime,
 deployment_commit, binary_sha256, wall_time_limit_seconds,
 command_sha256, started_utc) = sys.argv[1:18]
value = {
    "schema": "oneshotsea.runpod-worker.v3",
    "run_id": run_id,
    "run_kind": run_kind,
    "worker_id": int(worker_id),
    "worker_count": int(worker_count),
    "global_range": {"start": start, "end": end, "count": count},
    "assigned_range": {
        "start": assigned_start, "end": assigned_end,
        "count": assigned_count,
    },
    "seed": seed,
    "prime": prime,
    "deployment_commit": deployment_commit,
    "binary_sha256": binary_sha256,
    "build_id": f"git:{deployment_commit}+binary-sha256:{binary_sha256}",
    "wall_time_limit_seconds": int(wall_time_limit_seconds),
    "command_sha256": command_sha256,
    "command_argv": sys.argv[18:],
    "started_utc": started_utc,
}
json.dump(value, sys.stdout, sort_keys=True, separators=(",", ":"))
sys.stdout.write("\n")
PY
  mv -f -- "$manifest_tmp" "$manifest"
  new_run=1
fi
if [[ "$new_run" == 1 ]]; then
  mv "$command_candidate" "$command_file"
else
  rm -f -- "$command_candidate"
fi
trap - EXIT
tmux new-session -d -s "$session" "$command_file"
printf "session=%s\nrun_dir=%s\nassigned_range=[%s,%s)\n" "$session" "$run_dir" "$assigned_start" "$assigned_end"
'
remote_args=(
  "$RUNPOD_REMOTE_ROOT" "$run_id" "$run_kind" "$binary" "$prime" "$worker_id"
  "$worker_count" "$range_start" "$range_end" "$range_count" "$assigned_start"
  "$assigned_end" "$assigned_count" "$seed" "$max_level" "$table_dir"
  "$smooth_cache" "$smooth_cache_sha256" "$wall_time_limit_seconds" "$resume"
  ${search_args[@]+"${search_args[@]}"}
  ${resource_args[@]+"${resource_args[@]}"}
)
printf -v remote_invocation 'bash -c %q --' "$remote_script"
for arg in "${remote_args[@]}"; do
  printf -v quoted_arg '%q' "$arg"
  remote_invocation+=" ${quoted_arg}"
done
# shellcheck disable=SC2029
ssh "${SSH_ARGS[@]}" "$SSH_DESTINATION" "$remote_invocation"
