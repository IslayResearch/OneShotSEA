#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

instance_id=''
run_id=''
destination=''
remote_port=18765
local_port=18766

usage() {
  cat <<'EOF'
Usage: fetch.sh --run-id RUN [--instance-id ID] [--destination DIR]
                [--remote-port N] [--local-port N] [--execute]

Archives manifests, logs, checkpoints, resource records, and certificates on
the instance, then downloads the archive through an SSM port-forwarding
session. No SSH key, public ingress, or S3 bucket is needed. Source artifacts
remain on the instance. Dry-run is the default.
EOF
}

while (( $# )); do
  case "$1" in
    --instance-id) instance_id="${2:-}"; shift 2 ;;
    --run-id) run_id="${2:-}"; shift 2 ;;
    --destination) destination="${2:-}"; shift 2 ;;
    --remote-port) remote_port="${2:-}"; shift 2 ;;
    --local-port) local_port="${2:-}"; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

instance_id="$(load_instance_id "$instance_id")"
[[ -n "$run_id" ]] || die '--run-id is required'
validate_run_id "$run_id"
validate_positive_uint remote-port "$remote_port"
validate_positive_uint local-port "$local_port"
(( 10#$remote_port >= 1024 && 10#$remote_port <= 65535 )) || die 'remote port is out of range'
(( 10#$local_port >= 1024 && 10#$local_port <= 65535 )) || die 'local port is out of range'
validate_remote_root
if [[ -z "$destination" ]]; then
  destination="${PROJECT_ROOT}/artifacts/aws/${run_id}/${instance_id}"
fi
[[ "$destination" == /* ]] || destination="$(pwd)/${destination}"
[[ "$destination" != / && "$destination" != "$HOME" && "$destination" != "$PROJECT_ROOT" ]] ||
  die 'refusing a broad fetch destination'

if ! require_execute; then
  printf 'DRY-RUN: archive %s/runs/%s without deleting source artifacts\n' "$AWS_REMOTE_ROOT" "$run_id"
  print_cmd aws --no-cli-pager --region "$AWS_EC2_REGION" ssm start-session \
    --target "$instance_id" --document-name AWS-StartPortForwardingSession \
    --parameters "portNumber=${remote_port},localPortNumber=${local_port}"
  printf 'DRY-RUN: download through 127.0.0.1:%s, verify remote SHA-256, safely extract to %s\n' \
    "$local_port" "$destination"
  exit 0
fi

require_cmd curl
require_cmd python3
require_cmd shasum
ssm_require_online "$instance_id"

# The HTTP server binds only loopback and serves a single export directory. It
# is unreachable through the security group; the local SSM tunnel is the only
# network path. A later SSM command stops it after transfer.
# shellcheck disable=SC2016
remote_prepare='set -euo pipefail
root=$1; run_id=$2; port=$3
run_dir="$root/runs/$run_id"; export_dir="$root/exports"
[[ -d "$run_dir" ]] || { echo "error: run directory does not exist" >&2; exit 2; }
mkdir -p "$export_dir"
archive="$export_dir/$run_id.tar.gz"; tmp="$archive.tmp.$$"
tar -czf "$tmp" -C "$root/runs" "$run_id"
mv -f -- "$tmp" "$archive"
sha=$(sha256sum "$archive" | awk "{print \$1}")
pid_file="$export_dir/http-$port.pid"
if [[ -f "$pid_file" ]] && kill -0 "$(cat "$pid_file")" 2>/dev/null; then
  echo "error: export server already active on requested port" >&2; exit 2
fi
nohup python3 -m http.server "$port" --bind 127.0.0.1 --directory "$export_dir" \
  </dev/null >"$export_dir/http-$port.log" 2>&1 &
echo $! >"$pid_file"
printf "archive_sha256=%s\narchive_name=%s.tar.gz\n" "$sha" "$run_id"
'
printf -v prepare_command 'bash -c %q -- %q %q %q' \
  "$remote_prepare" "$AWS_REMOTE_ROOT" "$run_id" "$remote_port"
prepare_output="$(ssm_run_command "$instance_id" 'OneShotSEA prepare artifact export' "$prepare_command" 3600)"
archive_sha="$(sed -n 's/^archive_sha256=//p' <<<"$prepare_output" | tail -n 1)"
[[ "$archive_sha" =~ ^[0-9a-f]{64}$ ]] || die 'remote export returned no valid SHA-256'

mkdir -p "$destination"
archive_tmp="${destination}/retrieval.tar.gz.tmp.$$"
session_log="${destination}/ssm-port-forward.log"
session_pid=''
cleanup_local() {
  if [[ -n "$session_pid" ]] && kill -0 "$session_pid" 2>/dev/null; then
    kill "$session_pid" 2>/dev/null || true
    wait "$session_pid" 2>/dev/null || true
  fi
  if [[ -f "$archive_tmp" && "$archive_tmp" == "${destination}/retrieval.tar.gz.tmp."* ]]; then
    rm -f -- "$archive_tmp"
  fi
}
trap cleanup_local EXIT

parameters="$(jq -cn --arg port "$remote_port" --arg local "$local_port" \
  '{portNumber: [$port], localPortNumber: [$local]}')"
aws_cli ssm start-session --target "$instance_id" \
  --document-name AWS-StartPortForwardingSession --parameters "$parameters" \
  >"$session_log" 2>&1 &
session_pid=$!
curl --fail --silent --show-error --retry 20 --retry-connrefused \
  --retry-delay 1 --max-time 120 \
  --output "$archive_tmp" "http://127.0.0.1:${local_port}/${run_id}.tar.gz"

actual_sha="$(shasum -a 256 "$archive_tmp" | awk '{print $1}')"
[[ "$actual_sha" == "$archive_sha" ]] || die 'downloaded artifact digest does not match remote export'
mv -f -- "$archive_tmp" "${destination}/retrieval.tar.gz"

python3 - "${destination}/retrieval.tar.gz" "$destination" "$run_id" <<'PY'
import pathlib
import sys
import tarfile

archive, destination, run_id = sys.argv[1:]
prefix = f"{run_id}/"
with tarfile.open(archive, "r:gz") as bundle:
    members = bundle.getmembers()
    for member in members:
        path = pathlib.PurePosixPath(member.name)
        if path.is_absolute() or ".." in path.parts:
            raise SystemExit("error: unsafe path in artifact archive")
        if member.name != run_id and not member.name.startswith(prefix):
            raise SystemExit("error: artifact archive escaped its run directory")
        if member.issym() or member.islnk() or member.isdev():
            raise SystemExit("error: links/devices are forbidden in artifact archive")
    bundle.extractall(destination)
PY

response="$(aws_cli ec2 describe-instances --instance-ids "$instance_id")"
state_path="$(save_instance_state "$response")"
cp "$state_path" "${destination}/instance-metadata.json"
python3 - "${destination}/instance-metadata.json" "$run_id" "$archive_sha" <<'PY' \
  >"${destination}/fetch-metadata.json"
from datetime import datetime, timezone
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    instance = json.load(stream)
now = datetime.now(timezone.utc)
launch = datetime.fromisoformat(instance["launch_time"].replace("Z", "+00:00"))
elapsed = max(0.0, (now - launch).total_seconds())
rate = instance.get("on_demand_hourly_rate_usd")
json.dump({
    "schema": "oneshotsea.aws-fetch.v1",
    "fetched_at": now.strftime("%Y-%m-%dT%H:%M:%SZ"),
    "run_id": sys.argv[2], "instance_id": instance["instance_id"],
    "region": instance["region"], "instance_type": instance["instance_type"],
    "architecture": instance["architecture"], "launch_time": instance["launch_time"],
    "on_demand_hourly_rate_usd": rate,
    "elapsed_since_launch_seconds": elapsed,
    "estimated_upper_bound_cost_usd": None if rate is None else elapsed * rate / 3600,
    "cost_note": "Launch-to-fetch wall time times recorded on-demand rate; AWS billing is authoritative.",
    "remote_archive_sha256": sys.argv[3],
}, sys.stdout, sort_keys=True, separators=(",", ":"))
sys.stdout.write("\n")
PY

# Stop the loopback server; leave the export archive and original run intact.
# shellcheck disable=SC2016
printf -v stop_command \
  'set -euo pipefail; pid_file=%q; if [[ -f "$pid_file" ]]; then pid=$(cat "$pid_file"); kill "$pid" 2>/dev/null || true; rm -f -- "$pid_file"; fi' \
  "${AWS_REMOTE_ROOT}/exports/http-${remote_port}.pid"
ssm_run_command "$instance_id" 'OneShotSEA stop artifact export' "$stop_command" 300 >/dev/null

(
  cd "$destination"
  while IFS= read -r -d '' path; do
    shasum -a 256 "$path"
  done < <(find . -type f ! -name SHA256SUMS ! -name '*.tmp.*' -print0)
) >"${destination}/SHA256SUMS"

kill "$session_pid" 2>/dev/null || true
wait "$session_pid" 2>/dev/null || true
session_pid=''
trap - EXIT
printf '%s\n' "$destination"
