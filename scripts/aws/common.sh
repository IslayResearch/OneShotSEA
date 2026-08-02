#!/usr/bin/env bash

# Shared helpers for the AWS EC2 operator scripts. This file is sourced.

set -euo pipefail

AWS_EC2_REGION="${ONESHOTSEA_AWS_REGION:-us-east-2}"
AWS_STATE_DIR="${ONESHOTSEA_AWS_STATE_DIR:-${XDG_STATE_HOME:-${HOME}/.local/state}/oneshotsea/aws}"
AWS_REMOTE_ROOT="${ONESHOTSEA_AWS_REMOTE_ROOT:-/opt/oneshotsea}"
EXECUTE=0

die() {
  printf 'error: %s\n' "$*" >&2
  exit 2
}

note() {
  printf '%s\n' "$*" >&2
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

print_cmd() {
  local arg
  printf 'DRY-RUN:'
  for arg in "$@"; do
    printf ' %q' "$arg"
  done
  printf '\n'
}

require_execute() {
  if (( EXECUTE == 0 )); then
    note 'dry-run only; pass --execute to perform this operation'
    return 1
  fi
  return 0
}

reject_xtrace() {
  [[ "$-" != *x* ]] ||
    die 'refusing to use the AWS credential chain while shell xtrace is enabled'
}

validate_region() {
  [[ "$AWS_EC2_REGION" == us-east-2 ]] ||
    die 'OneShotSEA AWS workers are restricted to us-east-2'
}

validate_instance_type() {
  case "$1" in
    c8g.4xlarge|c8i.4xlarge|m8g.xlarge) ;;
    *) die 'instance type must be c8g.4xlarge, c8i.4xlarge, or the quota fallback m8g.xlarge' ;;
  esac
}

expected_architecture() {
  case "$1" in
    c8g.4xlarge|m8g.xlarge) printf 'arm64\n' ;;
    c8i.4xlarge) printf 'x86_64\n' ;;
    *) die "unsupported instance type: $1" ;;
  esac
}

validate_instance_id() {
  [[ "$1" =~ ^i-[0-9a-f]{8,17}$ ]] || die 'invalid EC2 instance id'
}

validate_run_id() {
  [[ "$1" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ ]] || die 'invalid run id'
}

validate_launch_id() {
  [[ "$1" =~ ^[A-Za-z0-9][A-Za-z0-9-]{0,47}$ ]] || die 'invalid launch id'
}

validate_uint() {
  [[ "$2" =~ ^[0-9]+$ ]] || die "$1 must be an unsigned decimal integer"
}

validate_positive_uint() {
  validate_uint "$1" "$2"
  [[ "$2" =~ [1-9] ]] || die "$1 must be positive"
}

is_zero_uint() {
  [[ "$1" =~ ^0+$ ]]
}

validate_remote_root() {
  if [[ -n "${ONESHOTSEA_AWS_TEST_REMOTE_ROOT:-}" ]]; then
    [[ "${AWS_INSTANCE_ID:-}" == i-00000000000000000 ]] ||
      die 'test remote roots require the reserved mock instance id'
    [[ "$AWS_REMOTE_ROOT" == "$ONESHOTSEA_AWS_TEST_REMOTE_ROOT" ]] ||
      die 'ONESHOTSEA_AWS_REMOTE_ROOT must equal ONESHOTSEA_AWS_TEST_REMOTE_ROOT in test mode'
    [[ "$AWS_REMOTE_ROOT" =~ ^/[A-Za-z0-9._/+~-]+$ ]] ||
      die 'test remote root must be a simple absolute path'
  else
    [[ "$AWS_REMOTE_ROOT" =~ ^/opt/[A-Za-z0-9._/-]+$ ]] ||
      die 'remote root must be a simple absolute path below /opt'
  fi
  [[ "$AWS_REMOTE_ROOT" != *'/../'* && "$AWS_REMOTE_ROOT" != */.. ]] ||
    die 'remote root may not contain parent traversal'
}

# Invoke the AWS CLI without exposing credentials as arguments. AWS resolves its
# normal environment/config/profile/role credential chain itself. Scripts never
# accept access keys or session tokens as command-line arguments.
aws_cli_region() {
  local region="$1"
  shift
  require_cmd aws
  reject_xtrace
  AWS_PAGER='' AWS_CLI_AUTO_PROMPT=off \
    aws --no-cli-pager --region "$region" "$@"
}

aws_cli() {
  validate_region
  aws_cli_region "$AWS_EC2_REGION" "$@"
}

load_instance_id() {
  local explicit="${1:-}"
  local instance_id="$explicit"
  if [[ -z "$instance_id" ]]; then
    instance_id="${AWS_INSTANCE_ID:-}"
  fi
  [[ -n "$instance_id" ]] ||
    die 'provide --instance-id or set AWS_INSTANCE_ID'
  validate_instance_id "$instance_id"
  printf '%s\n' "$instance_id"
}

instance_state_path() {
  local instance_id="$1"
  validate_instance_id "$instance_id"
  printf '%s/instances/%s.json\n' "$AWS_STATE_DIR" "$instance_id"
}

