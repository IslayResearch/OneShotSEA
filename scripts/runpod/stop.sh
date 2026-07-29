#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

pod_id=''
while (( $# )); do
  case "$1" in
    --pod-id) pod_id="${2:-}"; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help)
      echo 'Usage: stop.sh [--pod-id ID] [--execute]'; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done
pod_id="$(load_pod_id "$pod_id")"

if ! require_execute; then
  printf 'DRY-RUN: POST %s/v2/pods/%s/action {"action":"stop"}\n' "$RUNPOD_API_BASE" "$pod_id"
  exit 0
fi

current="$(api_request GET "/v2/pods/${pod_id}")"
status="$(jq -r '.status' <<<"$current")"
if [[ "$status" == EXITED || "$status" == TERMINATED ]]; then
  note "pod ${pod_id} is already ${status}"
  save_pod_state "$current" >/dev/null
  jq '{id, status, cost, actions}' <<<"$current"
  exit 0
fi
jq -e '.actions | index("stop") != null' <<<"$current" >/dev/null ||
  die "RunPod does not currently allow stop for pod ${pod_id}"
response="$(api_request POST "/v2/pods/${pod_id}/action" '{"action":"stop"}')"
save_pod_state "$response" >/dev/null
jq '{id, status, cost, actions}' <<<"$response"
