#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

instance_id=''
run_id=''
run_kind=''
prime=''
worker_id=''
worker_count=''
range_start=''
range_end=''
seed=''
max_level=''
sea_threads=''
curve_family='weber-f'
x1_require_point4=0
sea_strategy='weber-first'
classical_direct_levels=''
classical_direct_maximum_prime_candidates=1000000
classical_direct_maximum_x_candidates=1000000
classical_direct_context_cache=''
classical_direct_context_sha256=''
classical_direct_context_max_file_bytes=4294967296
classical_direct_cache_resident_bytes=0
direct_option_seen=0
curve_threads=1
sea_level_telemetry=1
schoof_fallback=0
skip_incomplete_curves=0
smooth_coordinators=0
table_dir=''
smooth_cache=''
smooth_cache_sha256=''
wall_time_limit_seconds=0
resume=0
resource_args=()
search_args=()

usage() {
  cat <<'EOF'
Usage: launch-worker.sh --run-id RUN --run-kind benchmark|production
       --prime P --worker-id I --worker-count W
       --range-start GLOBAL_FIRST --range-end GLOBAL_EXCLUSIVE --seed SEED
       --max-level L --sea-threads N --table-dir RELPATH
       --smooth-cache PATH --smooth-cache-sha256 SHA256 [options]

Options:
  --instance-id ID                    EC2 instance (or AWS_INSTANCE_ID)
  --max-curves N                      Required positive per-attempt curve cap
  --checkpoint-every N                Checkpoint interval
  --curve-family FAMILY               weber-f, x1-11, or x1-27
  --x1-require-point4 0|1             Require validated X1 point of order four
  --sea-strategy STRATEGY             weber-first or direct-first
  --classical-direct-levels CSV       Ordered direct-level schedule
  --classical-direct-max-prime-candidates N
  --classical-direct-max-x-candidates N
  --classical-direct-context-cache PATH
  --classical-direct-context-sha256 SHA256
  --classical-direct-context-max-file-bytes N
  --classical-direct-cache-resident-bytes N
  --curve-threads N                   Concurrent curve workers
  --sea-level-telemetry 0|1           Emit per-level SEA telemetry
  --schoof-fallback 0|1               Complete exhausted SEA states exactly
  --skip-incomplete-curves 0|1        Skip incomplete SEA states
  --trace-cap N                       Early complete-trace-set cap
  --smooth-threads N                  Exact-smooth workers (0 is automatic)
  --smooth-coordinators N             Exact-smooth coordinator cohorts
  --smooth-max-batch N                Exact-smooth batch cap
  --smooth-root-auxiliary-bytes N     Root-reduction memory cap
  --smooth-build-segment-span N       Smooth-cache build segment span
  --assembly-attempts N               Certificate assembly attempts
  --max-certificate-candidates N      Candidate cap
  --max-candidate-search-nodes N      Candidate enumeration node cap
  --wall-time-limit-seconds N         Host-side timeout; 0 means none
  --resume                            Resume the manifest-bound checkpoint
  --execute                           Launch the remote tmux worker

Dry-run is the default. Every worker receives the same global half-open range
and seed; the production CLI performs the one and only partition. Benchmark
Every run requires a positive --max-curves cap. Production runs also require a
positive wall-time limit for artifact-fetch margin.
EOF
}

append_uint_option() {
  local option="$1" value="${2:-}"
  validate_uint "$option" "$value"
  resource_args+=("--${option}" "$value")
}

append_positive_uint_option() {
  local option="$1" value="${2:-}"
  validate_positive_uint "$option" "$value"
  resource_args+=("--${option}" "$value")
}

append_boolean_option() {
  local option="$1" value="${2:-}"
  validate_uint "$option" "$value"
  [[ "$value" == 0 || "$value" == 1 ]] || die "$option must be zero or one"
  search_args+=("--${option}" "$value")
}

