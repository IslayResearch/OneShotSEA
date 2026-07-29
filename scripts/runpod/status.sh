#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

pod_id=''
remote=0
run_id=''

usage() {
  cat <<'EOF'
Usage: status.sh [--pod-id ID] [--remote --run-id RUN] [--execute]

Shows an allowlisted Pod API summary. With --remote, also lists matching tmux
sessions and tails compact worker progress logs over SSH. Dry-run is default.
EOF
}

while (( $# )); do
  case "$1" in
    --pod-id) pod_id="${2:-}"; shift 2 ;;
    --remote) remote=1; shift ;;
    --run-id) run_id="${2:-}"; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done
pod_id="$(load_pod_id "$pod_id")"
if (( remote )); then
  [[ -n "$run_id" ]] || die "--remote requires --run-id"
  validate_run_id "$run_id"
  validate_remote_root
  build_ssh_args
fi

if ! require_execute; then
  print_cmd curl --request GET "${RUNPOD_API_BASE}/v2/pods/${pod_id}" --header 'Authorization: Bearer [REDACTED]'
  if (( remote )); then
    print_cmd ssh "${SSH_ARGS[@]}" "$SSH_DESTINATION" \
      "inspect tmux sessions and ${RUNPOD_REMOTE_ROOT}/runs/${run_id}/worker-*/progress.jsonl"
  fi
  exit 0
fi

response="$(api_request GET "/v2/pods/${pod_id}")"
state_path="$(save_pod_state "$response")"
jq '{id, name, status, actions, gpu, cloud, dataCenterId,
     hourly_rate_usd: .cost, createdAt, startedAt,
     runtime: {uptime_seconds: (.runtime.uptime // null),
               gpu: (.runtime.gpus // []), ports: (.runtime.ports // [])}}' <<<"$response"
printf 'state: %s\n' "$state_path" >&2

if (( remote )); then
  remote_command="root='${RUNPOD_REMOTE_ROOT}/runs/${run_id}'; "
  remote_command+="printf '%s\\n' '--- tmux ---'; "
  remote_command+="tmux list-sessions -F '#{session_name} #{session_attached} #{session_created_string}' 2>/dev/null || true; "
  remote_command+="printf '%s\\n' '--- progress tails ---'; "
  remote_command+="find \"\$root\" -maxdepth 2 -type f -name progress.jsonl -print -exec tail -n 2 {} \\; 2>/dev/null || true"
  # run_id and remote root are constrained before this command is built.
  # shellcheck disable=SC2029
  ssh "${SSH_ARGS[@]}" "$SSH_DESTINATION" "$remote_command"
fi
