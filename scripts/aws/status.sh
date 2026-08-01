#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

instance_id=''
remote=0
run_id=''

usage() {
  cat <<'EOF'
Usage: status.sh [--instance-id ID] [--remote --run-id RUN] [--execute]

Reports allowlisted EC2 resource/cost state. Optional remote status inspects
tmux and compact run artifacts through keyless SSM. Dry-run contacts nothing.
EOF
}

while (( $# )); do
  case "$1" in
    --instance-id) instance_id="${2:-}"; shift 2 ;;
    --remote) remote=1; shift ;;
    --run-id) run_id="${2:-}"; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

instance_id="$(load_instance_id "$instance_id")"
if (( remote == 1 )); then
  [[ -n "$run_id" ]] || die '--remote requires --run-id'
  validate_run_id "$run_id"
elif [[ -n "$run_id" ]]; then
  die '--run-id requires --remote'
fi
validate_remote_root

if ! require_execute; then
  print_cmd aws --no-cli-pager --region "$AWS_EC2_REGION" ec2 describe-instances --instance-ids "$instance_id"
  if (( remote == 1 )); then
    printf 'DRY-RUN: inspect tmux and %s/runs/%s/worker-* through SSM\n' "$AWS_REMOTE_ROOT" "$run_id"
  fi
  exit 0
fi

response="$(aws_cli ec2 describe-instances --instance-ids "$instance_id")"
state_path="$(save_instance_state "$response")"
jq '
  .Reservations[0].Instances[0]
  | {instance_id: .InstanceId, state: .State.Name, instance_type: .InstanceType,
     image_id: .ImageId, launch_time: .LaunchTime,
     availability_zone: .Placement.AvailabilityZone,
     public_ip_present: (.PublicIpAddress != null),
     iam_instance_profile: (.IamInstanceProfile.Arn // null)}
' <<<"$response"
printf 'state: %s\n' "$state_path" >&2

if (( remote == 1 )); then
  ssm_require_online "$instance_id"
  # shellcheck disable=SC2016
  printf -v remote_command \
    'set -euo pipefail; root=%q; run_id=%q; tmux list-sessions 2>/dev/null || true; find "$root/runs/$run_id" -maxdepth 2 -type f -printf "%%p %%s bytes\\n" 2>/dev/null | sort; for path in "$root/runs/$run_id"/worker-*/progress.jsonl; do [[ -f "$path" ]] && tail -n 2 "$path"; done' \
    "$AWS_REMOTE_ROOT" "$run_id"
  ssm_run_command "$instance_id" 'OneShotSEA worker status' "$remote_command" 300
fi