max_curves=0
while (( $# )); do
  case "$1" in
    --instance-id) instance_id="${2:-}"; shift 2 ;;
    --run-id) run_id="${2:-}"; shift 2 ;;
    --run-kind) run_kind="${2:-}"; shift 2 ;;
    --prime) prime="${2:-}"; shift 2 ;;
    --worker-id) worker_id="${2:-}"; shift 2 ;;
    --worker-count) worker_count="${2:-}"; shift 2 ;;
    --range-start) range_start="${2:-}"; shift 2 ;;
    --range-end) range_end="${2:-}"; shift 2 ;;
    --seed) seed="${2:-}"; shift 2 ;;
    --max-level) max_level="${2:-}"; shift 2 ;;
    --sea-threads) sea_threads="${2:-}"; shift 2 ;;
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
    --sea-strategy)
      sea_strategy="${2:-}"
      case "$sea_strategy" in
        weber-first|direct-first) ;;
        *) die 'sea-strategy must be weber-first or direct-first' ;;
      esac
      shift 2 ;;
    --classical-direct-levels)
      classical_direct_levels="${2:-}"; direct_option_seen=1; shift 2 ;;
    --classical-direct-max-prime-candidates)
      classical_direct_maximum_prime_candidates="${2:-}"
      direct_option_seen=1; shift 2 ;;
    --classical-direct-max-x-candidates)
      classical_direct_maximum_x_candidates="${2:-}"
      direct_option_seen=1; shift 2 ;;
    --classical-direct-context-cache)
      classical_direct_context_cache="${2:-}"; direct_option_seen=1; shift 2 ;;
    --classical-direct-context-sha256)
      classical_direct_context_sha256="${2:-}"; direct_option_seen=1; shift 2 ;;
    --classical-direct-context-max-file-bytes)
      classical_direct_context_max_file_bytes="${2:-}"
      direct_option_seen=1; shift 2 ;;
    --classical-direct-cache-resident-bytes)
      classical_direct_cache_resident_bytes="${2:-}"
      direct_option_seen=1; shift 2 ;;
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
      max_curves="${2:-}"; append_positive_uint_option max-curves "$max_curves"; shift 2 ;;
    --checkpoint-every) append_positive_uint_option checkpoint-every "${2:-}"; shift 2 ;;
    --trace-cap) append_positive_uint_option trace-cap "${2:-}"; shift 2 ;;
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

instance_id="$(load_instance_id "$instance_id")"
[[ -n "$run_id" ]] || die '--run-id is required'
validate_run_id "$run_id"
[[ "$run_kind" == benchmark || "$run_kind" == production ]] ||
  die '--run-kind must be benchmark or production'
