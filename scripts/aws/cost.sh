#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

start=''
end=''
task=''

usage() {
  cat <<'EOF'
Usage: cost.sh --start YYYY-MM-DD --end YYYY-MM-DD [--task TAG] [--execute]

Reports daily AWS Cost Explorer charges grouped by service for resources tagged
`Project=OneShotSEA`. With `--task`, also filters on the separately activated
`Task` cost-allocation tag. The end date is exclusive. Cost Explorer data can lag
behind live instance usage; worker fetch metadata also records an immediate
launch-to-fetch upper-bound estimate. Dry-run is the default.
EOF
}

while (( $# )); do
  case "$1" in
    --start) start="${2:-}"; shift 2 ;;
    --end) end="${2:-}"; shift 2 ;;
    --task) task="${2:-}"; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

date_re='^[0-9]{4}-[0-9]{2}-[0-9]{2}$'
[[ "$start" =~ $date_re && "$end" =~ $date_re && "$start" < "$end" ]] ||
  die 'require valid --start and later exclusive --end dates (YYYY-MM-DD)'
if [[ -n "$task" ]]; then
  require_cmd jq
  [[ "$task" =~ ^[A-Za-z0-9][A-Za-z0-9._:/+=@-]{0,63}$ ]] ||
    die 'invalid Task tag'
fi

filter='{"Tags":{"Key":"Project","Values":["OneShotSEA"],"MatchOptions":["EQUALS"]}}'
if [[ -n "$task" ]]; then
  filter="$(jq -cn --arg task "$task" '{And:[
    {Tags:{Key:"Project",Values:["OneShotSEA"],MatchOptions:["EQUALS"]}},
    {Tags:{Key:"Task",Values:[$task],MatchOptions:["EQUALS"]}}
  ]}')"
fi
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
  | jq --arg task "$task" '{project:"OneShotSEA", task:(if $task == "" then null else $task end),
      tag_keys:(if $task == "" then ["Project"] else ["Project","Task"] end), results_by_time:
      [.ResultsByTime[] | {time_period:.TimePeriod, estimated:.Estimated,
       services:[.Groups[] | {service:.Keys[0], metrics:.Metrics}], total:.Total}]}'
