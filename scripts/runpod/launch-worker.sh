#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

run_id=''
binary='build/oneshot-sea'
prime=''
worker_id=''
worker_count=''
range_start=''
range_end=''
seed=''
resume=0
extra_args=()

usage() {
  cat <<'EOF'
Usage: launch-worker.sh --run-id RUN --prime P --worker-id I --worker-count W
       --range-start FIRST --range-end EXCLUSIVE --seed SEED [options]

Options:
  --binary RELPATH       Search executable below deployed `current`
  --search-arg ARG       Repeatable non-secret additional argument
  --resume               Add --resume-from CHECKPOINT when it exists
  --execute              Create files and launch the remote tmux session

The search executable contract is documented in docs/runpod.md. Dry-run is
default. Ranges are half-open, deterministic, and use arbitrary-size decimals.
EOF
}

while (( $# )); do
  case "$1" in
    --run-id) run_id="${2:-}"; shift 2 ;;
    --binary) binary="${2:-}"; shift 2 ;;
    --prime) prime="${2:-}"; shift 2 ;;
    --worker-id) worker_id="${2:-}"; shift 2 ;;
    --worker-count) worker_count="${2:-}"; shift 2 ;;
    --range-start) range_start="${2:-}"; shift 2 ;;
    --range-end) range_end="${2:-}"; shift 2 ;;
    --seed) seed="${2:-}"; shift 2 ;;
    --search-arg) extra_args+=("${2:-}"); shift 2 ;;
    --resume) resume=1; shift ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

[[ -n "$run_id" ]] || die "--run-id is required"
validate_run_id "$run_id"
[[ "$binary" =~ ^[A-Za-z0-9._/+~-]+$ && "$binary" != /* ]] || die "binary must be a simple relative path"
[[ "$binary" != ../* && "$binary" != */../* && "$binary" != */.. ]] || die "binary path may not traverse parents"
validate_positive_uint prime "$prime"
validate_uint worker-id "$worker_id"
validate_positive_uint worker-count "$worker_count"
validate_uint range-start "$range_start"
validate_positive_uint range-end "$range_end"
validate_uint seed "$seed"
require_cmd python3
python3 - "$worker_id" "$worker_count" "$range_start" "$range_end" <<'PY'
import sys
i, workers, start, end = map(int, sys.argv[1:])
if not 0 <= i < workers:
    raise SystemExit("error: require 0 <= worker-id < worker-count")
if not start < end:
    raise SystemExit("error: require range-start < range-end")
PY
range_count="$(python3 -c 'import sys; print(int(sys.argv[2])-int(sys.argv[1]))' "$range_start" "$range_end")"
validate_remote_root
build_ssh_args

remote_run_dir="${RUNPOD_REMOTE_ROOT}/runs/${run_id}/worker-${worker_id}"
session_run="${run_id//./_}"
session="sea-${session_run}-${worker_id}"

if ! require_execute; then
  printf 'DRY-RUN: tmux session=%s remote_dir=%s\n' "$session" "$remote_run_dir"
  printf 'DRY-RUN: %s/current/%s --prime %s --worker-id %s --worker-count %s --range-start %s --range-end %s --seed %s --checkpoint %s/checkpoint.json --progress-jsonl %s/progress.jsonl --result %s/certificate.txt' \
    "$RUNPOD_REMOTE_ROOT" "$binary" "$prime" "$worker_id" "$worker_count" \
    "$range_start" "$range_end" "$seed" "$remote_run_dir" "$remote_run_dir" "$remote_run_dir"
  if (( resume )); then printf ' --resume-from %s/checkpoint.json' "$remote_run_dir"; fi
  printf '\n'
  (( ${#extra_args[@]} == 0 )) || print_cmd extra-search-args "${extra_args[@]}"
  exit 0
fi

# shellcheck disable=SC2016
remote_script='set -euo pipefail
root=$1; run_id=$2; binary=$3; prime=$4; worker_id=$5; worker_count=$6
range_start=$7; range_end=$8; range_count=$9; seed=${10}; resume=${11}; shift 11
deploy=$(readlink -f "$root/current")
[[ -n "$deploy" && -d "$deploy" ]] || { echo "error: no current deployment" >&2; exit 2; }
exe="$deploy/$binary"
[[ -x "$exe" ]] || { echo "error: search executable is not executable: $exe" >&2; exit 2; }
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
command_file="$run_dir/command.sh"
manifest="$run_dir/manifest.json"
if [[ -f "$manifest" ]]; then
  [[ "$resume" == 1 ]] || { echo "error: worker manifest exists; use --resume after checking it" >&2; exit 2; }
  [[ -s "$checkpoint" ]] || { echo "error: cannot resume without a nonempty checkpoint" >&2; exit 2; }
  [[ ! -s "$result" ]] || { echo "error: result already exists; fetch and verify it instead of resuming" >&2; exit 2; }
  python3 - "$manifest" "$run_id" "$worker_id" "$worker_count" "$range_start" "$range_end" "$seed" "$prime" <<"PY"
import json, sys
path, run_id, worker_id, worker_count, start, end, seed, prime = sys.argv[1:]
with open(path, encoding="utf-8") as stream:
    value = json.load(stream)
expected = {"run_id": run_id, "worker_id": int(worker_id), "worker_count": int(worker_count),
            "range_start": start, "range_end": end, "seed": seed, "prime": prime}
for key, wanted in expected.items():
    if value.get(key) != wanted:
        raise SystemExit(f"error: resume manifest mismatch for {key}")
PY
else
  [[ "$resume" == 0 ]] || { echo "error: --resume requested but no prior manifest exists" >&2; exit 2; }
  {
    printf "{\"schema\":1,\"run_id\":\"%s\",\"worker_id\":%s,\"worker_count\":%s," "$run_id" "$worker_id" "$worker_count"
    printf "\"range_start\":\"%s\",\"range_end\":\"%s\",\"range_count\":\"%s\"," "$range_start" "$range_end" "$range_count"
    printf "\"seed\":\"%s\",\"prime\":\"%s\",\"git_commit\":\"%s\",\"started_utc\":\"%s\"}\n" "$seed" "$prime" "$(basename "$deploy")" "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  } >"$manifest"
fi
cmd=("$exe" --prime "$prime" --worker-id "$worker_id" --worker-count "$worker_count"
     --range-start "$range_start" --range-end "$range_end" --seed "$seed"
     --checkpoint "$checkpoint" --progress-jsonl "$progress" --result "$result")
if [[ "$resume" == 1 && -f "$checkpoint" ]]; then cmd+=(--resume-from "$checkpoint"); fi
cmd+=("$@")
{
  printf "#!/usr/bin/env bash\nset -euo pipefail\ncd %q\n" "$deploy"
  printf "exec"
  printf " %q" "${cmd[@]}"
  printf " >>%q 2>&1\n" "$log"
} >"$command_file"
chmod 700 "$command_file"
tmux new-session -d -s "$session" "$command_file"
printf "session=%s\nrun_dir=%s\n" "$session" "$run_dir"
'
remote_args=("$RUNPOD_REMOTE_ROOT" "$run_id" "$binary" "$prime" "$worker_id" "$worker_count"
             "$range_start" "$range_end" "$range_count" "$seed" "$resume" "${extra_args[@]}")
printf -v remote_invocation 'bash -c %q --' "$remote_script"
for arg in "${remote_args[@]}"; do
  printf -v quoted_arg '%q' "$arg"
  remote_invocation+=" ${quoted_arg}"
done
# shellcheck disable=SC2029
ssh "${SSH_ARGS[@]}" "$SSH_DESTINATION" "$remote_invocation"
