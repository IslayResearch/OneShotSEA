#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

instance_id=''
while (( $# )); do
  case "$1" in
    --instance-id) instance_id="${2:-}"; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help)
      echo 'Usage: terminate.sh [--instance-id ID] [--execute]'; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done
instance_id="$(load_instance_id "$instance_id")"

if ! require_execute; then
  print_cmd aws --no-cli-pager --region "$AWS_EC2_REGION" ec2 \
    terminate-instances --instance-ids "$instance_id"
  printf 'DRY-RUN: wait for terminated state; root EBS is delete-on-termination, so fetch artifacts first\n'
  exit 0
fi

current="$(aws_cli ec2 describe-instances --instance-ids "$instance_id")"
current_state="$(jq -r '.Reservations[0].Instances[0].State.Name' <<<"$current")"
save_instance_state "$current" >/dev/null
if [[ "$current_state" == terminated ]]; then
  note "instance ${instance_id} is already terminated"
else
  aws_cli ec2 terminate-instances --instance-ids "$instance_id" \
    | jq '{terminating: [.TerminatingInstances[] | {instance_id: .InstanceId,
          previous_state: .PreviousState.Name, current_state: .CurrentState.Name}]}'
  aws_cli ec2 wait instance-terminated --instance-ids "$instance_id"
fi
final="$(aws_cli ec2 describe-instances --instance-ids "$instance_id")"
state_path="$(save_instance_state "$final")"
jq '.Reservations[0].Instances[0]
    | {instance_id: .InstanceId, state: .State.Name, launch_time: .LaunchTime}' \
  <<<"$final"
printf 'state: %s\n' "$state_path" >&2
