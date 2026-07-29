#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

cloud=SECURE
gpu_count=1
min_cuda=12.0
gpu_filter=''

usage() {
  cat <<'EOF'
Usage: catalog.sh [--cloud SECURE|COMMUNITY] [--gpu-count N]
                  [--min-cuda VERSION] [--gpu REGEX] [--execute]

Lists current RunPod GPU availability and hourly catalog pricing. Dry-run is
the default; --execute performs the read-only API request.
EOF
}

while (( $# )); do
  case "$1" in
    --cloud) cloud="${2:-}"; shift 2 ;;
    --gpu-count) gpu_count="${2:-}"; shift 2 ;;
    --min-cuda) min_cuda="${2:-}"; shift 2 ;;
    --gpu) gpu_filter="${2:-}"; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

[[ "$cloud" == SECURE || "$cloud" == COMMUNITY ]] || die "invalid cloud tier"
validate_positive_uint gpu-count "$gpu_count"
[[ "$min_cuda" =~ ^[0-9]+([.][0-9]+)?$ ]] || die "invalid CUDA version"
path="/v2/catalog/gpus?include=AVAILABILITY&product=POD&count=${gpu_count}&cloud=${cloud}&minCudaVersion=${min_cuda}"

if ! require_execute; then
  print_cmd curl --request GET "${RUNPOD_API_BASE}${path}" --header 'Authorization: Bearer [REDACTED]'
  exit 0
fi

response="$(api_request GET "$path")"
cloud_key="$(printf '%s' "$cloud" | tr '[:upper:]' '[:lower:]')"
if [[ -n "$gpu_filter" ]]; then
  jq --arg re "$gpu_filter" --arg cloud "$cloud_key" '
    [.gpus[]
      | select((.id | test($re; "i")) or (.name | test($re; "i")))
      | {id, name, memory_gb: .memory, availability,
         hourly_rate_usd: .price[$cloud], available_count: .maxCount[$cloud],
         data_centers: [.dataCenters[]? | select(.availability != "NONE")]}]
  ' <<<"$response"
else
  jq --arg cloud "$cloud_key" '
    [.gpus[]
      | select(.availability != "NONE")
      | {id, name, memory_gb: .memory, availability,
         hourly_rate_usd: .price[$cloud], available_count: .maxCount[$cloud],
         data_centers: [.dataCenters[]? | select(.availability != "NONE")]}]
    | sort_by(.hourly_rate_usd)
  ' <<<"$response"
fi
