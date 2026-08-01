#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

repo_root="$(cd "${SCRIPT_DIR}/../.." && pwd)"
instance_id=''
jobs=16
install_dependencies=1
source_url='https://github.com/IslayResearch/OneShotSEA.git'

usage() {
  cat <<'EOF'
Usage: deploy.sh [--instance-id ID] [--repo PATH] [--jobs N]
                 [--skip-package-install] [--execute]

Builds exactly the clean local HEAD after fetching that commit from the public
canonical GitHub repository on the instance through keyless Systems Manager.
The deployment directory, environment record, build argv, and executable
SHA-256 are immutable inputs to worker identity. Dry-run does not contact AWS.
EOF
}

while (( $# )); do
  case "$1" in
    --instance-id) instance_id="${2:-}"; shift 2 ;;
    --repo) repo_root="${2:-}"; shift 2 ;;
    --jobs) jobs="${2:-}"; shift 2 ;;
    --skip-package-install) install_dependencies=0; shift ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

instance_id="$(load_instance_id "$instance_id")"
validate_positive_uint jobs "$jobs"
(( 10#$jobs <= 256 )) || die 'jobs may not exceed 256'
validate_remote_root
require_cmd git
repo_root="$(cd "$repo_root" && pwd)"
git -C "$repo_root" rev-parse --is-inside-work-tree >/dev/null 2>&1 ||
  die 'not a Git worktree'
commit="$(git -C "$repo_root" rev-parse HEAD)"
[[ "$commit" =~ ^[0-9a-f]{40}$ ]] || die 'unexpected Git commit id'

if ! require_execute; then
  printf 'DRY-RUN: require SSM Online for %s in %s\n' "$instance_id" "$AWS_EC2_REGION"
  printf 'DRY-RUN: fetch public %s commit %s into %s/deployments/%s\n' \
    "$source_url" "$commit" "$AWS_REMOTE_ROOT" "$commit"
  printf 'DRY-RUN: remote package install=%s; build argv=' "$install_dependencies"
  printf ' %q' make -j "$jobs" all
  printf '\n'
  printf 'DRY-RUN: record environment and binary SHA-256; update current only after a successful build\n'
  exit 0
fi

[[ -z "$(git -C "$repo_root" status --porcelain=v1)" ]] ||
  die 'deployment requires a clean worktree so local HEAD is an unambiguous identity'
ssm_require_online "$instance_id"

# The program is fixed; all varying values are validated and passed as quoted
# positional parameters. The public source URL carries no credential.
# shellcheck disable=SC2016
remote_bootstrap='set -euo pipefail
root=$1; commit=$2; jobs=$3; install_dependencies=$4; source_url=$5; instance_id=$6
deploy_dir="$root/deployments/$commit"
if [[ "$install_dependencies" == 1 ]]; then
  dnf install -y gcc gcc-c++ make gmp-devel libgomp tmux python3 git tar gzip time
fi
mkdir -p "$deploy_dir"
test -z "$(find "$deploy_dir" -mindepth 1 -maxdepth 1 -print -quit)"
git -C "$deploy_dir" init -q
git -C "$deploy_dir" remote add origin "$source_url"
git -C "$deploy_dir" fetch --depth=1 origin "$commit"
git -C "$deploy_dir" checkout -q --detach FETCH_HEAD
[[ "$(git -C "$deploy_dir" rev-parse HEAD)" == "$commit" ]]
git -C "$deploy_dir" remote remove origin
{
  printf "deployed_utc=%s\n" "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf "git_commit=%s\n" "$commit"
  printf "instance_id=%s\n" "$instance_id"
  printf "build_argv=make -j %s all\n" "$jobs"
  uname -a
  lscpu || true
  free -h || true
  c++ --version | head -n 1
  make --version | head -n 1
  rpm -q gmp-devel libgomp 2>/dev/null || true
} >"$deploy_dir/environment.txt"
(cd "$deploy_dir" && make -j "$jobs" all) 2>&1 | tee "$deploy_dir/build.log"
binary="$deploy_dir/build/oneshotsea"
[[ -x "$binary" ]]
binary_sha256=$(sha256sum "$binary" | awk "{print \$1}")
environment_sha256=$(sha256sum "$deploy_dir/environment.txt" | awk "{print \$1}")
manifest="$deploy_dir/build-manifest.json"
python3 - "$commit" "$instance_id" "$jobs" "$binary_sha256" "$environment_sha256" \
  "$(date -u +%Y-%m-%dT%H:%M:%SZ)" <<"PY" >"$manifest"
import json
import platform
import sys
commit, instance_id, jobs, binary_sha, environment_sha, built_utc = sys.argv[1:]
json.dump({
    "schema": "oneshotsea.aws-build.v1", "deployment_commit": commit,
    "instance_id": instance_id, "architecture": platform.machine(),
    "build_argv": ["make", "-j", jobs, "all"],
    "binary_relative_path": "build/oneshotsea", "binary_sha256": binary_sha,
    "environment_sha256": environment_sha, "built_utc": built_utc,
}, sys.stdout, sort_keys=True, separators=(",", ":"))
sys.stdout.write("\n")
PY
ln -sfn "$deploy_dir" "$root/current"
printf "deployment=%s\nbinary_sha256=%s\n" "$deploy_dir" "$binary_sha256"
'
printf -v remote_command 'bash -c %q -- %q %q %q %q %q %q' \
  "$remote_bootstrap" "$AWS_REMOTE_ROOT" "$commit" "$jobs" \
  "$install_dependencies" "$source_url" "$instance_id"
ssm_run_command "$instance_id" 'OneShotSEA immutable deploy' "$remote_command" 86400
