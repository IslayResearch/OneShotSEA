#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

usage() {
  cat <<'EOF'
Usage: catalog.sh [--execute]

Reports the two approved us-east-2 CPU candidates and their current Linux
on-demand prices. Dry-run is the default and does not contact AWS.
EOF
}

while (( $# )); do
  case "$1" in
    --execute) EXECUTE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

validate_region
candidates=(c8g.4xlarge c8i.4xlarge m8g.xlarge)

if ! require_execute; then
  print_cmd aws --no-cli-pager --region "$AWS_EC2_REGION" ec2 \
    describe-instance-types --instance-types "${candidates[@]}"
  for candidate in "${candidates[@]}"; do
    printf 'DRY-RUN: query current Linux on-demand price for %s in us-east-2\n' "$candidate"
  done
  exit 0
fi

metadata="$(aws_cli ec2 describe-instance-types --instance-types "${candidates[@]}")"
records=()
for candidate in "${candidates[@]}"; do
  price="$(lookup_hourly_price "$candidate")"
  record="$(jq -cer --arg type "$candidate" --argjson price "$price" '
    first(.InstanceTypes[] | select(.InstanceType == $type))
    | {
        instance_type: .InstanceType,
        architecture: .ProcessorInfo.SupportedArchitectures[0],
        vcpus: .VCpuInfo.DefaultVCpus,
        memory_mib: .MemoryInfo.SizeInMiB,
        network_performance: .NetworkInfo.NetworkPerformance,
        on_demand_hourly_rate_usd: $price,
        region: "us-east-2"
      }
  ' <<<"$metadata")" || die "AWS returned no metadata for ${candidate}"
  records+=("$record")
done
printf '%s\n' "${records[@]}" | jq -s '.'
