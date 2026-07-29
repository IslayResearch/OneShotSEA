#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

pod_id=''
run_id=''
destination=''

usage() {
  cat <<'EOF'
Usage: fetch.sh --run-id RUN [--pod-id ID] [--destination DIR] [--execute]

Copies logs, manifests, checkpoints, and results without deleting either side.
The default destination is artifacts/runpod/RUN/POD. Dry-run is default.
EOF
}

while (( $# )); do
  case "$1" in
    --pod-id) pod_id="${2:-}"; shift 2 ;;
    --run-id) run_id="${2:-}"; shift 2 ;;
    --destination) destination="${2:-}"; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

pod_id="$(load_pod_id "$pod_id")"
[[ -n "$run_id" ]] || die "--run-id is required"
validate_run_id "$run_id"
validate_remote_root
build_ssh_args
if [[ -z "$destination" ]]; then
  destination="${PROJECT_ROOT}/artifacts/runpod/${run_id}/${pod_id}"
fi
[[ "$destination" == /* ]] || destination="$(pwd)/${destination}"
[[ "$destination" != / && "$destination" != "$HOME" && "$destination" != "$PROJECT_ROOT" ]] ||
  die "refusing a broad fetch destination"
require_cmd rsync

ssh_transport='ssh'
for arg in "${SSH_ARGS[@]}"; do
  printf -v quoted_arg '%q' "$arg"
  ssh_transport+=" ${quoted_arg}"
done
remote_source="${SSH_DESTINATION}:${RUNPOD_REMOTE_ROOT}/runs/${run_id}/"

if ! require_execute; then
  print_cmd rsync -a --safe-links -e "$ssh_transport" "$remote_source" "${destination}/"
  printf 'DRY-RUN: write non-secret cost/retrieval metadata to %s/fetch-metadata.json\n' "$destination"
  exit 0
fi

mkdir -p "$destination"
rsync -a --safe-links -e "$ssh_transport" "$remote_source" "${destination}/"

state_path="$(pod_state_path "$pod_id")"
if [[ -f "$state_path" ]]; then
  state_json="$(cat "$state_path")"
else
  state_json='null'
fi
require_cmd jq
metadata_tmp="${destination}/fetch-metadata.json.tmp.$$"
jq -n \
  --arg fetched_at "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
  --arg run_id "$run_id" \
  --arg pod_id "$pod_id" \
  --arg source "${RUNPOD_REMOTE_ROOT}/runs/${run_id}" \
  --argjson pod_state "$state_json" '
  {
    schema: 1,
    fetched_at: $fetched_at,
    run_id: $run_id,
    pod_id: $pod_id,
    remote_source: $source,
    pod_state: $pod_state,
    estimated_current_session_compute_cost_usd:
      (if ($pod_state.runtime_uptime_seconds != null and
           $pod_state.provisioned_hourly_rate_usd != null)
       then (($pod_state.runtime_uptime_seconds / 3600) *
             $pod_state.provisioned_hourly_rate_usd)
       else null end),
    estimate_note: "Session uptime times recorded hourly rate; use RunPod billing for authoritative charges."
  }' >"$metadata_tmp"
mv -f -- "$metadata_tmp" "${destination}/fetch-metadata.json"

checksum_file="${destination}/SHA256SUMS"
(
  cd "$destination"
  while IFS= read -r -d '' path; do
    shasum -a 256 "$path"
  done < <(find . -type f ! -name SHA256SUMS ! -name '*.tmp.*' -print0)
) >"$checksum_file"
printf '%s\n' "$destination"