validate_positive_uint prime "$prime"
validate_uint worker-id "$worker_id"
validate_positive_uint worker-count "$worker_count"
validate_uint range-start "$range_start"
validate_positive_uint range-end "$range_end"
validate_uint seed "$seed"
validate_positive_uint max-level "$max_level"
(( 10#$max_level >= 5 )) || die 'max-level must be at least 5'
validate_positive_uint sea-threads "$sea_threads"
validate_positive_uint max-curves "$max_curves"
if [[ "$curve_family" == weber-f && "$x1_require_point4" == 1 ]]; then
  die '--x1-require-point4 requires an X1 curve family'
fi
require_cmd python3
validate_positive_uint classical-direct-max-prime-candidates \
  "$classical_direct_maximum_prime_candidates"
validate_positive_uint classical-direct-max-x-candidates \
  "$classical_direct_maximum_x_candidates"
validate_positive_uint classical-direct-context-max-file-bytes \
  "$classical_direct_context_max_file_bytes"
(( 10#$classical_direct_context_max_file_bytes >= 96 )) ||
  die 'classical-direct-context-max-file-bytes must be at least 96'
validate_uint classical-direct-cache-resident-bytes \
  "$classical_direct_cache_resident_bytes"
if [[ "$sea_strategy" == direct-first ]]; then
  [[ -n "$classical_direct_levels" ]] ||
    die '--sea-strategy direct-first requires --classical-direct-levels'
  [[ -n "$classical_direct_context_cache" ]] ||
    die '--sea-strategy direct-first requires --classical-direct-context-cache'
  [[ "$classical_direct_context_sha256" =~ ^[0-9a-f]{64}$ ]] ||
    die '--sea-strategy direct-first requires a trusted direct-cache SHA-256'
  if ! python3 - "$classical_direct_levels" <<'PY'
import math
import re
import sys

text = sys.argv[1]
if not re.fullmatch(r"[1-9][0-9]*(?:,[1-9][0-9]*)*", text):
    raise SystemExit(1)
values = [int(value) for value in text.split(",")]
if len(values) != len(set(values)) or any(value > (1 << 32) - 1 for value in values):
    raise SystemExit(1)
for value in values:
    if value <= 3 or any(value % divisor == 0 for divisor in range(2, math.isqrt(value) + 1)):
        raise SystemExit(1)
PY
  then
    die 'classical-direct-levels must be ordered distinct canonical decimal primes greater than three'
  fi
else
  (( direct_option_seen == 0 )) ||
    die 'classical direct options require --sea-strategy direct-first'
fi
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
if [[ "$run_kind" == production ]] &&
    is_zero_uint "$wall_time_limit_seconds"; then
  die 'production runs require a positive --wall-time-limit-seconds for fetch margin'
fi
[[ "$table_dir" =~ ^[A-Za-z0-9._/+~-]+$ && "$table_dir" != /* ]] ||
  die 'table directory must be a simple relative path'
[[ "$table_dir" != ../* && "$table_dir" != */../* && "$table_dir" != */.. ]] ||
  die 'table directory may not traverse parents'
[[ "$smooth_cache_sha256" =~ ^[0-9a-f]{64}$ ]] ||
  die '--smooth-cache-sha256 must be a trusted lowercase SHA-256 digest'
validate_remote_root
[[ "$smooth_cache" =~ ^/[A-Za-z0-9._/+~-]+$ &&
   "$smooth_cache" == "${AWS_REMOTE_ROOT}/"* ]] ||
  die 'smooth cache must be a simple absolute path below the remote root'
[[ "$smooth_cache" != *'/../'* && "$smooth_cache" != */.. ]] ||
  die 'smooth cache path may not traverse parents'
if [[ "$sea_strategy" == direct-first ]]; then
  [[ "$classical_direct_context_cache" =~ ^/[A-Za-z0-9._/+~-]+$ &&
     "$classical_direct_context_cache" == "${AWS_REMOTE_ROOT}/"* ]] ||
    die 'direct cache must be a simple absolute path below the remote root'
  [[ "$classical_direct_context_cache" != *'/../'* &&
     "$classical_direct_context_cache" != */.. ]] ||
    die 'direct cache path may not traverse parents'
fi
require_cmd jq

range_count="$(python3 - "$range_start" "$range_end" <<'PY'
import sys
start, end = map(int, sys.argv[1:])
maximum = (1 << 64) - 1
if not (0 <= start < end <= maximum):
    raise SystemExit("error: require 0 <= range-start < range-end <= UINT64_MAX")
print(end - start)
PY
)"
partition="$(python3 "${SCRIPT_DIR}/shard.py" \
  --global-start "$range_start" --global-count "$range_count" \
  --worker-id "$worker_id" --worker-count "$worker_count" --seed "$seed")"
assigned_start="$(jq -r '.assigned_range_start' <<<"$partition")"
assigned_end="$(jq -r '.assigned_range_end' <<<"$partition")"

remote_launcher="${AWS_REMOTE_ROOT}/current/scripts/aws/remote_worker.py"
remote_args=(
  --root "$AWS_REMOTE_ROOT" --instance-id "$instance_id"
  --run-id "$run_id" --run-kind "$run_kind" --prime "$prime"
  --worker-id "$worker_id" --worker-count "$worker_count"
  --range-start "$range_start" --range-end "$range_end" --seed "$seed"
  --max-level "$max_level" --sea-threads "$sea_threads"
  --table-dir "$table_dir" --smooth-cache "$smooth_cache"
  --smooth-cache-sha256 "$smooth_cache_sha256"
  --wall-time-limit-seconds "$wall_time_limit_seconds"
  "${search_args[@]}"
  "${resource_args[@]}"
)
if [[ "$sea_strategy" == direct-first ]]; then
  remote_args+=(
    --sea-strategy direct-first
    --classical-direct-levels "$classical_direct_levels"
    --classical-direct-max-prime-candidates \
      "$classical_direct_maximum_prime_candidates"
    --classical-direct-max-x-candidates \
      "$classical_direct_maximum_x_candidates"
    --classical-direct-context-cache "$classical_direct_context_cache"
    --classical-direct-context-sha256 "$classical_direct_context_sha256"
    --classical-direct-context-max-file-bytes \
      "$classical_direct_context_max_file_bytes"
    --classical-direct-cache-resident-bytes \
      "$classical_direct_cache_resident_bytes"
  )
fi
if (( resume == 1 )); then
  remote_args+=(--resume)
fi

if ! require_execute; then
  printf 'DRY-RUN: instance=%s run_kind=%s assigned_range=[%s,%s)\n' \
    "$instance_id" "$run_kind" "$assigned_start" "$assigned_end"
  print_cmd python3 "$remote_launcher" "${remote_args[@]}"
  printf 'DRY-RUN: launch manifest binds deployed commit, binary and launcher SHA-256, complete argv, global range, and assigned range\n'
  exit 0
fi

printf -v remote_invocation 'python3 %q' "$remote_launcher"
for argument in "${remote_args[@]}"; do
  printf -v quoted_argument '%q' "$argument"
  remote_invocation+=" ${quoted_argument}"
done
ssm_require_online "$instance_id"
ssm_run_command "$instance_id" 'OneShotSEA launch worker' "$remote_invocation"
