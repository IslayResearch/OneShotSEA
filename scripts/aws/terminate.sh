#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

instance_id=''
launch_id=''
task_tag=''
fetched_run=''
fetch_directory=''
allow_unfetched=0
while (( $# )); do
  case "$1" in
    --instance-id) instance_id="${2:-}"; shift 2 ;;
    --launch-id) launch_id="${2:-}"; shift 2 ;;
    --task-tag) task_tag="${2:-}"; shift 2 ;;
    --fetched-run) fetched_run="${2:-}"; shift 2 ;;
    --fetch-directory) fetch_directory="${2:-}"; shift 2 ;;
    --allow-unfetched) allow_unfetched=1; shift ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help)
      echo 'Usage: terminate.sh --launch-id ID [--task-tag TAG] (--fetched-run RUN [--fetch-directory DIR]|--allow-unfetched) [--instance-id ID] [--execute]'; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done
instance_id="$(load_instance_id "$instance_id")"
[[ -n "$launch_id" ]] || die '--launch-id is required for destructive target identity'
validate_launch_id "$launch_id"
if [[ -n "$task_tag" ]]; then
  [[ "$task_tag" =~ ^[A-Za-z0-9][A-Za-z0-9._:/+=@-]{0,63}$ ]] || die 'invalid Task tag'
fi
if [[ -n "$fetched_run" ]]; then
  validate_run_id "$fetched_run"
fi
if [[ -n "$fetch_directory" && -z "$fetched_run" ]]; then
  die '--fetch-directory requires --fetched-run'
fi
if [[ -n "$fetched_run" && "$allow_unfetched" == 1 ]] ||
   [[ -z "$fetched_run" && "$allow_unfetched" == 0 ]]; then
  die 'choose exactly one of --fetched-run or --allow-unfetched'
fi

if ! require_execute; then
  print_cmd aws --no-cli-pager --region "$AWS_EC2_REGION" ec2 \
    terminate-instances --instance-ids "$instance_id"
  printf 'DRY-RUN: require Project=OneShotSEA LaunchId=%s' "$launch_id"
  [[ -z "$task_tag" ]] || printf ' Task=%s' "$task_tag"
  printf '; verify fetched artifacts unless --allow-unfetched was explicit\n'
  exit 0
fi

if [[ -n "$fetched_run" ]]; then
  fetch_dir="${fetch_directory:-${PROJECT_ROOT}/artifacts/aws/${fetched_run}/${instance_id}}"
  [[ "$fetch_dir" == /* ]] || fetch_dir="$(pwd)/${fetch_dir}"
  [[ "$fetch_dir" != / && "$fetch_dir" != "$HOME" && "$fetch_dir" != "$PROJECT_ROOT" ]] ||
    die 'refusing a broad fetch-evidence directory'
  fetch_metadata="${fetch_dir}/fetch-metadata.json"
  fetch_checksums="${fetch_dir}/SHA256SUMS"
  retrieval_archive="${fetch_dir}/retrieval.tar.gz"
  [[ -f "$fetch_metadata" && ! -L "$fetch_metadata" &&
     -f "$fetch_checksums" && ! -L "$fetch_checksums" &&
     -f "$retrieval_archive" && ! -L "$retrieval_archive" ]] ||
    die "successful fetch evidence is missing for ${fetched_run}"
  jq -e --arg instance "$instance_id" --arg run "$fetched_run" '
    .schema == "oneshotsea.aws-fetch.v1" and .instance_id == $instance and
    .run_id == $run and (.remote_archive_sha256 | test("^[0-9a-f]{64}$"))
  ' "$fetch_metadata" >/dev/null || die 'fetch metadata does not match termination target'
  remote_archive_sha="$(jq -er '.remote_archive_sha256' "$fetch_metadata")" ||
    die 'fetch metadata has no remote archive digest'
  actual_archive_sha="$(shasum -a 256 "$retrieval_archive" | awk '{print $1}')"
  [[ "$actual_archive_sha" == "$remote_archive_sha" ]] ||
    die 'retrieval archive does not match the authenticated remote digest'
  if ! manifest_archive_sha="$(awk '
    $2 == "retrieval.tar.gz" || $2 == "./retrieval.tar.gz" { count++; digest = $1 }
    END { if (count != 1) exit 1; print digest }
  ' "$fetch_checksums")"; then
    die 'SHA256SUMS must contain exactly one retrieval archive entry'
  fi
  [[ "$manifest_archive_sha" == "$actual_archive_sha" ]] ||
    die 'SHA256SUMS retrieval archive digest does not match the fetched bytes'
  (cd "$fetch_dir" && shasum -a 256 -c SHA256SUMS >/dev/null) ||
    die 'fetched artifact checksums failed before termination'
fi

current="$(aws_cli ec2 describe-instances --instance-ids "$instance_id")"
instance_json="$(jq -cer '.Reservations[0].Instances[0]' <<<"$current")" ||
  die 'AWS response has no termination target'
jq -e --arg launch "$launch_id" --arg task "$task_tag" '
  def tag($key): [.Tags[]? | select(.Key == $key) | .Value] |
    if length == 1 then .[0] else null end;
  tag("Project") == "OneShotSEA" and tag("LaunchId") == $launch and
  ($task == "" or tag("Task") == $task)
' <<<"$instance_json" >/dev/null ||
  die 'instance Project/LaunchId/Task tags do not match the destructive request'
instance_type="$(jq -er '.InstanceType' <<<"$instance_json")"
validate_instance_type "$instance_type"
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
