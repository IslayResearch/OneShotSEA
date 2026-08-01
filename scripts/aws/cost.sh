#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

start=''
end=''

usage() {
  cat <<'EOF'
Usage: cost.sh --start YYYY-MM-DD --end YYYY-MM-DD [--execute]

Reports daily AWS Cost Explorer charges grouped by service for resources tagged
`Project=OneShotSEA`. The end date is exclusive. Cost Explorer data can lag
behind live instance usage; worker fetch metadata also records an immediate
launch-to-fetch upper-bound estimate. Dry-run is the default.
EOF
}

while (( $# )); do
  case "$1" in
    --start) start="${2:-}"; shift 2 ;;
    --end) end="${2:-}"; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

date_re='^[0-9]{4}-[0-9]{2}-[0-9]{2}$'
[[ "$start" =~ $date_re && "$end" =~ $date_re && "$start" < "$end" ]] ||
  die 'require valid --start and later exclusive --end dates (YYYY-MM-DD)'

filter='{"Tags":{"Key":"Project","Values":["OneShotSEA"],"MatchOptions":["EQUALS"]}}'
if ! require_execute; then
  print_cmd aws --no-cli-pager ce get-cost-and-usage \
    --time-period "Start=${start},End=${end}" --granularity DAILY \
    --metrics UnblendedCost AmortizedCost \
    --filter "$filter" --group-by Type=DIMENSION,Key=SERVICE
  exit 0
fi

aws_cli_region us-east-1 ce get-cost-and-usage \
  --time-period "Start=${start},End=${end}" --granularity DAILY \
  --metrics UnblendedCost AmortizedCost \
  --filter "$filter" --group-by Type=DIMENSION,Key=SERVICE \
  | jq '{project:"OneShotSEA", tag_key:"Project", results_by_time:
      [.ResultsByTime[] | {time_period:.TimePeriod, estimated:.Estimated,
       services:[.Groups[] | {service:.Keys[0], metrics:.Metrics}], total:.Total}]}'