# Save only an allowlisted, non-secret part of DescribeInstances/RunInstances.
# Tag values, user-data, block-device details, and IAM credentials are excluded.
save_instance_state() {
  local response="$1"
  local price="${2:-null}"
  local instance instance_id instance_type architecture state_path state_tmp old_price='null'
  require_cmd jq
  instance="$(jq -cer '
    if (.Reservations? | type) == "array" then .Reservations[0].Instances[0]
    elif (.Instances? | type) == "array" then .Instances[0]
    else . end
  ' <<<"$response")" || die 'AWS response has no instance'
  instance_id="$(jq -er '.InstanceId | strings' <<<"$instance")" ||
    die 'AWS response has no instance id'
  validate_instance_id "$instance_id"
  state_path="$(instance_state_path "$instance_id")"
  umask 077
  mkdir -p "$(dirname "$state_path")"
  chmod 700 "$(dirname "$state_path")"
  if [[ -f "$state_path" ]]; then
    old_price="$(jq -r '.on_demand_hourly_rate_usd // null' "$state_path")"
  fi
  [[ "$price" != null ]] || price="$old_price"
  state_tmp="${state_path}.tmp.$$"
  instance_type="$(jq -er '.InstanceType | strings' <<<"$instance")" ||
    die 'AWS response has no instance type'
  architecture="$(expected_architecture "$instance_type")"
  jq --arg region "$AWS_EC2_REGION" \
     --arg checked_at "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
     --arg architecture "$architecture" \
     --argjson price "$price" '
    {
      schema: "oneshotsea.aws-instance.v1",
      region: $region,
      instance_id: .InstanceId,
      instance_type: .InstanceType,
      image_id: .ImageId,
      architecture: $architecture,
      availability_zone: (.Placement.AvailabilityZone // null),
      lifecycle: (.InstanceLifecycle // "on-demand"),
      launch_time: (.LaunchTime // null),
      state: (.State.Name // null),
      private_dns_name: (.PrivateDnsName // null),
      private_ip_address: (.PrivateIpAddress // null),
      iam_instance_profile_arn: (.IamInstanceProfile.Arn // null),
      on_demand_hourly_rate_usd: $price,
      last_checked_at: $checked_at
    }
  ' <<<"$instance" >"$state_tmp"
  mv -f -- "$state_tmp" "$state_path"
  printf '%s\n' "$state_path"
}

lookup_hourly_price() {
  local instance_type="$1"
  local response
  validate_instance_type "$instance_type"
  response="$(aws_cli_region us-east-1 pricing get-products \
    --service-code AmazonEC2 \
    --filters \
      "Type=TERM_MATCH,Field=instanceType,Value=${instance_type}" \
      'Type=TERM_MATCH,Field=location,Value=US East (Ohio)' \
      'Type=TERM_MATCH,Field=operatingSystem,Value=Linux' \
      'Type=TERM_MATCH,Field=tenancy,Value=Shared' \
      'Type=TERM_MATCH,Field=preInstalledSw,Value=NA' \
      'Type=TERM_MATCH,Field=capacitystatus,Value=Used' \
    --format-version aws_v1)"
  jq -er '
    [.PriceList[] | fromjson
      | select(.product.productFamily == "Compute Instance")
      | .terms.OnDemand[] | .priceDimensions[]
      | .pricePerUnit.USD | tonumber | select(. > 0)]
    | unique
    | if length == 1 then .[0]
      else error("expected exactly one Linux on-demand hourly rate") end
  ' <<<"$response"
}

ssm_require_online() {
  local instance_id="$1" response
  validate_instance_id "$instance_id"
  response="$(aws_cli ssm describe-instance-information \
    --filters "Key=InstanceIds,Values=${instance_id}")"
  jq -e --arg id "$instance_id" '
    .InstanceInformationList | length == 1 and
    .[0].InstanceId == $id and .[0].PingStatus == "Online"
  ' <<<"$response" >/dev/null || die "instance ${instance_id} is not online in Systems Manager"
}

# Execute one fixed, non-secret shell program through AWS-RunShellScript and
# wait for its terminal result. Callers construct commands only from validated
# values; this helper never accepts or logs credentials.
ssm_run_command() {
  local instance_id="$1" comment="$2" command="$3"
  local timeout_seconds="${4:-3600}"
  local parameters response command_id invocation='' status='Pending'
  local started_epoch deadline now
  local comment_re='^[A-Za-z0-9._ -]{1,100}$'
  validate_instance_id "$instance_id"
  validate_positive_uint timeout-seconds "$timeout_seconds"
  (( 10#$timeout_seconds <= 2592000 )) || die 'SSM timeout may not exceed 2592000 seconds'
  [[ "$comment" =~ $comment_re ]] || die 'invalid SSM command comment'
  require_cmd jq
  parameters="$(jq -cn --arg command "$command" '{commands: [$command]}')"
  response="$(aws_cli ssm send-command --instance-ids "$instance_id" \
    --document-name AWS-RunShellScript --comment "$comment" \
    --timeout-seconds "$timeout_seconds" --parameters "$parameters")"
  command_id="$(jq -er '.Command.CommandId | strings' <<<"$response")" ||
    die 'SSM returned no command id'
  [[ "$command_id" =~ ^[0-9a-f-]{8,64}$ ]] || die 'SSM returned an invalid command id'
  started_epoch="$(date +%s)"
  deadline=$(( started_epoch + 10#$timeout_seconds + 120 ))
  while :; do
    if invocation="$(aws_cli ssm get-command-invocation \
        --command-id "$command_id" --instance-id "$instance_id" 2>/dev/null)"; then
      status="$(jq -r '.Status // "Unknown"' <<<"$invocation")"
      case "$status" in
        Success|Cancelled|Cancelling|Failed|TimedOut|Undeliverable|Terminated)
          break
          ;;
        Pending|InProgress|Delayed) ;;
        *) die "SSM command ${command_id} returned unknown status ${status}" ;;
      esac
    fi
    now="$(date +%s)"
    (( now < deadline )) || die "timed out waiting for SSM command ${command_id}"
    sleep 5
  done
  if [[ "$status" != Success ]]; then
    jq '{CommandId, Status, StatusDetails, StandardOutputContent, StandardErrorContent}' \
      <<<"$invocation" >&2
    die "SSM command ${command_id} failed with status ${status}"
  fi
  jq -r '.StandardOutputContent // ""' <<<"$invocation"
}
