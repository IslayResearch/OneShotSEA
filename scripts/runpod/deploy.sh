#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

repo_root="$(cd "${SCRIPT_DIR}/../.." && pwd)"
build_command=''

usage() {
  cat <<'EOF'
Usage: deploy.sh [--repo PATH] [--build-command COMMAND] [--execute]

Deploys exactly the clean, committed HEAD via `git archive` into a versioned
directory below /workspace/OneShotSEA/deployments. It never copies .git,
untracked files, API keys, SSH keys, or the operator environment. An optional
non-secret build command runs natively on the pod before `current` is updated.
EOF
}

while (( $# )); do
  case "$1" in
    --repo) repo_root="${2:-}"; shift 2 ;;
    --build-command) build_command="${2:-}"; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

require_cmd git
repo_root="$(cd "$repo_root" && pwd)"
git -C "$repo_root" rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "not a Git worktree"
commit="$(git -C "$repo_root" rev-parse HEAD)"
[[ "$commit" =~ ^[0-9a-f]{40}$ ]] || die "unexpected Git commit id"
validate_remote_root
build_ssh_args

if ! require_execute; then
  print_cmd git -C "$repo_root" archive --format=tar "$commit"
  print_cmd ssh "${SSH_ARGS[@]}" "$SSH_DESTINATION" \
    "extract commit ${commit} to ${RUNPOD_REMOTE_ROOT}/deployments/${commit}"
  if [[ -n "$build_command" ]]; then
    printf 'DRY-RUN: remote build command: %s\n' "$build_command"
  else
    printf 'DRY-RUN: no remote build command requested\n'
  fi
  exit 0
fi

[[ -z "$(git -C "$repo_root" status --porcelain=v1)" ]] ||
  die "deployment requires a clean worktree so the remote tree is reproducible"

remote_deploy_dir="${RUNPOD_REMOTE_ROOT}/deployments/${commit}"
printf -v quoted_deploy_dir '%q' "$remote_deploy_dir"
# Values below have already been shell-escaped with printf %q.
# shellcheck disable=SC2029
ssh "${SSH_ARGS[@]}" "$SSH_DESTINATION" \
  "set -euo pipefail; mkdir -p ${quoted_deploy_dir}; test -z \"\$(find ${quoted_deploy_dir} -mindepth 1 -maxdepth 1 -print -quit)\""
# shellcheck disable=SC2029
git -C "$repo_root" archive --format=tar "$commit" |
  ssh "${SSH_ARGS[@]}" "$SSH_DESTINATION" \
    "set -euo pipefail; tar -xf - -C ${quoted_deploy_dir}"

# Positional parameters keep operator input out of the remote script body.
# shellcheck disable=SC2016
remote_bootstrap='set -euo pipefail
root=$1
commit=$2
build_command=$3
deploy_dir="$root/deployments/$commit"
{
  printf "deployed_utc=%s\n" "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf "git_commit=%s\n" "$commit"
  printf "build_command="; printf "%q" "$build_command"; printf "\n"
  uname -a
  command -v nvidia-smi >/dev/null && nvidia-smi --query-gpu=name,uuid,driver_version,memory.total --format=csv,noheader || true
  command -v nvcc >/dev/null && nvcc --version || true
  command -v cmake >/dev/null && cmake --version | head -n 1 || true
  command -v c++ >/dev/null && c++ --version | head -n 1 || true
} >"$deploy_dir/environment.txt"
if [[ -n "$build_command" ]]; then
  (cd "$deploy_dir" && bash -lc "$build_command") 2>&1 | tee "$deploy_dir/build.log"
fi
ln -sfn "$deploy_dir" "$root/current"
printf "%s\n" "$deploy_dir"
'
printf -v remote_invocation 'bash -c %q -- %q %q %q' \
  "$remote_bootstrap" "$RUNPOD_REMOTE_ROOT" "$commit" "$build_command"
# shellcheck disable=SC2029
ssh "${SSH_ARGS[@]}" "$SSH_DESTINATION" "$remote_invocation"
