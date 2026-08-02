#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

launch_id=''
task_tag=''
task_tag_set=0
name='oneshotsea-cpu'
instance_type=''
ami_id=''
subnet_id=''
security_group_id=''
iam_instance_profile=''
volume_gb=100
max_price=''
max_lifetime_minutes=''
associate_public_ip=0

usage() {
  cat <<'EOF'
Usage: provision.sh --launch-id ID --instance-type TYPE --ami-id AMI
       --subnet-id SUBNET --security-group-id SG
       --iam-instance-profile NAME
       --max-price-per-hour USD --max-lifetime-minutes N [options] [--execute]

Options:
  --name NAME                   Name tag (default: oneshotsea-cpu)
  --task-tag VALUE              Optional Task cost-allocation tag
  --instance-type TYPE          c8g.4xlarge or c8i.4xlarge; m8g.xlarge quota fallback
  --ami-id AMI                  Pinned us-east-2 Amazon Linux 2023 AMI
  --subnet-id SUBNET            Existing private subnet with SSM connectivity
  --security-group-id SG        Existing group; inbound SSH is not required
  --iam-instance-profile NAME   Existing profile with SSM managed-instance access
  --associate-public-ip         Explicit outbound Internet path for a public subnet
  --volume-gb N                 Encrypted root volume size (default: 100)
  --max-price-per-hour USD      Required current on-demand price ceiling
  --max-lifetime-minutes N      Required hard-stop timer (15..10080 minutes)
  --execute                     Validate and create one on-demand instance

Dry-run is the default and does not contact AWS or write operator state. There
is no EC2 key pair and the security group must have zero ingress; all control
uses keyless Systems Manager commands. Public addressing is opt-in only to
provide outbound package/source access in a public subnet. The instance has
IMDSv2 required, an encrypted delete-on-termination root disk, and a cloud-init
hard-stop timer backed by instance-initiated termination.
EOF
}

while (( $# )); do
  case "$1" in
    --launch-id) launch_id="${2:-}"; shift 2 ;;
    --task-tag) task_tag="${2:-}"; task_tag_set=1; shift 2 ;;
    --name) name="${2:-}"; shift 2 ;;
    --instance-type) instance_type="${2:-}"; shift 2 ;;
    --ami-id) ami_id="${2:-}"; shift 2 ;;
    --subnet-id) subnet_id="${2:-}"; shift 2 ;;
    --security-group-id) security_group_id="${2:-}"; shift 2 ;;
    --iam-instance-profile) iam_instance_profile="${2:-}"; shift 2 ;;
    --associate-public-ip) associate_public_ip=1; shift ;;
    --volume-gb) volume_gb="${2:-}"; shift 2 ;;
    --max-price-per-hour) max_price="${2:-}"; shift 2 ;;
    --max-lifetime-minutes) max_lifetime_minutes="${2:-}"; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

[[ -n "$launch_id" ]] || die '--launch-id is required'
validate_launch_id "$launch_id"
if (( task_tag_set == 1 )); then
  [[ "$task_tag" =~ ^[A-Za-z0-9][A-Za-z0-9._:/+=@-]{0,63}$ ]] ||
    die 'invalid task tag'
fi
[[ "$name" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,62}$ ]] || die 'invalid instance name'
validate_instance_type "$instance_type"
[[ "$ami_id" =~ ^ami-[0-9a-f]{8,17}$ ]] || die 'invalid AMI id'
[[ "$subnet_id" =~ ^subnet-[0-9a-f]{8,17}$ ]] || die 'invalid subnet id'
[[ "$security_group_id" =~ ^sg-[0-9a-f]{8,17}$ ]] || die 'invalid security-group id'
[[ "$iam_instance_profile" =~ ^[A-Za-z0-9+=,.@_-]{1,128}$ ]] ||
  die 'invalid IAM instance-profile name'
