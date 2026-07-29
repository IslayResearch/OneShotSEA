#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

name='oneshotsea-worker'
image='runpod/pytorch:2.8.0-py3.11-cuda12.8.1'
gpu_id=''
gpu_count=1
cloud=SECURE
disk_gb=50
volume_gb=50
data_center=''
max_price=''

usage() {
  cat <<'EOF'
Usage: provision.sh --gpu-id ID --max-price-per-hour USD [options] [--execute]

Options:
  --name NAME                 Pod name (default: oneshotsea-worker)
  --image IMAGE               CUDA image reference
  --gpu-count N               GPU count (default: 1)
  --cloud SECURE|COMMUNITY    Cloud tier (default: SECURE)
  --disk-gb N                 Ephemeral container disk (default: 50)
  --volume-gb N               Stateful /workspace disk (default: 50, min: 10)
  --data-center ID            Optional preferred data center
  --max-price-per-hour USD    Required total hourly safety ceiling
  --execute                   Perform catalog check and pod creation

Without --execute this prints a redacted plan and does not contact RunPod.
EOF
}

while (( $# )); do
  case "$1" in
    --name) name="${2:-}"; shift 2 ;;
    --image) image="${2:-}"; shift 2 ;;
    --gpu-id) gpu_id="${2:-}"; shift 2 ;;
    --gpu-count) gpu_count="${2:-}"; shift 2 ;;
    --cloud) cloud="${2:-}"; shift 2 ;;
    --disk-gb) disk_gb="${2:-}"; shift 2 ;;
    --volume-gb) volume_gb="${2:-}"; shift 2 ;;
    --data-center) data_center="${2:-}"; shift 2 ;;
    --max-price-per-hour) max_price="${2:-}"; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

[[ -n "$gpu_id" ]] || die "--gpu-id is required"
[[ -n "$max_price" ]] || die "--max-price-per-hour is required"
[[ "$name" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,62}$ ]] || die "invalid pod name"
[[ "$image" =~ ^[A-Za-z0-9][A-Za-z0-9._/:@+-]{0,255}$ ]] || die "invalid image reference"
gpu_id_re='^[A-Za-z0-9][A-Za-z0-9 ._+-]{0,127}$'
[[ "$gpu_id" =~ $gpu_id_re ]] || die "invalid GPU id"
[[ "$cloud" == SECURE || "$cloud" == COMMUNITY ]] || die "invalid cloud tier"
validate_positive_uint gpu-count "$gpu_count"
validate_positive_uint disk-gb "$disk_gb"
validate_positive_uint volume-gb "$volume_gb"
(( 10#$volume_gb >= 10 )) || die "volume-gb must be at least 10"
[[ "$max_price" =~ ^[0-9]+([.][0-9]+)?$ ]] || die "invalid max hourly price"
if [[ -n "$data_center" ]]; then
  [[ "$data_center" =~ ^[A-Za-z0-9_-]+$ ]] || die "invalid data center id"
fi

require_cmd jq
body="$(jq -cn \
  --arg name "$name" \
  --arg image "$image" \
  --arg gpu "$gpu_id" \
  --arg cloud "$cloud" \
  --arg dc "$data_center" \
  --argjson gpu_count "$gpu_count" \
  --argjson disk "$disk_gb" \
  --argjson volume "$volume_gb" '
    {
      name: $name,
      image: $image,
      gpu: {id: $gpu, count: $gpu_count},
      cloud: $cloud,
      disk: $disk,
      ports: ["22/tcp"],
      env: {},
      mounts: {persistent: {size: $volume, path: "/workspace"}}
    }
    + (if $dc == "" then {} else {dataCenterIds: [$dc]} end)
  ')"
catalog_path="/v2/catalog/gpus?include=AVAILABILITY&product=POD&count=${gpu_count}&cloud=${cloud}"

if ! require_execute; then
  print_cmd curl --request GET "${RUNPOD_API_BASE}${catalog_path}" --header 'Authorization: Bearer [REDACTED]'
  printf 'DRY-RUN: POST %s/v2/pods\n' "$RUNPOD_API_BASE"
  jq '{name, image, gpu, cloud, disk, ports, mounts, dataCenterIds}' <<<"$body"
  printf 'DRY-RUN: hourly safety ceiling USD %s\n' "$max_price"
  exit 0
fi

catalog="$(api_request GET "$catalog_path")"
cloud_key="$(printf '%s' "$cloud" | tr '[:upper:]' '[:lower:]')"
offer="$(jq -cer --arg id "$gpu_id" --arg cloud "$cloud_key" '
  first(.gpus[] | select(.id == $id))
  | {availability, rate: .price[$cloud], max_count: .maxCount[$cloud]}
' <<<"$catalog")" || die "GPU id not present in current RunPod catalog"
[[ "$(jq -r '.availability' <<<"$offer")" != NONE ]] || die "selected GPU is currently unavailable"
jq -e '.max_count | numbers' <<<"$offer" >/dev/null || die "catalog did not return available GPU count"
available_count="$(jq -r '.max_count' <<<"$offer")"
(( 10#$available_count >= 10#$gpu_count )) || die "requested GPU count is unavailable"
quoted_rate="$(jq -r '.rate' <<<"$offer")"
jq -e '.rate | numbers' <<<"$offer" >/dev/null || die "catalog did not return a numeric price"
quoted_total="$(jq -nr --argjson rate "$quoted_rate" --argjson count "$gpu_count" '$rate * $count')"
jq -en --argjson rate "$quoted_total" --argjson cap "$max_price" '$rate <= $cap' >/dev/null ||
  die "conservative catalog total USD ${quoted_total}/hr exceeds ceiling USD ${max_price}/hr"

response="$(api_request POST /v2/pods "$body")"
state_path="$(save_pod_state "$response")"
actual_rate="$(jq -r '.cost // 0' <<<"$response")"
if ! jq -e '.cost | numbers' <<<"$response" >/dev/null ||
   ! jq -en --argjson rate "$actual_rate" --argjson cap "$max_price" '$rate > 0 and $rate <= $cap' >/dev/null; then
  pod_id="$(jq -er '.id' <<<"$response")"
  note "created pod returned a missing/invalid/over-ceiling rate; attempting immediate stop of ${pod_id}"
  if stopped="$(api_request POST "/v2/pods/${pod_id}/action" '{"action":"stop"}')"; then
    save_pod_state "$stopped" >/dev/null
    die "created rate USD ${actual_rate}/hr is unsafe for ceiling USD ${max_price}/hr; pod ${pod_id} was stopped"
  fi
  die "URGENT: automatic stop failed for pod ${pod_id}; stop it in the RunPod console now"
fi

jq '{id, name, status, gpu, cloud, dataCenterId, hourly_rate_usd: .cost,
     createdAt, actions}' <<<"$response"
printf 'state: %s\n' "$state_path" >&2
