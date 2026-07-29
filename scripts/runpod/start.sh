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
      echo 'Usage: start.sh [--pod-id ID] [--execute]'; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done
pod_id="$(load_pod_id "$pod_id")"

if ! require_execute; then
  printf 'DRY-RUN: POST %s/v2/pods/%s/action {"action":"start"}\n' "$RUNPOD_API_BASE" "$pod_id"
  exit 0
fi

current="$(api_request GET "/v2/pods/${pod_id}")"
if [[ "$(jq -r '.status' <<<"$current")" == RUNNING ]]; then
  note "pod ${pod_id} is already RUNNING"
  save_pod_state "$current" >/dev/null
  jq '{id, status, gpu, cost, startedAt}' <<<"$current"
  exit 0
fi
jq -e '.actions | index("start") != null' <<<"$current" >/dev/null ||
  die "RunPod does not currently allow start for pod ${pod_id}"
response="$(api_request POST "/v2/pods/${pod_id}/action" '{"action":"start"}')"
save_pod_state "$response" >/dev/null
jq '{id, status, gpu, cost, startedAt, actions}' <<<"$response"