validate_positive_uint volume-gb "$volume_gb"
(( 10#$volume_gb >= 20 && 10#$volume_gb <= 2048 )) ||
  die 'volume-gb must be between 20 and 2048'
[[ "$max_price" =~ ^[0-9]+([.][0-9]+)?$ ]] || die 'invalid max hourly price'
validate_positive_uint max-lifetime-minutes "$max_lifetime_minutes"
(( 10#$max_lifetime_minutes >= 15 && 10#$max_lifetime_minutes <= 10080 )) ||
  die 'max-lifetime-minutes must be between 15 and 10080'
validate_region
expected_arch="$(expected_architecture "$instance_type")"

user_data="#!/usr/bin/env bash
set -euo pipefail
/usr/bin/systemd-run --unit=oneshotsea-hard-stop --on-active=${max_lifetime_minutes}m /usr/sbin/shutdown -h now
"

if ! require_execute; then
  print_cmd aws --no-cli-pager --region "$AWS_EC2_REGION" ec2 describe-images --image-ids "$ami_id"
  print_cmd aws --no-cli-pager --region "$AWS_EC2_REGION" ec2 describe-instance-types --instance-types "$instance_type"
  print_cmd aws --no-cli-pager --region "$AWS_EC2_REGION" ec2 describe-security-groups --group-ids "$security_group_id"
  printf 'DRY-RUN: query current Linux on-demand price for %s in us-east-2; require <= USD %s/hour\n' \
    "$instance_type" "$max_price"
  print_cmd aws --no-cli-pager --region "$AWS_EC2_REGION" ec2 run-instances \
    --image-id "$ami_id" --instance-type "$instance_type" --count 1 \
    --subnet-id "$subnet_id" --security-group-ids "$security_group_id" \
    --iam-instance-profile "Name=${iam_instance_profile}" \
    --instance-initiated-shutdown-behavior terminate \
    --metadata-options 'HttpTokens=required,HttpEndpoint=enabled,HttpPutResponseHopLimit=1' \
    --client-token "oneshotsea-${launch_id}" --user-data '[hard-stop timer; no secrets]'
  printf 'DRY-RUN: encrypted gp3 root volume=%s GiB, delete-on-termination=true, expected architecture=%s\n' \
    "$volume_gb" "$expected_arch"
  if [[ -n "$task_tag" ]]; then
    printf 'DRY-RUN: tag instance+root-volume Project=OneShotSEA LaunchId=%s Task=%s\n' \
      "$launch_id" "$task_tag"
  else
    printf 'DRY-RUN: tag instance+root-volume Project=OneShotSEA LaunchId=%s\n' \
      "$launch_id"
  fi
  if (( associate_public_ip == 1 )); then
    printf 'DRY-RUN: associate public IP for outbound-only access; require security group ingress=[]\n'
  else
    printf 'DRY-RUN: no public IP; subnet must provide NAT or SSM/VCS/package endpoints\n'
  fi
  exit 0
fi

require_cmd jq
hourly_price="$(lookup_hourly_price "$instance_type")"
jq -en --argjson price "$hourly_price" --argjson ceiling "$max_price" \
  '$price <= $ceiling' >/dev/null ||
  die "current price USD ${hourly_price}/hour exceeds ceiling USD ${max_price}/hour"

image_response="$(aws_cli ec2 describe-images --image-ids "$ami_id")"
image="$(jq -cer '.Images | if length == 1 then .[0] else error("expected one AMI") end' \
  <<<"$image_response")" || die 'AMI was not found in us-east-2'
[[ "$(jq -r '.State' <<<"$image")" == available ]] || die 'AMI is not available'
[[ "$(jq -r '.OwnerId' <<<"$image")" == 137112412989 &&
   "$(jq -r '.Name // ""' <<<"$image")" == al2023-ami-* ]] ||
  die 'AMI must be an official Amazon Linux 2023 image owned by Amazon'
[[ "$(jq -r '.Architecture' <<<"$image")" == "$expected_arch" ]] ||
  die "AMI architecture does not match ${instance_type} (${expected_arch})"
root_device="$(jq -er '.RootDeviceName | strings' <<<"$image")" ||
  die 'AMI has no root device name'
[[ "$root_device" =~ ^/dev/[A-Za-z0-9]+$ ]] || die 'AMI returned an unsafe root device name'

type_response="$(aws_cli ec2 describe-instance-types --instance-types "$instance_type")"
jq -e --arg arch "$expected_arch" '
  .InstanceTypes | length == 1 and
  (.[0].ProcessorInfo.SupportedArchitectures | index($arch) != null)
' <<<"$type_response" >/dev/null || die 'instance type architecture metadata did not match'

subnet_response="$(aws_cli ec2 describe-subnets --subnet-ids "$subnet_id")"
subnet_vpc="$(jq -er '.Subnets | if length == 1 then .[0].VpcId else error("expected one subnet") end' \
  <<<"$subnet_response")" || die 'subnet was not found in us-east-2'
security_group_response="$(aws_cli ec2 describe-security-groups --group-ids "$security_group_id")"
jq -e --arg vpc "$subnet_vpc" '
  .SecurityGroups | length == 1 and .[0].VpcId == $vpc and
  (.[0].IpPermissions | length == 0)
' <<<"$security_group_response" >/dev/null ||
  die 'security group must be in the selected VPC and have zero ingress rules'

block_devices="$(jq -cn --arg device "$root_device" --argjson size "$volume_gb" '
  [{DeviceName: $device, Ebs: {
    DeleteOnTermination: true, Encrypted: true,
    VolumeSize: $size, VolumeType: "gp3"
  }}]
')"
tags="$(jq -cn \
  --arg name "$name" --arg launch "$launch_id" --arg task "$task_tag" \
  --arg lifetime "$max_lifetime_minutes" '
  def task_tag: if $task == "" then [] else [{Key: "Task", Value: $task}] end;
  [{ResourceType: "instance", Tags: ([
    {Key: "Name", Value: $name},
    {Key: "Project", Value: "OneShotSEA"},
    {Key: "LaunchId", Value: $launch},
    {Key: "MaxLifetimeMinutes", Value: $lifetime}
  ] + task_tag)}, {ResourceType: "volume", Tags: ([
    {Key: "Project", Value: "OneShotSEA"},
    {Key: "LaunchId", Value: $launch}
  ] + task_tag)}]
')"

public_ip_flag=--no-associate-public-ip-address
if (( associate_public_ip == 1 )); then
  public_ip_flag=--associate-public-ip-address
fi
response="$(aws_cli ec2 run-instances \
  --image-id "$ami_id" --instance-type "$instance_type" --count 1 \
  --subnet-id "$subnet_id" --security-group-ids "$security_group_id" \
  --iam-instance-profile "Name=${iam_instance_profile}" "$public_ip_flag" \
  --instance-initiated-shutdown-behavior terminate \
  --metadata-options 'HttpTokens=required,HttpEndpoint=enabled,HttpPutResponseHopLimit=1' \
  --block-device-mappings "$block_devices" --tag-specifications "$tags" \
  --ebs-optimized --client-token "oneshotsea-${launch_id}" \
  --user-data "$user_data")"

instance_count="$(jq -r '.Instances | length' <<<"$response")"
returned_id="$(jq -r '.Instances[0].InstanceId // ""' <<<"$response")"
returned_type="$(jq -r '.Instances[0].InstanceType // ""' <<<"$response")"
if [[ "$instance_count" != 1 || ! "$returned_id" =~ ^i-[0-9a-f]{8,17}$ ||
      "$returned_type" != "$instance_type" ]]; then
  if [[ "$returned_id" =~ ^i-[0-9a-f]{8,17}$ ]]; then
    note "unexpected launch response; attempting immediate termination of ${returned_id}"
    aws_cli ec2 terminate-instances --instance-ids "$returned_id" >/dev/null ||
      note "URGENT: automatic termination failed; terminate ${returned_id} in the AWS console"
  fi
  die 'AWS returned an unexpected instance launch response'
fi

state_path="$(save_instance_state "$response" "$hourly_price")"
jq --argjson price "$hourly_price" --arg arch "$expected_arch" '
  .Instances[0]
  | {instance_id: .InstanceId, state: .State.Name, instance_type: .InstanceType,
     image_id: .ImageId, architecture: $arch, launch_time: .LaunchTime,
     on_demand_hourly_rate_usd: $price, region: "us-east-2"}
' <<<"$response"
printf 'state: %s\n' "$state_path" >&2
